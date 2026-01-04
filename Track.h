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
        PLAYBACK
    };

    Track() {
        memory = nullptr;
        state = IDLE;
        gain = 1.0f;
        muted = false;
        paused = false;
        blockCounter = 0;
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
        
        // --- PLAYBACK ---
        else if (state == PLAYBACK) {
            if (paused) {
                // If paused, we output silence and do NOT advance the read head (do not call readSample)
                return; 
            }

            // Attempt to read from memory
            // We read directly into the output block
            bool hasData = memory->readSample(output);
            
            if (hasData) {
                // Determine effective gain (Mute overrides level to 0.0)
                float effectiveGain = muted ? 0.0f : gain;

                // Always process gain to support future micro-fading
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
                    int32_t val = (int32_t)(output->data[i] * effectiveGain);
                    
                    // Simple Hard Limiter
                    if (val > 32767) val = 32767;
                    else if (val < -32768) val = -32768;
                    
                    output->data[i] = (int16_t)val;
                }
            } else {
                // Buffer underrun or empty, output is already silenced by memset above
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
        } else if (state == IDLE && getLengthInBlocks() > 0) {
            if (memory) memory->restartPlayback();
            state = PLAYBACK;
        }
        paused = false;
    }

    void stop() {
        // Depending on design, stop might reset position or just go to IDLE
        state = IDLE;
        paused = false;
        memory->restartPlayback(); // Reset cursors
    }

    // --- Feature Control ---

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
};

#endif // TRACK_H