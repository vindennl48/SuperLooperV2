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
        // Create 2x loops allocating to MEM0
        loop1 = new MemorySd(0, LOOP_BUFFER_SIZE);
        loop2 = new MemorySd(0, LOOP_BUFFER_SIZE);

        // Create 1 input buffer allocating to MEM1
        inputBuffer = new MemoryRam(1, LOOP_BUFFER_SIZE);
        
        state = IDLE;
    }

    // Call this from the main loop() function as often as possible
    void poll() {
        // loops should ALWAYS update (handles deferred file operations)
        loop1->update();

        audio_block_t tempBlock;
        
        // 1. Drain the Input Buffer (Recording to SD)
        // We always check this, effectively background flushing whatever is left
        while (inputBuffer->pop(&tempBlock)) {
            loop1->writeToSd(&tempBlock);
        }
    }

    void trigger() {
        switch (state) {
            case IDLE:
                LOG("AudioLooper: State IDLE -> RECORDING");
                // IDLE -> RECORDING
                inputBuffer->reset();
                loop1->clearLoop(); // Clear old loop data
                state = RECORDING;
                break;

            case RECORDING:
                LOG("AudioLooper: State RECORDING -> PLAYBACK");
                // RECORDING -> PLAYBACK
                state = PLAYBACK;
                loop1->resetPlayhead(); // Start playing from beginning
                break;

            case PLAYBACK:
                // Stay in PLAYBACK indefinitely for testing
                LOG("AudioLooper: Trigger ignored (Staying in PLAYBACK)");
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

        // 1. Setup Output with Dry Signal & Record
        if (inBlock) {
            memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
            
            if (state == RECORDING) {
                // push returns false if buffer is full
                inputBuffer->push(inBlock); 
            }
        } else {
            memset(outBlock->data, 0, sizeof(outBlock->data));
        }

        // 2. Mix Loop 1 (Only in PLAYBACK)
        if (state == PLAYBACK && loop1->pop(&loopBlock)) {
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                int32_t sample = outBlock->data[i] + loopBlock.data[i];
                
                // Hard limiter
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
                
                outBlock->data[i] = (int16_t)sample;
            }
        }

        transmit(outBlock, 0);
        release(outBlock);
        if (inBlock) release(inBlock);
    }
    
    // Getter for UI feedback
    LooperState getState() { return state; }

private:
    audio_block_t *inputQueueArray[1];
    MemorySd *loop1;
    MemorySd *loop2;
    MemoryRam *inputBuffer;
    
    volatile LooperState state;
};

#endif // AUDIO_LOOPER_H