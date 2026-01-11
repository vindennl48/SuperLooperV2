#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Track.h"

class AudioLooper : public AudioStream {
public:
  enum State {
    NONE,
    RECORD,
    PLAY,
    STOP
  };

  AudioLooper(void) : AudioStream(1, inputQueueArray) {
    for (int i = 0; i < NUM_LOOPS; i++) {
      tracks[i] = new Track(&ram);
    }
    hardReset();
  }

  void begin() {
    ram.begin();
  }

  bool isWaiting() {
    return reqState != NONE;
  }

  bool isIdle() {
    return state == NONE;
  }

  bool isRecording() {
    return state == RECORD;
  }

  bool isPlaying() {
    return state == PLAY;
  }

  bool isMaxTracksReached() {
    return activeTrackIndex >= NUM_LOOPS - 1;
  }

  void updateSmartMute(float potValue) {
    // Track 0 (Base) is always unmuted
    tracks[0]->mute(false);

    int totalActiveTracks = activeTrackIndex + 1;
    // If only base track exists, nothing else to do
    if (totalActiveTracks <= 1) return;

    // Handle subsequent tracks
    for (int i = 1; i < totalActiveTracks; i++) {
      // Threshold increases with index. 
      // Example with 3 tracks (0, 1, 2):
      // Track 1 threshold: 1/3 = 0.33
      // Track 2 threshold: 2/3 = 0.66
      float threshold = (float)i / (float)totalActiveTracks;
      
      bool shouldMute = potValue <= threshold;
      tracks[i]->mute(shouldMute);
    }
  }

  void trigger() {
    LOG("AudioLooper::trigger() called. Current State: %d", state);
    switch (state) {
      case NONE:
        reqState = RECORD;
        LOG("AudioLooper::trigger() -> Requesting RECORD");
        break;
      
      case RECORD:
        reqState = PLAY;
        LOG("AudioLooper::trigger() -> Requesting PLAY");
        break;

      case PLAY:
        reqState = RECORD;
        LOG("AudioLooper::trigger() -> Requesting RECORD (New Layer)");
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

    updateState();

    // zero out the output block
    memset(outBlock->data, 0, sizeof(outBlock->data));

    // Check if base loop just finished recording to set global timeline
    if (activeTrackIndex == 0 && timeline == 0) {
      if (tracks[0]->getState() == Track::PLAY) {
        timeline = tracks[0]->getTimelineLength();
        LOG("AudioLooper -> Global Timeline Set: %d blocks", timeline);
      }
    }

    if (timeline > 0) {
      playhead++;
      if (playhead >= timeline) {
        playhead = 0;
        // LOG("AudioLooper -> Loop Wrap"); // Commented out to avoid spam, uncomment if needed
      }
    }

    for (size_t i = 0; i < NUM_LOOPS; i++) {
      tracks[i]->update(inBlock, outBlock);
    }

    transmit(outBlock, 0);
    release(outBlock);
    release(inBlock);
  }

  void reset() {
    for (int i = NUM_LOOPS-1; i >= 0; i--) {
      tracks[i]->stop();
      for (int j = 0; j < 100; j++) {
        if (tracks[i]->isStopped()) break;
        delay(10);
      }
      AudioNoInterrupts();
      tracks[i]->clear();
      AudioInterrupts();
    }

    hardReset();
  }

private:
  audio_block_t *inputQueueArray[1];
  Track* tracks[NUM_LOOPS];
  Ram ram;
  volatile State state, reqState;
  size_t playhead; // by blocks
  size_t timeline; // by blocks
  int activeTrackIndex;

  void hardReset() {
    LOG("AudioLooper::hardReset() called");
    AudioNoInterrupts();
    state = NONE;
    reqState = NONE;
    playhead = 0; // by blocks
    timeline = 0; // by blocks
    activeTrackIndex = 0;
    AudioInterrupts();
  }

  void updateState() {
    // Wait for the start of the global loop to sync state changes
    if (timeline > 0 && playhead != 0) return;

    switch (state) {
      case NONE:
        if (reqState == RECORD) {
          activeTrackIndex = 0;
          LOG("AudioLooper::updateState() -> Starting Recording on Track %d", activeTrackIndex);
          tracks[activeTrackIndex]->record();

          state = reqState;
          reqState = NONE;
        }
        break;
      
      case RECORD:
        if (reqState == PLAY) {
          LOG("AudioLooper::updateState() -> Stopping Recording, Starting Playback on Track %d", activeTrackIndex);
          tracks[activeTrackIndex]->play();

          state = reqState;
          reqState = NONE;
        }
        break;

      case PLAY:
        if (reqState == RECORD) {
          // 1. Prune muted tracks
          while (activeTrackIndex > 0 && tracks[activeTrackIndex]->getMuteState()) {
            LOG("AudioLooper::updateState() -> Pruning Muted Track %d", activeTrackIndex);
            tracks[activeTrackIndex]->forceClear();
            activeTrackIndex--;
          }

          // 2. Only transition if we have space (after pruning)
          if (activeTrackIndex < NUM_LOOPS - 1) {
            activeTrackIndex++;
            LOG("AudioLooper::updateState() -> Starting New Layer Recording on Track %d", activeTrackIndex);
            tracks[activeTrackIndex]->record();

            state = reqState;
          } else {
             LOG("AudioLooper::updateState() -> Max Tracks Reached (%d). Cannot Record New Layer.", activeTrackIndex);
          }

          reqState = NONE;
        }
        break;

      default:
        break;
    }
  }
};

#endif // AUDIO_LOOPER_H