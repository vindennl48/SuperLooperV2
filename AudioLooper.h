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

        // Handle Recording to Buffer (Producer)
        if (recording) {
            // push returns false if buffer is full, effectively dropping the packet
            inputBuffer->push(inputBlock); 
        }

        transmit(outBlock);
        release(outBlock);
        if (inBlock) release(inBlock);
    }

private:
    audio_block_t *inputQueueArray[1];
    MemorySd *loop1;
    MemorySd *loop2;
    MemoryRam *inputBuffer;
    
    volatile bool recording = false;
};

#endif // AUDIO_LOOPER_H
