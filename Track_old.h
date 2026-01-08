#ifndef TRACK_H
#define TRACK_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Memory.h"

class Track {
public:
    enum TrackState {
        IDLE,
        RECORDING,
        PLAYBACK,
        STOPPING
    };

    Track() {
        memory = nullptr;
        state = IDLE;
        gain = 1.0f;
        muted = false;
        paused = false;
        blockCounter = 0;
        fadeStep = 0;
    }

    void begin() {
        if (memory) delete memory;
        memory = new MemorySd(LOOP_BUFFER_SIZE);
    }

    ~Track() {
        if (memory) delete memory;
    }

    // Call this from the main loop() for SD card maintenance
    void poll() {
        if (memory) memory->update();
    }

    // Audio Interrupt Handler
    // input: Audio block from the mixer (dry signal) to be recorded
    // output: Buffer to fill with playback audio
    void tick(audio_block_t* input, audio_block_t* output) {
        // Always ensure output is silent initially
        if (output) memset(output->data, 0, sizeof(output->data));
        else return; // Should not happen if caller allocates correctly

        if (!memory) return;

        // --- RECORDING ---
        if (state == RECORDING) {
            if (input) {
                memory->writeSample(input);
                blockCounter++;
            }
        }
        
        // --- PLAYBACK / IDLE / STOPPING ---
        else {
            // Check if we are effectively idle (no audio output needed)
            bool effectiveIdle = (state == IDLE) || 
                                 (state == STOPPING && fadeStep == 0) || 
                                 (paused && fadeStep == 0);
            
            if (effectiveIdle) {
                // If we finished stopping, ensure we are technically IDLE and reset
                if (state == STOPPING) {
                    state = IDLE;
                    memory->restartPlayback();
                }
                return; 
            }

            // Attempt to read from memory
            // We read directly into the output block
            bool hasData = memory->readSample(output);
            
            if (hasData) {
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    // Update Fade Step
                    bool shouldFadeIn = (state == PLAYBACK) && !paused && !muted;
                    
                    if (shouldFadeIn) {
                        if (fadeStep < FADE_SAMPLES) fadeStep++;
                    } else {
                        // IDLE, STOPPING, PAUSED, or MUTED
                        if (fadeStep > 0) fadeStep--;
                    }

                    // Calculate Gain fresh per sample to avoid exponential decay
                    float sampleGain = gain * ((float)fadeStep / FADE_SAMPLES);

                    int32_t val = (int32_t)(output->data[i] * sampleGain);
                    
                    // Simple Hard Limiter
                    if (val > 32767) val = 32767;
                    else if (val < -32768) val = -32768;
                    
                    output->data[i] = (int16_t)val;
                }
            }
        }
    }

    // --- State Control ---

    void record() {
        if (state == IDLE) {
            memory->clearLoop();
            blockCounter = 0;
            state = RECORDING;
            paused = false;
        }
    }

    void play() {
        if (state == RECORDING) {
            memory->finishRecording();
            state = PLAYBACK;
        } else if ((state == IDLE || state == STOPPING) && getLengthInBlocks() > 0) {
            if (state == IDLE) {
                if (memory) memory->restartPlayback();
            }
            state = PLAYBACK;
        }
        paused = false;
    }

    void stop() {
        if (state != IDLE) {
            state = STOPPING;
        }
        paused = false;
    }

    // --- Feature Control ---

    // For Late-Start Forgiveness: Inject past audio into the loop
    void injectBlock(audio_block_t* block) {
        if (!memory || !block) return;
        
        // We assume state is already RECORDING or this is called inside a critical section
        // effectively simulating a 'tick' that happened in the past
        memory->writeSample(block);
        blockCounter++;
    }

    // For Late-Stop Forgiveness: Trim the tail of the loop
    void trim(size_t blocksToTrim) {
        if (blockCounter > blocksToTrim) {
            blockCounter -= blocksToTrim;
        } else {
            blockCounter = 0;
        }
    }

    void mute() { muted = true; }
    void unmute() { muted = false; }
    void toggleMute() { muted = !muted; }

    void pause() { paused = true; }
    void resume() { paused = false; }
    void togglePause() { paused = !paused; }

    void setGain(float g) { gain = g; }
    
    void clear() {
        memory->clearLoop();
        state = IDLE;
        paused = false;
        blockCounter = 0;
    }

    // --- Getters ---

    bool isPlaying() const { return state == PLAYBACK && !paused; }
    bool isRecording() const { return state == RECORDING; }
    bool isPaused() const { return paused; }
    bool isMuted() const { return muted; }
    size_t getLengthInBlocks() const { return blockCounter; }
    TrackState getState() const { return state; }

private:
    MemorySd* memory;
    volatile TrackState state;
    float gain;
    bool muted;
    bool paused;
    volatile uint32_t blockCounter;
    uint16_t fadeStep;
};

#endif // TRACK_H
