#ifndef TRACK_H
#define TRACK_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Ram.h"

class Track {
public:
  enum State {
    NORMAL,
    MUTE,
    RESUME,
    STOP
  };

  Track(Ram* ram) : ram(ram)
  {
    hardReset();
  }
  ~Track() {}

  // Run in audio interrupt loop
  void update(audio_block_t* inBlock, audio_block_t* outBlock) {
    // Always ensure output is silent initially
    if (outBlock) memset(outBlock->data, 0, sizeof(outBlock->data));
    else return;

    if (!address) return; // if we dont have RAM addr, we have nothing to play!

    if (state == MUTE) mute(true); else mute(false);
    if (state == STOP) {
      mute(true);
      if (isMuted()) return;
    }
    if (state == RESUME) {
      // mute(false);
      gain = 1.0f; gainTarget = 1.0f; // fadeTarget will take care of micro-fade
      state = NORMAL;
      playhead = 0;
      isOverdub = false;
    }

    size_t addrOffset = playhead * AUDIO_BLOCK_SAMPLES * 2;

    if (isTimelineLocked) { // PLAYBACK
      ram->read16(address + addrOffset, outBlock->data, AUDIO_BLOCK_SAMPLES);

      if (isOverdub) processOverdub(inBlock, outBlock, addrOffset); // OVERDUB
      setFadeTarget();
      processOutput(outBlock);

      // playhead advance && micro-fades
      playhead++; if (playhead >= timeline) playhead = 0;
    }
    else { // RECORD
      ram->write16(address + addrOffset, inBlock->data, AUDIO_BLOCK_SAMPLES);
      timeline++;
    }
  }

  void mute(bool willMute) {
    setGain(willMute ? 0.0f : 1.0f);
  }

  bool isMuted() {
    if (gain == 0.0f) return true;
    return false;
  }

  /*
   * newAddress: can be 0 if we are overdubbing
   * */
  void startRecording(size_t newAddress) {
    if (address) {  // already have data
      isOverdub = true;
      return;
    }
    if (!newAddress) return;  // cant overdub with 0 address

    // if we got this far, we have no data so safe to hardReset
    hardReset();
    address = newAddress; // setting mem address will trigger record
  }

  void stopRecording() {
    if (!address) return; // nothing to do!

    if (isOverdub) {
      isOverdub = false;
      return;
    }

    isTimelineLocked = true;
  }

  void stop() {
    if (!isTimelineLocked) return;
    state = STOP;
  }

  bool isStopped() {
    if (state == STOP && isMuted()) return true;
    return false;
  }

  void resume() {
    if (!isTimelineLocked) return;
    state = RESUME;
  }

private:
  // Amount to increment or decrement fade by
  static const float fadeStep = 1.0f / (FADE_DURATION_BLOCKS * AUDIO_BLOCK_SAMPLES);

  Ram* ram;
  volatile State state;
  volatile size_t address; // start pos in ram
  float gain;
  volatile float gainTarget;
  float fadeGain;
  float fadeTarget;
  uint32_t timeline;  // length of track in audio blocks
  uint32_t playhead;  // pos on timeline in audio blocks
  volatile uint32_t startPos;  // loop start pos on global timeline
  volatile bool isTimelineLocked;  // have we recorded a full loop
  volatile bool isOverdub;

  void hardReset() {
    AudioNoInterrupts();

    address = 0; // must be 1 or greater to signfiy we have data
    state = NORMAL; // normal operation
    gain = 1.0f;
    gainTarget = 1.0f;
    fadeGain = 0.0f;
    fadeTarget = 0.0f;
    timeline = 0;
    playhead = 0;
    startPos = 0;
    isTimelineLocked = false;
    isOverdub = false;

    AudioInterrupts();
  }

  void setGain(float newGain) {
    gainTarget = newGain;
  }

  void processOutput(audio_block_t* block) {
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t sample = block->data[i];
      processFade(&fadeGain, &fadeTarget);
      processFade(&gain, &gainTarget);
      sample *= gain;                   // appply gain
      sample *= fadeGain;               // appply fade
      sample = SAMPLE_LIMITER(sample);  // hard limiter
      block->data[i] = (int16_t)sample; // send sample
    }
  }

  void processOverdub(audio_block_t* inBlock, audio_block_t* outBlock, size_t addrOffset) {
    if (!inBlock) return;

    int16_t mixBuffer[AUDIO_BLOCK_SAMPLES];

    for (int i=0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t sum = outBlock->data[i] + inBlock->data[i];
      mixBuffer[i] = SAMPLE_LIMITER(sum);
    }

    ram->write16(address + addrOffset, mixBuffer, AUDIO_BLOCK_SAMPLES);
  }

  void processFade(float* gain, volatile float* target) {
    // if we are within a reasonable margin, lets call it even
    if (*gain < *target + 0.001f && *gain > *target - 0.001f) *gain = *target;

    if (*gain == *target) return;
    if (*target > *gain) {
      *gain += fadeStep;
      if (*gain > 0.99f) *gain = 1.0f;
    }
    else {
      *gain -= fadeStep;
      if (*gain < 0.01f) *gain = 0.0f;
    }
  }

  void setFadeTarget() {
    // only during playback
    if (playhead >= timeline - FADE_DURATION_BLOCKS) {
      fadeTarget = 0.0f; // end of loop
    }
    else if (playhead == 0) fadeGain = 0.0f; // guarantee 0.0f at playhead start
    else fadeTarget = 1.0f; // start of loop
  }
};

#endif
