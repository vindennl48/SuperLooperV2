#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Memory.h"

class AudioLooper : public AudioStream {
public:
    enum LooperState {
        IDLE,
        RECORDING,
        PLAYBACK
    };

    AudioLooper(void) : AudioStream(1, inputQueueArray) {
        loop1 = nullptr;
        loop2 = nullptr;
        state = IDLE;
    }

    void begin() {
        // Clear SD card state on startup for a fresh session
        MemorySd::removeAllFiles();

        // Create Loop 1 on MEM0 (Chip Index 0)
        if (!loop1) {
            loop1 = new MemorySd(0, LOOP_BUFFER_SIZE); 
            if (loop1) LOG("AudioLooper: Loop1 created on MEM0");
        }
        
        // Create Loop 2 on MEM1 (Chip Index 1) - currently unused but initialized
        if (!loop2) {
            loop2 = new MemorySd(1, LOOP_BUFFER_SIZE);
            if (loop2) LOG("AudioLooper: Loop2 created on MEM1");
        }
    }

    // Call this from the main loop() function frequentyl to handle SD transfers
    void poll() {
        if (loop1) loop1->update();
        if (loop2) loop2->update();
    }

    void trigger() {
        switch (state) {
            case IDLE:
                // Start Recording
                LOG("AudioLooper: IDLE -> RECORDING");
                if (loop1) {
                    loop1->clearLoop(); // Reset buffers and files
                }
                state = RECORDING;
                break;

            case RECORDING:
                // Stop Recording, Start Playback
                LOG("AudioLooper: RECORDING -> PLAYBACK");
                if (loop1) {
                    loop1->finishRecording(); // Finalize the loop length
                }
                // Note: We do NOT restartPlayback() here. 
                // The Output Buffer has been auto-filling during recording (pre-fetch),
                // so it is primed and ready for instant playback.
                state = PLAYBACK;
                break;

            case PLAYBACK:
                // Stop Playback
                LOG("AudioLooper: PLAYBACK -> IDLE");
                state = IDLE;
                break;
        }
    }

    virtual void update(void) {
        audio_block_t *inBlock = receiveReadOnly(0);
        audio_block_t *outBlock = allocate();
        audio_block_t loopBlock; 

        if (!outBlock) {
            if (inBlock) release(inBlock);
            return;
        }

        // 1. Pass-through Dry Signal
        if (inBlock) {
            memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
            
            // 2. Handle Recording
            if (state == RECORDING && loop1) {
                // Write received audio to Input Buffer (RAM -> SD)
                loop1->writeSample(inBlock);
            }
        } else {
            // Generate silence if no input
            memset(outBlock->data, 0, sizeof(outBlock->data));
        }

        // 3. Handle Playback (Mixing Loop)
        if (state == PLAYBACK && loop1) {
            // Read from Output Buffer (SD -> RAM)
            if (loop1->readSample(&loopBlock)) {
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    int32_t sample = outBlock->data[i] + loopBlock.data[i];
                    
                    // Simple Hard Limiter to prevent wrap-around clipping
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    
                    outBlock->data[i] = (int16_t)sample;
                }
            }
        }

        transmit(outBlock, 0);
        release(outBlock);
        if (inBlock) release(inBlock);
    }
    
    LooperState getState() { return state; }

private:
    audio_block_t *inputQueueArray[1];
    MemorySd *loop1;
    MemorySd *loop2;
    
    volatile LooperState state;
};

#endif // AUDIO_LOOPER_H