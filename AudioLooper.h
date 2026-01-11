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
      tracks[i]->clear();
    }

    hardReset();
  }

private:
  audio_block_t *inputQueueArray[1];
  Track* tracks[NUM_LOOPS];
  Ram ram;

  size_t playhead; // by blocks
  size_t timeline; // by blocks
  
  volatile State state;
  volatile State reqState;
  int activeTrackIndex;

  void hardReset() {
    playhead = 0; // by blocks
    timeline = 0; // by blocks
    state = NONE;
    reqState = NONE;
    activeTrackIndex = 0;
  }

  void updateState() {
    switch (state) {
      case NONE:
        if (reqState == RECORD) {
          state = RECORD;
          activeTrackIndex = 0;
          tracks[activeTrackIndex]->record();
          reqState = NONE;
        }
        break;
      
      case RECORD:
        if (reqState == PLAY) {
          state = PLAY;
          tracks[activeTrackIndex]->play();
          reqState = NONE;
        }
        break;

      case PLAY:
        if (reqState == RECORD) {
          state = RECORD;
          activeTrackIndex++;
          tracks[activeTrackIndex]->record();
          reqState = NONE;
        }
        break;

      default:
        break;
    }
  }


#endif // AUDIO_LOOPER_H