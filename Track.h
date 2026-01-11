#ifndef TRACK_H
#define TRACK_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Ram.h"
#include "GainControl.h"

class Track {
public:
  enum State {
    NONE,
    RECORD,
    PLAY,
    OVERDUB,
    STOP
  };

  Track(Ram* ram) : ram(ram)
  {
    hardReset();
  }
  ~Track() {}

  // Audio Interrupt Callback
  void update(audio_block_t* inBlock, audio_block_t* outBlock) {
    // --- Safety Checks ---
    // Output Block should already be zeroed coming in!!
    if (!inBlock || !outBlock) return;

    updateState();

    switch (state) {
      case RECORD: {
        size_t addrOffset = address + BLOCKS_TO_ADDR(timeline);

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          int16_t s_in = inBlock->data[i] * gc_record.get(i);
          ram->write16(addrOffset + i, s_in);
        }

        timeline++;
        break;
      }

      case OVERDUB:
      case PLAY: {
        size_t addrOffset = address + BLOCKS_TO_ADDR(playhead);
        size_t xfadeOffset = address + BLOCKS_TO_ADDR(timeline + playhead);
        
        bool recordXfade = xfadeBlockCount < FADE_DURATION_BLOCKS;
        bool processXfade = !recordXfade && playhead < FADE_DURATION_BLOCKS;

        if (playhead == 0) {
          gc_xfade.hardReset(1.0f);
          gc_xfade.fadeOut();
        }

        ram->read16(addrOffset, outBlock->data, AUDIO_BLOCK_SAMPLES);

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
          int32_t s_in = inBlock->data[i];
          int32_t s_out = outBlock->data[i];

          if (recordXfade) {
            ram->write16(xfadeOffset + i, s_in);
          }
          else if (processXfade) {
            int32_t s_xfade = ram->read16(xfadeOffset + i);
            s_out += s_xfade * gc_xfade.get(i);
          }

          if (state == OVERDUB) {
            s_in *= gc_record.get(i);
            s_in += s_out;
            s_in *= FEEDBACK_MULTIPLIER;
            ram->write16(addrOffset + i, (int16_t)s_in);
          }

          s_out *= gc_volume.get(i);
          s_out = SAMPLE_LIMITER(s_out);
          outBlock->data[i] = (int16_t)s_out;
        }

        if (recordXfade) xfadeBlockCount++;
        playhead++;
        if (playhead >= timeline) playhead = 0;
        break;
      }

      case STOP:
        playhead = 0;
        return;

      default:
        return;
    }
  }

  void record() { reqState = RECORD; }
  void play() { reqState = PLAY; }
  void overdub() { reqState = OVERDUB; }
  void stop() { reqState = STOP; }

  void trimLength(size_t n_actualBlockLength) {
    actualBlockLength = n_actualBlockLength;
    trim = true;
  }

  void mute(bool willMute) {
    muteState = willMute;
    gc_volume.mute(willMute);
  }

  void toggleMute() {
    if (muteState) muteState = false;
    else muteState = true;
    gc_volume.mute(muteState);
  }

  bool isMuted() {
    return muteState && gc_volume.isMuteDone();
  }

  bool isStopped() {
    return state == STOP;
  }

  bool isNone() {
    return state == NONE;
  }

  State getState() {
    return state;
  }

  void setVolume(float n_volume) {
    gc_volume.setGain(n_volume);
  }

  // NOTE: This function modifies shared static memory counters.
  // The user MUST include an AudioNoInterrupt wrapper around this function
  // (or the caller) to prevent race conditions with the audio update interrupt.
  void clear() {
    // STRICT LIFO CHECK
    // Only clear if this is the most recently allocated track
    if (allocationId != activeAllocationCount) return;

    if (!isStopped() && !isNone()) return;

    // Reclaim memory
    nextAvailableAddress = address;
    activeAllocationCount--;
    allocationId = 0;
    address = 0;

    hardReset();
  }

  // FORCE CLEAR: Bypasses state checks to immediately remove track.
  // MUST be called within AudioNoInterrupt() context.
  void forceClear() {
    state = NONE;
    clear();
  }

  bool getMuteState() {
    return muteState;
  }

  bool isXfadeComplete() {
    return xfadeBlockCount >= FADE_DURATION_BLOCKS;
  }

  size_t getTimelineLength() { return timeline; }

private:
  static inline size_t nextAvailableAddress = 1; // should be 1, leave 0 empty
  static inline bool lock_nextAvailableAddress = false;
  static inline int activeAllocationCount = 0;

  Ram* ram;
  int allocationId;
  volatile State state, nextState, reqState;
  GainControl gc_volume, gc_record, gc_xfade;

  size_t address; // start pos in ram
  size_t playhead;  // pos on timeline in audio blocks
  size_t timeline;  // length of playable loop in audio blocks
  uint16_t xfadeBlockCount;  // block pos for crossfade samples
  size_t actualBlockLength;
  volatile bool trim;
  volatile bool muteState;

  void hardReset() {
    // allocationId = 0; // only reset from clear()

    state = NONE;
    nextState = NONE;
    reqState = NONE;

    gc_volume.hardReset(1.0f);
    gc_record.hardReset(0.0f);
    gc_xfade.hardReset(1.0f);

    // address = 0; // only reset from clear()
    playhead = 0;
    timeline = 0;
    xfadeBlockCount = 0;
    actualBlockLength = 0;
    trim = false;
    muteState = false;
  }

  bool isRamOutOfBounds(uint32_t extraBlocks) {
      size_t end_pos_words = address + BLOCKS_TO_ADDR(timeline + extraBlocks);
      return end_pos_words >= TOTAL_SRAM_SAMPLES;
  }

  void updateState() {
    switch (state) {
      case NONE:
        if (reqState == RECORD) {
          hardReset();

          if (!address) {
            if (lock_nextAvailableAddress) return;

            address = nextAvailableAddress;
            lock_nextAvailableAddress = true;
            
            // Assign allocation ID
            activeAllocationCount++;
            allocationId = activeAllocationCount;
          }

          gc_record.fadeIn();

          state = RECORD;
          nextState = NONE;
          reqState = NONE;
        }
        break;

      case RECORD:
        if (isRamOutOfBounds(1)) reqState = PLAY; // RAM Bounds Check

        if (reqState == PLAY) {
          gc_record.hardReset(0.0f);
          xfadeBlockCount = 0;

          if (trim && actualBlockLength > timeline) {
            xfadeBlockCount = actualBlockLength - timeline;
            timeline = actualBlockLength;
          }

          nextAvailableAddress += BLOCKS_TO_ADDR(timeline + FADE_DURATION_BLOCKS);
          lock_nextAvailableAddress = false;

          state = reqState;
          nextState = NONE;
          reqState = NONE;
        }
        break;

      case PLAY:
        if (reqState == OVERDUB) {
          gc_record.fadeIn();

          state = reqState;
          nextState = NONE;
          reqState = NONE;
        }

        if (reqState == STOP) {
          gc_volume.mute();

          nextState = reqState;
          reqState = NONE;
        }
        if (nextState == STOP && gc_volume.isDone()) {
          state = nextState;
          nextState = NONE;
          reqState = NONE;
        }
        break;

      case OVERDUB:
        if (reqState == PLAY) {
          gc_record.fadeOut();

          nextState = reqState;
          reqState = NONE;
        }
        if (nextState == PLAY && gc_record.isDone()) {
          state = nextState;
          nextState = NONE;
          reqState = NONE;
        }
        break;

      case STOP:
        if (reqState == PLAY) {
          if (!muteState) gc_volume.unmute();

          state = reqState;
          nextState = NONE;
          reqState = NONE;
        }
        break;

      default:
        state = NONE;
        nextState = NONE;
        reqState = NONE;
        break;
    }
  }
};

#endif