#ifndef TRACK_MANAGER_H
#define TRACK_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include "BALibrary.h"
#include "Definitions.h"

using namespace BALibrary;

struct Track {
  ExtMemSlot sramSlot;   // Allocated SRAM slot object
  File file;              // SD card file handle
  
  enum State {
    EMPTY,
    RECORDING,
    FINISHING_RECORD,
    PLAYING,
    STOPPED,
    MUTED,
    FADING_IN,
    FADING_OUT,
    PRIMING
  };
  volatile State state;
  volatile State nextState;

  // Fade & Transport State
  float currentGain;
  float fadeStep;
  uint32_t fadeSamplesRemaining;
  bool primingResetNeeded;

  // RAM Ring Buffer Pointers (Playback / Instant Replay)
  volatile uint32_t ramReadHead;  // Audio thread reads from here
  volatile uint32_t ramWriteHead; // SD loader (or live input) writes to here
  bool ringBufferFull;            // Flag for initial recording

  // SD Card Management
  uint32_t sdReadPosition;        // Current read offset in the file
  uint32_t loopLengthSamples;     // Valid length of the recorded loop (in samples)

  // Constructor
  Track() : state(EMPTY), 
            currentGain(0.0f), fadeStep(0.0f), fadeSamplesRemaining(0), nextState(STOPPED), primingResetNeeded(false),
            ramReadHead(0), ramWriteHead(0), ringBufferFull(false), 
            sdReadPosition(0), loopLengthSamples(0) {}
};

class TrackManager {
public:
  TrackManager();
  
  // Initializes SD card and allocates memory
  bool init();

  // Main Loop Logic
  void update(); // Handle SD Card IO

  // State Management
  void startRecording(int trackIndex);
  void stopRecording(int trackIndex);
  
  void playTrack(int trackIndex);
  void stopTrack(int trackIndex);
  void muteTrack(int trackIndex);
  void unmuteTrack(int trackIndex);
  void clearTrack(int trackIndex);

  bool isTrackEmpty(int trackIndex);
  uint32_t getTrackLoopLength(int trackIndex);
  void eraseTrack(int trackIndex);

  // Audio Interrupt Methods (Fast Path)
  void pushToRecord(int trackIndex, int16_t sample);
  int16_t pullForPlayback(int trackIndex);

  // Getters
  Track* getTrack(int index);
  Track* getWriteBuffer();

private:
  ExternalSramManager m_sramManager;
  Track m_tracks[NUM_AUDIO_TRACKS];
  
  // Dedicated Write Buffer (SRAM Only)
  Track m_writeBuffer; 
  volatile uint32_t writeBufferWriteHead; // Input from ADC
  volatile uint32_t writeBufferReadHead;  // Output to SD

  // instant and dangerous
  void eraseTrack(int trackIndex);
};

#endif
