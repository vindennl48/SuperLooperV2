#ifndef TRACK_H
#define TRACK_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Ram.h"
#include "GainControl.h"

class Track {
public:
  Track(Ram* ram) : ram(ram)
  {
    hardReset();
  }
  ~Track() {}

  // Audio Interrupt Callback
  void update(audio_block_t* inBlock, audio_block_t* outBlock) {
    // --- Safety Checks ---
    // Output Block should already be zeroed coming in!!
    if (!inBlock || !outBlock || !address) return; // if RAM addr = 0, we have nothing to play!
    // --- RAM Bounds Check ---
    if (!isTimelineLocked && isRamOutOfBounds(1)) stopRecording();

    if (isTimelineLocked && playhead >= timeline) {
      playhead = 0;
      gc_xfade.hardReset(1.0f);
      gc_xfade.mute();
    }

    if (stopped && gc_volume.isDone()) { // stopped and micro-fade complete
      playhead = 0;
      return;
    }

    /*
     * Tasks for the update loop:
     *   - record fade in
     *   - record full loop
     *   - playback loop while recording xfade
     *   - mix in xfade to front of loop
     *   - continue playback
     *   - overdub & stop & start track
     *   - update track volume
     * */

    int16_t tmp_inBlock[AUDIO_BLOCK_SAMPLES];
    int16_t tmp_xfadeBlock[AUDIO_BLOCK_SAMPLES];
    size_t addrOffset, xfadeOffset;

    if (isTimelineLocked) {
      addrOffset = BLOCKS_TO_ADDR(playhead);

      // --- Processing CROSS FADE ---
      // once xfadeRecord reaches FADE_DURATION_BLOCKS, we are done
      // recording xfade
      if (xfadeBlockCount < FADE_DURATION_BLOCKS) {
        if (!isRamOutOfBounds(xfadeBlockCount + 1)) {
           xfadeOffset = BLOCKS_TO_ADDR(timeline + xfadeBlockCount);
           ram->write16(address + xfadeOffset, inBlock->data, AUDIO_BLOCK_SAMPLES);
        }
        xfadeBlockCount++;
      }
      else if (playhead < FADE_DURATION_BLOCKS) {
        xfadeOffset = BLOCKS_TO_ADDR(timeline + playhead);
        ram->read16(address + xfadeOffset, tmp_xfadeBlock, AUDIO_BLOCK_SAMPLES);
      }
      // -----------------------------

      // load playback block from ram
      ram->read16(address + addrOffset, outBlock->data, AUDIO_BLOCK_SAMPLES);
    }
    else {
      addrOffset = BLOCKS_TO_ADDR(timeline);
    }

    // --- Individual Sample Processing ---
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
      int32_t s_in = inBlock->data[i];
      int32_t s_out = outBlock->data[i];

      // --- Processing IN Block ---
      if (isRecordActive()) {
        s_in *= gc_record.get(i); // fade in/out gain value

        if (isTimelineLocked) { // if we have a base-loop
          s_in += (int32_t)(s_out * FEEDBACK_MULTIPLIER); // mix in base-loop with decay
          s_in = SAMPLE_LIMITER(s_in);  // hard limiter
        }

        tmp_inBlock[i] = (int16_t)s_in;
      }
      // ---------------------------

      // --- Processing OUT Block ---
      if (isXfadeComplete() && playhead < FADE_DURATION_BLOCKS) {
        s_out += (int32_t)tmp_xfadeBlock[i] * gc_xfade.get(i);
      }
      s_out *= gc_volume.get(i);
      s_out = SAMPLE_LIMITER(s_out);  // hard limiter
      outBlock->data[i] = (int16_t)s_out;
      // ----------------------------
    }
    // ------------------------------------

    if (isRecordActive()) {
      // this takes care of recording the base-loop as well as any
      // overdub loops
      ram->write16(address + addrOffset, tmp_inBlock, AUDIO_BLOCK_SAMPLES);
    }

    if (!isTimelineLocked) timeline++;
    else playhead++;
  }

  void startRecording() {
    AudioNoInterrupts();
    if (!address) {
      if (lock_nextAvailableAddress) {
        AudioInterrupts();
        return;
      }
      address = nextAvailableAddress;
      lock_nextAvailableAddress = true;
      
      // Assign allocation ID
      activeAllocationCount++;
      allocationId = activeAllocationCount;
    }
    AudioInterrupts();

    if (!init) { // START RECORDING
      AudioNoInterrupts();
      hardReset();
      gc_record.unmute();
      init = true;
      AudioInterrupts();
      return;
    }

    if (isTimelineLocked && isXfadeComplete && gc_record.isDone()) {
      gc_record.unmute(); // start overdub
    }
  }

  /*
   * If actualBlockLength is used, we trim the timeline to this length
   * */
  void stopRecording() { stopRecording(0); }
  void stopRecording(size_t actualBlockLength) {
    if (!address || !init) return;

    if (!isTimelineLocked) {
      // STOP RECORDING
      AudioNoInterrupts();
      isTimelineLocked = true;
      gc_record.hardReset(0.0f);
      playhead = 0;
      if (actualBlockLength && actualBlockLength > timeline) {
        // fill up xfade buffer if we've already recorded it
        xfadeBlockCount = actualBlockLength - timeline;
        timeline = actualBlockLength;
      }
      nextAvailableAddress += BLOCKS_TO_ADDR(timeline + FADE_DURATION_BLOCKS);
      lock_nextAvailableAddress = false;
      AudioInterrupts();
    }
    else if (isTimelineLocked && isXfadeComplete && gc_record.isDone()) {
      gc_record.mute(); // stop overdub
    }
  }

  void mute(bool willMute) {
    muted = willMute;
    gc_volume.mute(muted);
  }

  bool isMuted() {
    return muted;
  }

  void stop(bool willStop) {
    stopped = willStop;
    gc_volume.mute(willStop || muted);
  }

  bool isStopped() {
    return stopped;
  }

  void setVolume(float n_volume) {
    gc_volume.setGain(n_volume);
  }

  void clear() {
    if (!stopped || !gc_volume.isDone()) return;
    
    // STRICT LIFO CHECK
    // Only clear if this is the most recently allocated track
    if (allocationId != activeAllocationCount) return;

    AudioNoInterrupts();
    // Reclaim memory
    nextAvailableAddress = address;
    activeAllocationCount--;
    allocationId = 0;
    AudioInterrupts();

    hardReset();
  }

  bool isXfadeComplete() {
    return xfadeBlockCount >= FADE_DURATION_BLOCKS;
  }

  size_t getMemorySize() {
    return BLOCKS_TO_ADDR(timeline + FADE_DURATION_BLOCKS);
  }

private:
  Ram* ram;
  GainControl gc_volume, gc_record, gc_xfade;
  volatile size_t address; // start pos in ram
  volatile uint32_t playhead;  // pos on timeline in audio blocks
  volatile uint32_t timeline;  // length of playable loop in audio blocks
  volatile uint32_t xfadeBlockCount;  // block pos for crossfade samples
  volatile bool isTimelineLocked;  // have we recorded a full loop
  volatile bool stopped;
  volatile bool muted;
  volatile bool init;
  int allocationId;
  
  void hardReset() {
    AudioNoInterrupts();

    gc_volume.hardReset(1.0f);
    gc_record.hardReset(0.0f);
    gc_xfade.hardReset(1.0f);
    address = 0; // must be 1 or greater to signfiy we have data
    playhead = 0;
    timeline = 0;
    xfadeBlockCount = 0;
    isTimelineLocked = false;
    stopped = false;
    muted = false;
    init = false;
    
    AudioInterrupts();
  }

  bool isRamOutOfBounds(uint32_t extraBlocks) {
      size_t end_pos_samples = address + (timeline + extraBlocks) * AUDIO_BLOCK_SAMPLES;
      return end_pos_samples >= TOTAL_SRAM_SAMPLES;
  }

  bool isRecordActive() { return !gc_record.isMuted() || !gc_record.isDone(); }
  
  static inline size_t nextAvailableAddress = 1;
  static inline bool lock_nextAvailableAddress = false;
  static inline int activeAllocationCount = 0;
};

#endif
