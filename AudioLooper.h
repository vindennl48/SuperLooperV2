#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Memory.h"

class AudioLooper : public AudioStream {
public:
    AudioLooper(void) : AudioStream(1, inputQueueArray) {
        // Create 2x loops allocating to MEM0
        loop1 = new MemorySd(0, LOOP_BUFFER_SIZE);
        loop2 = new MemorySd(0, LOOP_BUFFER_SIZE);

        // Create 1 input buffer allocating to MEM1
        inputBuffer = new MemoryRam(1, LOOP_BUFFER_SIZE);
    }

    // Call this from the main loop() function as often as possible
    void poll() {
        audio_block_t tempBlock;
        // Drain the RAM buffer into the SD card
        // This decouples the strict timing of the audio interrupt from the variable timing of SD writes
        while (inputBuffer->pop(&tempBlock)) {
            loop1->writeToSd(&tempBlock);
        }
    }

    void startRecording() {
        // Clear any old data before starting
        inputBuffer->reset();
        
        // We also likely want to reset loop1's file/playhead here if we are overwriting
        // For now, we just ensure the file is ready to accept new data
        // loop1->reset(); // Uncomment if startRecording should always wipe the old loop
        
        recording = true;
    }

    void stopRecording() {
        recording = false;
        // We do NOT reset inputBuffer here. 
        // poll() needs to finish draining the remaining data to the SD card.
    }

    virtual void update(void) {
        audio_block_t *inputBlock;
        audio_block_t *outputBlock;

        outputBlock = allocate();
        if (!outputBlock) {
            // If we can't allocate output, we must still consume input to avoid stalling
            inputBlock = receiveReadOnly(0);
            if (inputBlock) release(inputBlock);
            return;
        }

        inputBlock = receiveReadOnly(0);

        if (inputBlock) {
            // Copy dry signal to output
            memcpy(outputBlock->data, inputBlock->data, sizeof(outputBlock->data));
            
            // Handle Recording to Buffer (Producer)
            if (recording) {
                // push returns false if buffer is full, effectively dropping the packet
                inputBuffer->push(inputBlock); 
            }

            release(inputBlock);
        } else {
            // No input, silence the output
            memset(outputBlock->data, 0, sizeof(outputBlock->data));
        }

        transmit(outputBlock, 0);
        release(outputBlock);
    }

private:
    audio_block_t *inputQueueArray[1];
    MemorySd *loop1;
    MemorySd *loop2;
    MemoryRam *inputBuffer;
    
    volatile bool recording = false;
};

#endif // AUDIO_LOOPER_H
