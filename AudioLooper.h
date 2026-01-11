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
  }

  virtual void update(void) {
    audio_block_t *inBlock = receiveReadOnly(0);
    audio_block_t *outBlock = allocate();

    if (!outBlock) {
      if (inBlock) release(inBlock);
      return;
    }
    if (!inBlock) return;

    // zero out the output block
    memset(outBlock->data, 0, sizeof(outBlock->data));

    for (size_t i = 0; i < NUM_LOOPS; i++) {
      Track& track = tracks[i];
      track.update(inBlock, outBlock);
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

private:
  audio_block_t *inputQueueArray[1];
  Track* tracks[NUM_LOOPS];
  Ram ram;

  size_t playhead; // by blocks
  size_t timeline; // by blocks

  void hardReset() {
    playhead = 0; // by blocks
    timeline = 0; // by blocks
  }

  void updateState() {
    switch (state) {
      case NONE:
        break;

      default:
        break;
    }
  }
};

#endif // AUDIO_LOOPER_H