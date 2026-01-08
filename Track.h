#ifndef TRACK_H
#define TRACK_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Ram.h"

class Track {
public:
  Track(Ram* ram) : ram(ram)
  {
    hardReset();
  }
  ~Track() {}

  void trigger() {
    if (!address) return;

    if (!init) { // START RECORDING
      hardReset();
      recGainTarget = 1.0f;
      init = true;
      return;
    }

    if (!isTimelineLocked) { // STOP RECORDING
      // since we are modifying the timeline var, we need to prevent the
      // audio interrupt loop from taking over during this process.
      AudioNoInterrupts();
      isTimelineLocked = true;
      recGainTarget = 0.0f;
      if (timeline > 0) timeline--; // we are always 1 emtpy block ahead during recording
      AudioInterrupts();
    }
    else {  // Toggle OVERDUB
      // toggle recGainTarget for overdub
      recGainTarget = recGainTarget > 0.0f ? 0.0f : 1.0f;
      return;
    }

  }

  void update(audio_block_t* inBlock, audio_block_t* outBlock) {
    // --- Safety Checks ---
    // Output Block should already be zeroed coming in!!
    if (!inBlock || !outBlock || !address) return; // if RAM addr = 0, we have nothing to play!
    if (playhead > timeline) playhead = 0;

    if (stopped && !muteGain) { // stopped and micro-fade complete
      playhead = 0;
      return;
    }

    size_t addrOffset = playhead * AUDIO_BLOCK_SAMPLES * 2;
    if (isTimelineLocked)
      ram->read16(address + addrOffset, outBlock->data, AUDIO_BLOCK_SAMPLES);

    processBlock(inBlock, outBlock);

    if (recGain) {
      ram->write16(address + addrOffset, inBlock->data, AUDIO_BLOCK_SAMPLES);
      if (!isTimelineLocked) timeline++;
    }
    playhead++;
  }

  void setMemAddress(size_t n_address) {
    address = n_address;
  }

  void mute(bool willMute) {
    muteGainTarget = willMute ? 0.0f : 1.0f;
    muted = willMute;
  }

  bool isMuted() {
    return muted;
  }

  void stop(bool willStop) {
    stopped = willStop;
    muteGainTarget = willStop || muted ? 0.0f : 1.0f;
  }

  bool isStopped() {
    return stopped;
  }

  void setGain(float newGain) {
    gainTarget = newGain;
  }

  void clear() {
    if (!stopped || muteGain) return;
    hardReset();
  }

private:
  // Amount to increment or decrement fade by
  static const float fadeStep = 1.0f / (FADE_DURATION_BLOCKS * AUDIO_BLOCK_SAMPLES);

  Ram* ram;
  volatile size_t address; // start pos in ram
  float gain, muteGain, recGain;
  volatile float gainTarget, muteGainTarget, recGainTarget;
  volatile uint32_t timeline;  // length of track in audio blocks
  uint32_t playhead;  // pos on timeline in audio blocks
  volatile uint32_t startPos;  // loop start pos on global timeline
  volatile bool isTimelineLocked;  // have we recorded a full loop
  volatile bool stopped;
  volatile bool muted;
  bool init;

  void hardReset() {
    AudioNoInterrupts();

    address = 0; // must be 1 or greater to signfiy we have data
    gain = 1.0f;
    muteGain = 0.0f;
    recGain = 0.0f;
    gainTarget = 1.0f;
    muteGainTarget = 0.0f;
    recGainTarget = 0.0f;
    timeline = 0;
    playhead = 0;
    startPos = 0;
    isTimelineLocked = false;
    stopped = false;
    muted = false;
    init = false;

    AudioInterrupts();
  }

  void processBlock(audio_block_t* inBlock, audio_block_t* outBlock) {
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t s_in = inBlock->data[i];
      int32_t s_out = outBlock->data[i];

      processFade(&gain, &gainTarget);
      processFade(&recGain, &recGainTarget);
      processFade(&muteGain, &muteGainTarget);

      if (recGain) {
        s_in *= recGain;
        s_in += s_out;
        s_in = SAMPLE_LIMITER(s_in);  // hard limiter
        inBlock->data[i] = (int16_t)s_in;
      }

      s_out *= gain * muteGain;
      s_out = SAMPLE_LIMITER(s_out);  // hard limiter
      outBlock->data[i] = (int16_t)s_out;
    }
  }

  void processFade(float* gain, volatile float* target) {
    if (*gain == *target) return;

    // if we are within a reasonable margin, lets call it even
    if (*gain < *target + 0.001f && *gain > *target - 0.001f) {
      *gain = *target;
      return;
    }

    if (*target > *gain) {
      *gain += fadeStep;
      if (*gain > 0.99f) *gain = 1.0f;
    }
    else {
      *gain -= fadeStep;
      if (*gain < 0.01f) *gain = 0.0f;
    }
  }
};

#endif
