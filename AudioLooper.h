#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Track.h"

class AudioLooper : public AudioStream {
public:
  AudioLooper(void) : AudioStream(1, inputQueueArray) {
    for (int i = 0; i < NUM_LOOPS; i++) {
      tracks[i] = new Track(&ram);
    }
    hardReset();
  }

  void begin() {
    ram.begin();
  }

  void trigger() { // when footswitch is pressed
    if (isStopped) return;

    Track& track = tracks[activeTrack];

    switch (track.state) {
      case Track::NONE: // NONE => RECORDING
        if (isArmed) return;

        if (quantize && playhead % quantize != 0) {
          isArmed = true;
          return;
        }

        track.startRecording();

        if (!timeline) playhead = 0;
        break;

      case Track::RECORDING: // RECORDING => PLAYBACK
        if (isArmed) return;

        if (quantize && playhead % quantize != 0) {
          isArmed = true;
          return;
        }

        track.stopRecording();

        if (!timeline) {
          timeline = playhead;
          quantize = playhead;
          playhead = 0;
        }
        else {
          size_t n_timeline = track.getTimelineLength();
          if (n_timeline > timeline) timeline = n_timeline;
        }

        if (activeTrack + 1 < NUM_LOOPS) activeTrack++;
        break;

      case Track::PLAYBACK: // PLAYBACK => OVERDUB
        break;

      default:
        break;
    }
  }

  virtual void update(void) {
    audio_block_t *inBlock = receiveReadOnly(0);
    audio_block_t *outBlock = allocate();

    if (!outBlock) {
      if (inBlock) release(inBlock);
      return;
    }
    if (!inBlock) return;

    if (isArmed && playhead % quantize == 0) {
      isArmed = false;
      trigger();
    }

    // zero out the output block
    memset(outBlock->data, 0, sizeof(outBlock->data));

    for (size_t i = 0; i < NUM_LOOPS; i++) {
      Track& track = tracks[i];
      track.update(inBlock, outBlock);
    }

    if (!isStopped) {
      playhead++;
      if (timeline && playhead >= timeline) playhead = 0;
    }

    transmit(outBlock, 0);
    release(outBlock);
    release(inBlock);
  }

  void reset() {
    for (int i = NUM_LOOPS-1; i >= 0; i--) {
      Track& track = tracks[i];
      track.stop(true);
      for (int j = 0; j < 100; j++) {
        if (track.isStopped()) break;
        delay(10);
      }
      track.clear();
    }

    hardReset();
  }

  void stop() {
    for (int i = NUM_LOOPS-1; i >= 0; i--) {
      Track& track = tracks[i];
      track.stop(true);
      for (int j = 0; j < 100; j++) {
        if (track.isStopped()) break;
        delay(10);
      }
    }

    isStopped = true;
  }

  void play() {
    if (!isStopped) return;

    for (int i = NUM_LOOPS-1; i >= 0; i--) {
      Track& track = tracks[i];
      track.stop(false);
    }

    playhead = 0;
    isStopped = false;
  }

private:
  audio_block_t *inputQueueArray[1];
  Track* tracks[NUM_LOOPS];
  Ram ram;

  size_t playhead; // by blocks
  size_t timeline; // by blocks
  size_t quantize; // by blocks
  uint8_t activeTrack;
  uint8_t activeTrackCount;
  bool isArmed;
  bool isStopped;

  void hardReset() {
    playhead = 0; // by blocks
    timeline = 0; // by blocks
    quantize = 0; // by blocks
    currentTrack = 0;
    activeTrackCount = 0;
    isArmed = false; // for delayed record start/stop
    isStopped = false;
  }

  void updateState() {
    // a
    switch (state) {
      case NONE:
        break;

      default:
        break;
    }
  }
};

#endif // AUDIO_LOOPER_H


// --- IGNORE BELOW THIS LINE ---
  // // Call this from the main loop() function frequently
  // void poll() {
  //   for (int i = 0; i < NUM_LOOPS; i++) {
  //     if (tracks[i]) tracks[i]->poll();
  //   }
  // }

  // // New function to handle Potentiometer Muting Logic
  // void updateMuteState(float potVal) {
  //   // Only allow muting changes if we are not currently recording or waiting to record
  //   // We also don't mute if we have 0 or 1 track (Track 0 is always unmuted)
  //   if (state == RECORDING || state == WAITING_TO_RECORD || state == WAITING_TO_FINISH || activeTrackCount <= 1) {
  //     return;
  //   }
  //
  //   // Determine the split point (first muted track index) based on Pot Value
  //   // Range: 1 to activeTrackCount
  //   // (Track 0 is always unmuted, so minimum cutoff is 1)
  //   int cutoff = (int)(potVal * activeTrackCount) + 1;
  //   if (cutoff > activeTrackCount) cutoff = activeTrackCount;
  //
  //   // Apply mute state to all active tracks
  //   for (int i = 0; i < activeTrackCount; i++) {
  //     if (i < cutoff) {
  //       tracks[i]->unmute();
  //     } else {
  //       tracks[i]->mute();
  //     }
  //   }
  //
  //   // Update pointer to the cut-off point
  //   // If we are appending, this will be activeTrackCount.
  //   // If we are branching (muted), this will be the index of the first muted track.
  //   currentTrackIndex = cutoff;
  //
  //   // Safety clamp for array access (e.g. if activeTrackCount == NUM_LOOPS)
  //   if (currentTrackIndex >= NUM_LOOPS) {
  //     currentTrackIndex = NUM_LOOPS - 1;
  //   }
  //
  //   // Logic Check: If we are pointing at an existing track (Branching), 
  //   // ensure we are in PLAYBACK mode to allow re-recording.
  //   // (If we were in FULL_PLAYBACK, we must transition to allow trigger() to record)
  //   if (currentTrackIndex < activeTrackCount && state == FULL_PLAYBACK) {
  //     state = PLAYBACK;
  //   }
  // }
