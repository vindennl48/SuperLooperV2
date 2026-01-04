#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Track.h"

class AudioLooper : public AudioStream {
public:
    enum LooperState {
        IDLE,
        RECORDING,
        PLAYBACK,
        FULL_PLAYBACK
    };

    AudioLooper(void) : AudioStream(1, inputQueueArray) {
        for (int i = 0; i < NUM_LOOPS; i++) {
            tracks[i] = new Track();
        }
        currentTrackIndex = 0;
        state = IDLE;
        globalPlayhead = 0;
        timelineLength = 0;
    }

    void begin() {
        // Clear SD card state on startup for a fresh session
        MemorySd::removeAllFiles();

        for (int i = 0; i < NUM_LOOPS; i++) {
            if (tracks[i]) {
                tracks[i]->begin();
                LOG("AudioLooper: Track %d initialized", i);
            }
        }
    }

    // Call this from the main loop() function frequently
    void poll() {
        for (int i = 0; i < NUM_LOOPS; i++) {
            if (tracks[i]) tracks[i]->poll();
        }
    }

    void trigger() {
        Track* currentTrack = tracks[currentTrackIndex];
        if (!currentTrack) return;

        switch (state) {
            case IDLE:
                // Start Recording First Loop
                LOG("AudioLooper: IDLE -> RECORDING (Track %d)", currentTrackIndex);
                currentTrack->record();
                
                // Reset Global Time only on fresh start
                timelineLength = 0;
                globalPlayhead = 0;
                
                state = RECORDING;
                break;

            case RECORDING:
                // Stop Recording Current Loop, Start Playback
                LOG("AudioLooper: RECORDING -> PLAYBACK (Track %d)", currentTrackIndex);
                currentTrack->play();
                
                // Update Global Timeline if this track is the longest so far
                if (currentTrack->getLengthInBlocks() > timelineLength) {
                    timelineLength = currentTrack->getLengthInBlocks();
                    LOG("AudioLooper: Timeline updated to %d blocks", timelineLength);
                }
                
                // Check if we have more loops to record
                if (currentTrackIndex < NUM_LOOPS - 1) {
                    currentTrackIndex++;
                    state = PLAYBACK; // Ready for next trigger to record
                } else {
                    state = FULL_PLAYBACK; // All loops recorded
                    LOG("AudioLooper: All Tracks Recorded -> FULL_PLAYBACK");
                }
                
                // NOTE: We do NOT reset globalPlayhead here to keep time continuity.
                break;

            case PLAYBACK:
                // Triggered while playing back (and not full): Start Recording Next Loop
                LOG("AudioLooper: PLAYBACK -> RECORDING (Track %d)", currentTrackIndex);
                tracks[currentTrackIndex]->record();
                state = RECORDING;
                break;
                
            case FULL_PLAYBACK:
                // Triggered while everything is full: Stop All
                LOG("AudioLooper: FULL_PLAYBACK -> IDLE (Reset)");
                for(int i=0; i<NUM_LOOPS; i++) {
                    if(tracks[i]) tracks[i]->stop();
                }
                currentTrackIndex = 0;
                state = IDLE;
                break;
        }
    }

    virtual void update(void) {
        audio_block_t *inBlock = receiveReadOnly(0);
        audio_block_t *outBlock = allocate();

        if (!outBlock) {
            if (inBlock) release(inBlock);
            return;
        }

        // Initialize Output with Dry Signal or Silence
        if (inBlock) {
            memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
        } else {
            memset(outBlock->data, 0, sizeof(outBlock->data));
        }

        // --- Process Tracks ---
        
        // Temporary buffer for mixing
        audio_block_t tempTrackOut; 

        for (int i = 0; i < NUM_LOOPS; i++) {
            Track* t = tracks[i];
            if (!t) continue;

            // Render track
            t->tick(inBlock, &tempTrackOut);

            // Mix if playing
            if (t->getState() == Track::PLAYBACK || t->getState() == Track::IDLE) {
                 for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
                    int32_t sample = outBlock->data[s] + tempTrackOut.data[s];
                    
                    // Simple Hard Limiter
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    
                    outBlock->data[s] = (int16_t)sample;
                }
            }
        }

        // --- Update Global Timeline ---
        if (timelineLength > 0) {
            // Only advance playhead if we are active (not IDLE)
            if (state != IDLE) {
                globalPlayhead++;
                if (globalPlayhead >= timelineLength) {
                    globalPlayhead = 0;
                }
            }
        }

        transmit(outBlock, 0);
        release(outBlock);
        if (inBlock) release(inBlock);
    }
    
    LooperState getState() { return state; }
    uint32_t getPlayhead() { return globalPlayhead; }
    uint32_t getTimelineLength() { return timelineLength; }

private:
    audio_block_t *inputQueueArray[1];
    Track* tracks[NUM_LOOPS];
    int currentTrackIndex;
    
    volatile LooperState state;
    uint32_t globalPlayhead;
    uint32_t timelineLength;
};

#endif // AUDIO_LOOPER_H