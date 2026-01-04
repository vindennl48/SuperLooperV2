#include "AudioLooper.h"

AudioLooper::AudioLooper()
    : AudioStream(1, m_inputQueueArray),
      m_state(STATE_IDLE),
      m_memory(nullptr),
      m_clearRequested(false),
      m_activeTrackIdx(0),
      m_slaveOverdubMode(true),
      m_isFirstCycle(true),
      m_masterLoopLength(0),
      m_currentPos(0),
      m_visibleTrackCount(8),
      m_masterFade(1.0f)
{
    for (int i = 0; i < 8; i++) {
        m_trackActive[i] = false;
        m_trackGain[i] = 1.0f; 
    }
}

void AudioLooper::setMemoryManager(MemoryManager* mem) {
    m_memory = mem;
}

void AudioLooper::setLoopDepth(float depth) {
    int activeCount = 0;
    for (int i = 0; i < 8; i++) {
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
}

void AudioLooper::trigger() {
    switch (m_state) {
        case STATE_IDLE:
            m_state = STATE_RECORD_MASTER;
            m_currentPos = 0;
            m_activeTrackIdx = 0;
            m_masterFade = 1.0f;
            if (m_memory) m_memory->startRecording(0);
            LOG("Looper: START RECORD MASTER (Track 1)");
            break;

        case STATE_RECORD_MASTER:
            m_state = STATE_PLAY;
            m_masterLoopLength = m_currentPos;
            m_currentPos = 0;
            m_trackActive[0] = true;
            m_visibleTrackCount = 1; 
            if (m_memory) m_memory->stopRecording(0, m_masterLoopLength);
            {
                float durationSec = (float)m_masterLoopLength / 44100.0f;
                LOG("Looper: MASTER SET (Length: %u samples, %.2f sec)", m_masterLoopLength, durationSec);
            }
            break;

        case STATE_PLAY:
            m_activeTrackIdx = m_visibleTrackCount;

            for (int i = m_activeTrackIdx; i < 8; i++) {
                m_trackActive[i] = false;
            }

            if (m_activeTrackIdx < 8) {
                m_state = STATE_ARM_RECORD;
                m_isFirstCycle = true; 
                LOG("Looper: ARM RECORD (Track %d). Waiting for Loop Start...", m_activeTrackIdx + 1);
            } else {
                LOG("Looper: ALL TRACKS FULL");
            }
            break;

        case STATE_RECORD_SLAVE:
            m_state = STATE_ARM_STOP;
            LOG("Looper: ARM STOP. Finishing loop cycle...");
            break;

        default:
            break;
    }
}

void AudioLooper::stopAndClear() {
    if (m_state == STATE_IDLE) return;

    if (m_masterLoopLength == 0) {
        m_state = STATE_IDLE;
        m_currentPos = 0;
        m_visibleTrackCount = 0;
        for (int i = 0; i < 8; i++) {
            m_trackActive[i] = false;
            m_trackGain[i] = 1.0f; 
        }
        m_clearRequested = true;
        LOG("Looper: STOP & CLEAR ALL (Instant)");
        return;
    }

    m_state = STATE_FADE_OUT;
    LOG("Looper: FADING OUT...");
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
            break;
        case STATE_RECORD_MASTER:
            handleRecordMaster(inBlock, outBlock);
            break;
        case STATE_PLAY:
        case STATE_ARM_RECORD:
        case STATE_RECORD_SLAVE:
        case STATE_ARM_STOP:
            handlePlay(inBlock, outBlock);
            break;
        case STATE_FADE_OUT:
            handlePlay(inBlock, outBlock);
            m_masterFade -= 0.01f; 
            if (m_masterFade <= 0.0f) {
                m_masterFade = 0.0f;
                m_state = STATE_IDLE;
                m_currentPos = 0;
                m_masterLoopLength = 0;
                m_visibleTrackCount = 0;
                for (int i = 0; i < 8; i++) {
                    m_trackActive[i] = false;
                    m_trackGain[i] = 1.0f; 
                }
                m_masterFade = 1.0f; 
                m_clearRequested = true;
                LOG("Looper: STOP & CLEAR ALL");
            }
            break;
    }

    transmit(outBlock);
    release(outBlock);
    if (inBlock) release(inBlock);
}

void AudioLooper::handleRecordMaster(audio_block_t *in, audio_block_t *out) {
    if (!in || !m_memory) return;

    // Write to Global Buffer (For SD)
    m_memory->writeToGlobalBuffer(m_currentPos, in->data, AUDIO_BLOCK_SAMPLES);
    
    // Write to Read Buffer (For Instant Playback)
    m_memory->writeToReadBuffer(0, m_currentPos, in->data, AUDIO_BLOCK_SAMPLES);

    m_currentPos += AUDIO_BLOCK_SAMPLES;
}

void AudioLooper::handlePlay(audio_block_t *in, audio_block_t *out) {
    if (m_masterLoopLength == 0 || !m_memory) return;

    // 1. SUM: All active tracks
    for (int i = 0; i < 8; i++) {
        if (!m_trackActive[i]) continue;

        float target = (i < m_visibleTrackCount) ? 1.0f : 0.0f;
        if (m_trackGain[i] <= 0.001f && target == 0.0f) {
            m_trackGain[i] = 0.0f;
            continue;
        }

        m_memory->readTrack(i, m_currentPos, m_readBuffer, AUDIO_BLOCK_SAMPLES);
        
        for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
            if (m_trackGain[i] < target) m_trackGain[i] += MUTE_FADE_STEP;
            else if (m_trackGain[i] > target) m_trackGain[i] -= MUTE_FADE_STEP;

            uint32_t absPos = m_currentPos + s;
            float seamFade = 1.0f;
            if (absPos < FADE_SAMPLES) {
                seamFade = (float)absPos / (float)FADE_SAMPLES;
            } else if (absPos >= m_masterLoopLength - FADE_SAMPLES) {
                seamFade = (float)(m_masterLoopLength - absPos) / (float)FADE_SAMPLES;
            }

            float totalGain = m_trackGain[i] * seamFade * m_masterFade;
            int32_t trackSample = (int32_t)(m_readBuffer[s] * totalGain);
            int32_t val = out->data[s] + trackSample;
            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;
            out->data[s] = (int16_t)val;
        }
    }

    // 2. RECORD SLAVE
    if (m_state == STATE_RECORD_SLAVE || m_state == STATE_ARM_STOP) {
        if (in) {
            bool doOverdub = !m_isFirstCycle && m_slaveOverdubMode;

            if (doOverdub) {
                int16_t prevTake[AUDIO_BLOCK_SAMPLES];
                m_memory->readTrack(m_activeTrackIdx, m_currentPos, prevTake, AUDIO_BLOCK_SAMPLES);
                int16_t mixBuffer[AUDIO_BLOCK_SAMPLES];
                
                for (int s=0; s < AUDIO_BLOCK_SAMPLES; s++) {
                    uint32_t absPos = m_currentPos + s;
                    float fade = 1.0f;
                    if (absPos < FADE_SAMPLES) fade = (float)absPos / (float)FADE_SAMPLES;
                    else if (absPos >= m_masterLoopLength - FADE_SAMPLES) fade = (float)(m_masterLoopLength - absPos) / (float)FADE_SAMPLES;

                    int16_t fadedPrev = (int16_t)(prevTake[s] * fade * m_masterFade);
                    int32_t mix = in->data[s] + fadedPrev;
                    if (mix > 32767) mix = 32767;
                    if (mix < -32768) mix = -32768;
                    mixBuffer[s] = (int16_t)mix;

                    int32_t outVal = out->data[s] + fadedPrev;
                    if (outVal > 32767) outVal = 32767;
                    if (outVal < -32768) outVal = -32768;
                    out->data[s] = (int16_t)outVal;
                }
                
                m_memory->writeToGlobalBuffer(m_currentPos, mixBuffer, AUDIO_BLOCK_SAMPLES);
                m_memory->writeToReadBuffer(m_activeTrackIdx, m_currentPos, mixBuffer, AUDIO_BLOCK_SAMPLES);

            } else {
                m_memory->writeToGlobalBuffer(m_currentPos, in->data, AUDIO_BLOCK_SAMPLES);
                m_memory->writeToReadBuffer(m_activeTrackIdx, m_currentPos, in->data, AUDIO_BLOCK_SAMPLES);
            }
        }
    }

    // 3. QUANTIZATION
    uint32_t nextPos = m_currentPos + AUDIO_BLOCK_SAMPLES;
    bool wrapping = (nextPos >= m_masterLoopLength);

    if (m_state == STATE_ARM_RECORD && wrapping) {
        m_state = STATE_RECORD_SLAVE;
        m_isFirstCycle = true; 
        // Note: No 'justStarted' flag needed because m_currentPos resets immediately
        m_memory->startRecording(m_activeTrackIdx);
        LOG("Looper: Loop Start! START RECORD SLAVE (Track %d)", m_activeTrackIdx + 1);
    } 
    else if (m_state == STATE_ARM_STOP && wrapping) {
        m_state = STATE_PLAY;
        m_trackActive[m_activeTrackIdx] = true;
        m_visibleTrackCount = m_activeTrackIdx + 1;
        
        m_memory->stopRecording(m_activeTrackIdx, m_masterLoopLength);
        LOG("Looper: Loop End! STOP RECORD SLAVE (Track %d committed)", m_activeTrackIdx + 1);
    }

    // 4. ADVANCE
    if (wrapping) {
        m_currentPos = 0;
        if (m_state == STATE_RECORD_SLAVE) m_isFirstCycle = false;
    } else {
        m_currentPos = nextPos;
    }
}
