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
        // Clear SD card state
        MemorySd::removeAllFiles();

        // Create Loop 1 on MEM0
        if (!loop1) {
          loop1 = new MemorySd(0, LOOP_BUFFER_SIZE); 
          if (loop1) LOG("loop1 Created on MEM0!");
        }
        
        // Create Loop 2 on MEM1 (Distribute load)
        if (!loop2) {
          loop2 = new MemorySd(1, LOOP_BUFFER_SIZE);
          if (loop2) LOG("loop2 Created on MEM1!");
        }
    }

    // Call this from the main loop() function as often as possible
    void poll() {
        if (loop1) loop1->update();
        if (loop2) loop2->update();
    }

    void trigger() {
        switch (state) {
            case IDLE:
                LOG("AudioLooper: IDLE -> RECORDING");
                if (loop1) {
                    loop1->clearLoop();
                }
                state = RECORDING;
                break;

            case RECORDING:
                LOG("AudioLooper: RECORDING -> PLAYBACK");
                // Optional: Force read head to 0 to ensure immediate playback of what was just recorded
                if (loop1) loop1->restartPlayback();
                state = PLAYBACK;
                break;

            case PLAYBACK:
                LOG("AudioLooper: PLAYBACK -> IDLE (Stop)");
                // Implementation choice: Go to IDLE or Overdub? 
                // For now, let's just loop back to IDLE or stay in Playback.
                // User example had "Staying in PLAYBACK", but let's allow stopping.
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

        // 1. Setup Output with Dry Signal
        if (inBlock) {
            memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
            
            // 2. Handle Recording
            if (state == RECORDING && loop1) {
                loop1->writeSample(inBlock);
            }
        } else {
            memset(outBlock->data, 0, sizeof(outBlock->data));
        }

        // 3. Handle Playback (Mixing)
        // Note: We can Play AND Record simultaneously if we add an OVERDUB state later.
        if (state == PLAYBACK && loop1) {
            if (loop1->readSample(&loopBlock)) {
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    int32_t sample = outBlock->data[i] + loopBlock.data[i];
                    
                    // Hard limiter
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