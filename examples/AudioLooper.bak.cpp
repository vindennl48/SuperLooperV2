#include "AudioLooper.h"

AudioLooper::AudioLooper()
    : AudioStream(1, m_inputQueueArray),
      m_state(STATE_IDLE),
      m_trackManager(nullptr),
      m_activeTrackIdx(0),
      m_isFirstCycle(true),
      m_masterLoopLength(0),
      m_currentPos(0),
      m_visibleTrackCount(NUM_AUDIO_TRACKS)
{
    for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
        m_trackActive[i] = false;
    }
}

void AudioLooper::setTrackManager(TrackManager* tm) {
    m_trackManager = tm;
}

void AudioLooper::setLoopDepth(float depth) {
    if (!m_trackManager) return;

    int activeCount = 0;
    for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
        if (m_trackActive[i]) activeCount++;
    }

    if (activeCount <= 1) {
        m_visibleTrackCount = activeCount; 
        return;
    }

    int numZones = activeCount; 
    if (depth >= 0.99f) {
        m_visibleTrackCount = activeCount;
    } else {
        int state = (int)(depth * numZones);
        m_visibleTrackCount = state + 1;
    }
    
    if (m_visibleTrackCount < 1) m_visibleTrackCount = 1;
    if (m_visibleTrackCount > activeCount) m_visibleTrackCount = activeCount;

    for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
        if (m_trackActive[i]) {
            if (i < m_visibleTrackCount) {
                m_trackManager->unmuteTrack(i);
            } else {
                m_trackManager->muteTrack(i);
            }
        }
    }
}

void AudioLooper::trigger() {
    if (!m_trackManager) return;

    // trigger() handles footswitch state TRANSITIONS.
    // Action logic (startRecording, etc.) is handled in update() based on state.
    if (m_state == STATE_IDLE) {
        // [FS1] Arm/Start Master Recording
        m_state = STATE_RECORD_MASTER;
        m_currentPos = 0;
        m_activeTrackIdx = 0;
        LOG("Looper: [FS1] Start Master Record Triggered");
    } 
    else if (m_state == STATE_RECORD_MASTER) {
        // [FS2] Finalize Master Recording
        m_masterLoopLength = m_currentPos;
        m_currentPos = 0; // Prepare to transition to PLAY
        m_trackActive[0] = true;
        m_visibleTrackCount = 1; 
        m_state = STATE_PLAY;
        LOG("Looper: [FS2] Stop Master Record Triggered (Length: %u)", m_masterLoopLength);
    }
    else if (m_state == STATE_PLAY) {
        // [FS3] Arm Slave Recording
        m_activeTrackIdx = m_visibleTrackCount; 
        if (m_activeTrackIdx < NUM_AUDIO_TRACKS) {
            m_state = STATE_ARM_RECORD;
            m_isFirstCycle = true; 
            LOG("Looper: [FS3] Arm Slave Record (Track %d)", m_activeTrackIdx + 1);
        } else {
            LOG("Looper: ALL TRACKS FULL");
        }
    }
    else if (m_state == STATE_RECORD_SLAVE) {
        // [FS4] Arm Stop Slave
        m_state = STATE_ARM_STOP;
        LOG("Looper: [FS4] Arm Slave Stop");
    }
}

void AudioLooper::stopAndClear() {
    if (m_state == STATE_IDLE || !m_trackManager) return;

    m_state = STATE_IDLE;
    m_currentPos = 0;
    m_masterLoopLength = 0;
    m_visibleTrackCount = 0;
    
    for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
        m_trackActive[i] = false;
        m_trackManager->stopTrack(i);
    }

    LOG("Looper: STOP ALL");
}

void AudioLooper::update(void) {
    audio_block_t *inBlock = receiveReadOnly(0);
    audio_block_t *outBlock = allocate();

    if (!outBlock) {
        if (inBlock) release(inBlock);
        return;
    }

    if (inBlock) {
        memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
    } else {
        memset(outBlock->data, 0, sizeof(outBlock->data));
    }

    switch (m_state) {
        case STATE_IDLE:
            // Do absolutely nothing. Waiting for trigger().
            break;

        case STATE_RECORD_MASTER:
            // Start recording on the very first block of this state
            if (m_currentPos == 0) {
                m_trackManager->startRecording(0);
                LOG("Looper: Recording Started (Block 0)");
            }
            handleRecordMaster(inBlock, outBlock);
            break;

        case STATE_PLAY:
        case STATE_ARM_RECORD:
        case STATE_RECORD_SLAVE:
        case STATE_ARM_STOP:
            // Sync: If we just transitioned to PLAY from RECORD_MASTER
            if (m_state == STATE_PLAY && m_currentPos == 0 && m_masterLoopLength > 0) {
                m_trackManager->stopRecording(0);
                LOG("Looper: Recording Stopped (Master Committed)");
            }
            handlePlay(inBlock, outBlock);
            break;
    }

    transmit(outBlock);
    release(outBlock);
    if (inBlock) release(inBlock);
}

void AudioLooper::handleRecordMaster(audio_block_t *in, audio_block_t *out) {
    if (!in || !m_trackManager) return;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        m_trackManager->pushToRecord(0, in->data[i]);
    }

    m_currentPos += AUDIO_BLOCK_SAMPLES;
}

void AudioLooper::handlePlay(audio_block_t *in, audio_block_t *out) {
    if (m_masterLoopLength == 0 || !m_trackManager) return;

    // 1. Mixing
    for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
        int32_t mix = out->data[s]; 

        for (int t = 0; t < NUM_AUDIO_TRACKS; t++) {
            if (m_trackActive[t]) {
                int16_t sample = m_trackManager->pullForPlayback(t);
                mix += sample;
            }
        }

        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out->data[s] = (int16_t)mix;
    }

    // 2. Slave Capture
    if (m_state == STATE_RECORD_SLAVE || m_state == STATE_ARM_STOP) {
        if (in) {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                m_trackManager->pushToRecord(m_activeTrackIdx, in->data[i]);
            }
        } else {
             for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                m_trackManager->pushToRecord(m_activeTrackIdx, 0);
            }
        }
    }

    // 3. Quantization & Transitions
    uint32_t nextPos = m_currentPos + AUDIO_BLOCK_SAMPLES;
    bool wrapping = (nextPos >= m_masterLoopLength);

    if (m_state == STATE_ARM_RECORD && wrapping) {
        m_state = STATE_RECORD_SLAVE;
        m_isFirstCycle = true; 
        m_trackManager->startRecording(m_activeTrackIdx);
        LOG("Looper: Loop Start! START RECORD SLAVE (Track %d)", m_activeTrackIdx + 1);
    } 
    else if (m_state == STATE_ARM_STOP && wrapping) {
        m_state = STATE_PLAY;
        m_trackActive[m_activeTrackIdx] = true;
        m_visibleTrackCount = m_activeTrackIdx + 1;
        m_trackManager->stopRecording(m_activeTrackIdx);
        LOG("Looper: Loop End! STOP RECORD SLAVE (Track %d)", m_activeTrackIdx + 1);
    }

    // 4. Advance
    if (wrapping) {
        m_currentPos = 0;
        if (m_state == STATE_RECORD_SLAVE) m_isFirstCycle = false;
    } else {
        m_currentPos = nextPos;
    }
}