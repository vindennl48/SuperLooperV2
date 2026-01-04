#include "AudioLooper.h"

AudioLooper::AudioLooper()
    : AudioStream(1, m_inputQueueArray),
      state(EMPTY),
      quantizeLoopLength(0),
      currentSamplePos(0),
      visibleTrackCount(8)
{}

void AudioLooper::setLoopDepth(float depth) {
    int activeCount = 0;
    for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
        if (!trackManager.isTrackEmpty(i)) activeCount++;
    }

    if (activeCount <= 1) {
        visibleTrackCount = activeCount; 
        return;
    }

    int numZones = activeCount; 
    if (depth >= 0.99f) {
        visibleTrackCount = activeCount;
    } else {
        int state = (int)(depth * numZones);
        visibleTrackCount = state + 1;
    }
    
    if (visibleTrackCount < 1) visibleTrackCount = 1;
    if (visibleTrackCount > activeCount) visibleTrackCount = activeCount;
}

void AudioLooper::trigger() {
    switch (state) {
        case EMPTY:
            state = RECORD_MASTER;
            currentSamplePos = 0;
            activeTrack = 0;
            trackManager.startRecording(activeTrack);
            LOG("Looper: START RECORD MASTER (Track 1)");
            break;

        case RECORD_MASTER:
            state = PLAY;
            quantizeLoopLength = currentSamplePos;
            currentSamplePos = 0;
            visibleTrackCount = 1; 
            trackManager.stopRecording(activeTrack);
            {
                float durationSec = (float)quantizeLoopLength / 44100.0f;
                LOG("Looper: MASTER SET (Length: %u samples, %.2f sec)", quantizeLoopLength, durationSec);
            }
            break;

        case PLAY:
            activeTrack = visibleTrackCount;

            for (int i = activeTrack; i < 8; i++) {
                m_trackActive[i] = false; // TODO: NEED TO FIX THIS
                trackManager.eraseTrack(i);
            }

            if (activeTrack < 8) {
                state = ARM_RECORD;
                LOG("Looper: ARM RECORD (Track %d). Waiting for Loop Start...", activeTrack + 1);
            } else {
                LOG("Looper: ALL TRACKS FULL");
            }
            break;

        case RECORD_SLAVE:
            state = STATE_ARM_STOP;
            LOG("Looper: ARM STOP. Finishing loop cycle...");
            break;

        default:
            break;
    }
}

void AudioLooper::stopAndClear() {
    if (state == EMPTY) return;

    if (quantizeLoopLength == 0) {
        state = EMPTY;
        currentSamplePos = 0;
        visibleTrackCount = 0;
        for (int i = 0; i < 8; i++) {
            m_trackActive[i] = false;
            m_trackGain[i] = 1.0f; 
        }
        m_clearRequested = true;
        LOG("Looper: STOP & CLEAR ALL (Instant)");
        return;
    }

    state = STATE_FADE_OUT;
    LOG("Looper: FADING OUT...");
}

void AudioLooper::update(void) {
    audio_block_t *inBlock = receiveReadOnly(0);
    audio_block_t *outBlock = allocate();
    uint32_t nextSamplePos = WRAP_NUM(currentSamplePos, AUDIO_BLOCK_SAMPLES, quantizeLoopLength);

    if (!outBlock) {
        if (inBlock) release(inBlock);
        return;
    }

    if (inBlock) {
        memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
    } else {
        memset(outBlock->data, 0, sizeof(outBlock->data));
    }

    switch (state) {
        case EMPTY:
            break;
        case RECORD_MASTER:
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                pushToRecord(activeTrack, inBlock->data[i]);
            }
            break;
        case PLAY:
            for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
              if (trackManager.isTrackEmpty(i)) continue;

              // Peeling back layers logic
              if (i < visibleTrackCount) {
                trackManager.unmuteTrack(i);
              } else {
                trackManager.muteTrack(i);
              }

              // Summing and clipping tracks
              for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
                int32_t val = out->data[s] + trackManager.pullForPlayback(i);
                if (val > 32767) val = 32767;
                if (val < -32768) val = -32768;
                out->data[s] = (int16_t)val;
              }
            }
            break;

        case ARM_RECORD:
            if (currentSamplePos <> 0) break;

        case RECORD_SLAVE:

        case STATE_ARM_STOP:
            handlePlay(inBlock, outBlock);
            break;
        case STATE_FADE_OUT:
            handlePlay(inBlock, outBlock);
            m_masterFade -= 0.01f; 
            if (m_masterFade <= 0.0f) {
                m_masterFade = 0.0f;
                state = EMPTY;
                currentSamplePos = 0;
                quantizeLoopLength = 0;
                visibleTrackCount = 0;
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

    currentSamplePos = nextSamplePos;

    transmit(outBlock);
    release(outBlock);
    if (inBlock) release(inBlock);
}

void AudioLooper::handlePlay(audio_block_t *in, audio_block_t *out) {
    if (quantizeLoopLength == 0 || !trackManager) return;

    // 1. SUM: All active tracks
    for (int i = 0; i < 8; i++) {
        if (!m_trackActive[i]) continue;

        float target = (i < visibleTrackCount) ? 1.0f : 0.0f;
        if (m_trackGain[i] <= 0.001f && target == 0.0f) {
            m_trackGain[i] = 0.0f;
            continue;
        }

        m_memory->readTrack(i, currentSamplePos, m_readBuffer, AUDIO_BLOCK_SAMPLES);
        
        for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
            if (m_trackGain[i] < target) m_trackGain[i] += MUTE_FADE_STEP;
            else if (m_trackGain[i] > target) m_trackGain[i] -= MUTE_FADE_STEP;

            uint32_t absPos = currentSamplePos + s;
            float seamFade = 1.0f;
            if (absPos < FADE_SAMPLES) {
                seamFade = (float)absPos / (float)FADE_SAMPLES;
            } else if (absPos >= quantizeLoopLength - FADE_SAMPLES) {
                seamFade = (float)(quantizeLoopLength - absPos) / (float)FADE_SAMPLES;
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
    if (state == RECORD_SLAVE || state == STATE_ARM_STOP) {
        if (in) {
            bool doOverdub = !m_isFirstCycle && m_slaveOverdubMode;

            if (doOverdub) {
                int16_t prevTake[AUDIO_BLOCK_SAMPLES];
                m_memory->readTrack(activeTrack, currentSamplePos, prevTake, AUDIO_BLOCK_SAMPLES);
                int16_t mixBuffer[AUDIO_BLOCK_SAMPLES];
                
                for (int s=0; s < AUDIO_BLOCK_SAMPLES; s++) {
                    uint32_t absPos = currentSamplePos + s;
                    float fade = 1.0f;
                    if (absPos < FADE_SAMPLES) fade = (float)absPos / (float)FADE_SAMPLES;
                    else if (absPos >= quantizeLoopLength - FADE_SAMPLES) fade = (float)(quantizeLoopLength - absPos) / (float)FADE_SAMPLES;

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
                
                m_memory->writeToGlobalBuffer(currentSamplePos, mixBuffer, AUDIO_BLOCK_SAMPLES);
                m_memory->writeToReadBuffer(activeTrack, currentSamplePos, mixBuffer, AUDIO_BLOCK_SAMPLES);

            } else {
                m_memory->writeToGlobalBuffer(currentSamplePos, in->data, AUDIO_BLOCK_SAMPLES);
                m_memory->writeToReadBuffer(activeTrack, currentSamplePos, in->data, AUDIO_BLOCK_SAMPLES);
            }
        }
    }

    // 3. QUANTIZATION
    uint32_t nextPos = currentSamplePos + AUDIO_BLOCK_SAMPLES;
    bool wrapping = (nextPos >= quantizeLoopLength);

    if (state == ARM_RECORD && wrapping) {
        state = RECORD_SLAVE;
        m_isFirstCycle = true; 
        // Note: No 'justStarted' flag needed because currentSamplePos resets immediately
        m_memory->startRecording(activeTrack);
        LOG("Looper: Loop Start! START RECORD SLAVE (Track %d)", activeTrack + 1);
    } 
    else if (state == STATE_ARM_STOP && wrapping) {
        state = PLAY;
        m_trackActive[activeTrack] = true;
        visibleTrackCount = activeTrack + 1;
        
        m_memory->stopRecording(activeTrack, quantizeLoopLength);
        LOG("Looper: Loop End! STOP RECORD SLAVE (Track %d committed)", activeTrack + 1);
    }

    // 4. ADVANCE
    if (wrapping) {
        currentSamplePos = 0;
        if (state == RECORD_SLAVE) m_isFirstCycle = false;
    } else {
        currentSamplePos = nextPos;
    }
}
