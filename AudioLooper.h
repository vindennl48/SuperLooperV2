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
        PLAYBACK
    };

    AudioLooper(void) : AudioStream(1, inputQueueArray) {
        track1 = nullptr;
        track2 = nullptr;
        state = IDLE;
        globalPlayhead = 0;
        timelineLength = 0;
    }

    void begin() {
        // Clear SD card state on startup for a fresh session
        MemorySd::removeAllFiles();

        // Create Track 1 on MEM0
        if (!track1) {
            track1 = new Track(0, LOOP_BUFFER_SIZE); 
            if (track1) LOG("AudioLooper: Track1 created on MEM0");
        }
        
        // Create Track 2 on MEM1 (Reserved)
        if (!track2) {
            track2 = new Track(1, LOOP_BUFFER_SIZE);
            if (track2) LOG("AudioLooper: Track2 created on MEM1");
        }
    }

    // Call this from the main loop() function frequently
    void poll() {
        if (track1) track1->poll();
        if (track2) track2->poll();
    }

    void trigger() {
        switch (state) {
            case IDLE:
                // Start Recording Loop 1
                LOG("AudioLooper: IDLE -> RECORDING");
                if (track1) {
                    track1->record();
                    timelineLength = 0; // Reset timeline
                    globalPlayhead = 0;
                }
                state = RECORDING;
                break;

            case RECORDING:
                // Stop Recording Loop 1, Start Playback
                LOG("AudioLooper: RECORDING -> PLAYBACK");
                if (track1) {
                    track1->play();
                    // Set Global Timeline based on the first loop
                    timelineLength = track1->getLengthInBlocks();
                    LOG("AudioLooper: Timeline set to %d blocks", timelineLength);
                }
                globalPlayhead = 0;
                state = PLAYBACK;
                break;

            case PLAYBACK:
                // Stop Playback
                LOG("AudioLooper: PLAYBACK -> IDLE");
                if (track1) {
                    track1->stop();
                }
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
        
        // Track 1
        if (track1) {
            // Allocate a temp block for the track to render into
            // (We could optimize this by summing directly, but Track::tick expects a block to fill)
            // For now, we reuse the stack-allocated trackBlock logic if we can copy data out.
            // But Track::tick takes a pointer. We need a real buffer.
            // Let's use a temporary stack buffer for the track output.
            
            // NOTE: Allocating a full audio_block_t on stack is 256 bytes + overhead, safe for T4.1
            audio_block_t tempTrackOut; 
            
            track1->tick(inBlock, &tempTrackOut);

            // Mix Track Output into Main Output
            // Only mix if we are NOT recording (monitoring is usually dry only)
            // or if the track is playing back.
            if (track1->getState() == Track::PLAYBACK || track1->getState() == Track::IDLE) {
                 for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    int32_t sample = outBlock->data[i] + tempTrackOut.data[i];
                    
                    // Simple Hard Limiter
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    
                    outBlock->data[i] = (int16_t)sample;
                }
            }
        }
        
        // Track 2 (Placeholder logic)
        if (track2) {
            audio_block_t tempTrackOut;
            track2->tick(inBlock, &tempTrackOut);
             if (track2->getState() == Track::PLAYBACK) {
                 for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    int32_t sample = outBlock->data[i] + tempTrackOut.data[i];
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    outBlock->data[i] = (int16_t)sample;
                }
             }
        }

        // --- Update Global Timeline ---
        if (timelineLength > 0) {
            // Only advance playhead if we are in playback mode? 
            // Or does time always flow? Usually time flows if the master loop is running.
            // Since Track1 is master, if it's playing, time flows.
            if (track1 && track1->isPlaying()) {
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
    Track *track1;
    Track *track2;
    
    volatile LooperState state;
    uint32_t globalPlayhead;
    uint32_t timelineLength;
};

#endif // AUDIO_LOOPER_H