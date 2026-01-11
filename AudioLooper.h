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
    switch (state) {
      case NONE:
        reqState = RECORD;
        break;
      
      case RECORD:
        reqState = PLAY;
        break;

      case PLAY:
        if (activeTrackIndex < NUM_LOOPS - 1) {
          reqState = RECORD;
        }
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
      }
    }

    if (timeline > 0) {
      playhead++;
      if (playhead >= timeline) playhead = 0;
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
      AudioNoInterrupt();
      tracks[i]->clear();
      AudioInterrupt();
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
    AudioNoInterrupt();
    state = NONE;
    reqState = NONE;
    playhead = 0; // by blocks
    timeline = 0; // by blocks
    activeTrackIndex = 0;
    AudioInterrupt();
  }

  void updateState() {
    // Wait for the start of the global loop to sync state changes
    if (timeline > 0 && playhead != 0) return;

    switch (state) {
      case NONE:
        if (reqState == RECORD) {
          activeTrackIndex = 0;
          tracks[activeTrackIndex]->record();

          state = reqState;
          reqState = NONE;
        }
        break;
      
      case RECORD:
        if (reqState == PLAY) {
          tracks[activeTrackIndex]->play();

          state = reqState;
          reqState = NONE;
        }
        break;

      case PLAY:
        if (reqState == RECORD) {
          activeTrackIndex++;
          tracks[activeTrackIndex]->record();

          state = reqState;
          reqState = NONE;
        }
        break;

      default:
        break;
    }
  }


#endif // AUDIO_LOOPER_H