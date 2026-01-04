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

        // Add loop audio here...

        transmit(outBlock);
        release(outBlock);
        if (inBlock) release(inBlock);
    }

private:
    audio_block_t *inputQueueArray[1];
    MemorySd *loop1;
    MemorySd *loop2;
    MemoryRam *inputBuffer;
};

#endif // AUDIO_LOOPER_H
