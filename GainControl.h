#ifndef GAINCONTROL_H
#define GAINCONTROL_H

#include <AudioStream.h>
#include "Definitions.h"

class GainControl {
public:
    GainControl() {
        // Initialize to full gain
        userGain = 1.0f;
        targetGain = 1.0f;
        startGain = 1.0f;
        currentGain = 1.0f;
        // Set blockCounter to Duration so isDone() returns true initially
        blockCounter = FADE_DURATION_BLOCKS; 
    }

    void setGain(float gain) {
        userGain = gain;
        // If we are currently audible (or fading to audible), update the live target.
        // If we are muted (target is 0), just updating userGain is enough; 
        // next unmute() will use the new value.
        if (!isMuted()) {
            startFadeTo(userGain);
        }
    }

    bool isDone() {
        return blockCounter >= FADE_DURATION_BLOCKS;
    }

    void fadeIn() { unmute(); }
    void unmute() {
        startFadeTo(userGain);
    }

    void fadeOut() { mute(); }
    void mute() {
        startFadeTo(0.0f);
    }

    void mute(bool willMute) {
      if (willMute) mute();
      else unmute();
    }

    void toggleMute() {
        if (isMuted()) {
            unmute();
        } else {
            mute();
        }
    }

    bool isMuted() {
        if (targetGain == 0.0f)
            return true;
        return false;
    }

    bool isMuteDone() {
      return isMuted() && isDone();
    }

    void hardReset(float gain) {
        userGain = gain;
        targetGain = gain;
        startGain = gain;
        currentGain = gain;
        blockCounter = FADE_DURATION_BLOCKS;
    }

    // This is expected to be called from the Audio Interrupt (update)
    float get(int sampleNum) {
        // If fade is complete, return target immediately
        if (isDone()) {
             return targetGain;
        }

        // Calculate progress
        // Total samples in the fade duration
        float totalSamples = (float)(FADE_DURATION_BLOCKS * AUDIO_BLOCK_SAMPLES);
        // Current sample position in the fade
        float currentSamplePos = (float)(blockCounter * AUDIO_BLOCK_SAMPLES + sampleNum);

        float t = currentSamplePos / totalSamples;
        if (t > 1.0f) t = 1.0f;

        // Linear interpolation
        currentGain = startGain + (targetGain - startGain) * t;
        return currentGain;
    }

    // Must be called once per block by the owner to advance fades
    void update() {
        if (blockCounter < FADE_DURATION_BLOCKS) {
            blockCounter++;
        }
    }

private:
    volatile float userGain;      // The "setting" (e.g. from a pot)
    volatile float targetGain;    // Where we are fading to (userGain or 0.0)
    volatile float startGain;     // Where we started the fade
    volatile float currentGain;   // Current calculated value
    volatile int blockCounter;    // How many blocks have passed in this fade

    void startFadeTo(float newTarget) {
        // Protect critical section: multiple variables updated that are read by ISR
        if (targetGain == newTarget && isDone()) {
            return; // Already there
        }

        startGain = currentGain; // Start from wherever we are right now
        targetGain = newTarget;
        blockCounter = 0;
    }
};

#endif // GAINCONTROL_H
