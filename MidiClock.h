#ifndef MIDI_CLOCK_H
#define MIDI_CLOCK_H

#include <Arduino.h>

class MidiClock {
public:
  enum SyncState {
    IDLE,
    LEARNING,
    LOCKED
  };

  MidiClock() {
    reset();
    _beatsPerMeasure = 4; // Default
    _state = IDLE;
  }

  void handleClock() {
    unsigned long now = micros();
    
    // Check for timeout/reset condition (e.g. if we haven't received a clock in a long time)
    if (_lastTickMicros > 0 && (now - _lastTickMicros > 500000)) { // 500ms timeout
       // Note: We don't hard reset the sync state here, just the BPM calculation
       _sampleCount = 0;
    }
    _lastTickMicros = now;

    // Pulse counting logic
    _tickCounter++;
    if (_tickCounter >= 24) {
      _tickCounter = 0;
      _totalBeatCount++;
      _absBeatCount++;

      // Update current beat within measure
      _currentBeatOfMeasure++;
      if (_currentBeatOfMeasure > _beatsPerMeasure) {
        _currentBeatOfMeasure = 1;
        _totalMeasureCount++;
      }

      // Check for learning timeout (Missed beat)
      if (_state == LEARNING) {
        // If we have passed a full beat window since the last stomp
        if (_absBeatCount > _lastStompedBeat + 1) {
          _beatsPerMeasure = _lastStompedBeat;
          _state = LOCKED;
          // Recalculate measure count to be accurate to new time signature
          // But usually we just let it roll from here.
          _currentBeatOfMeasure = ((_absBeatCount - 1) % _beatsPerMeasure) + 1;
        }
      }
    }

    // BPM Calculation (Existing logic)
    _timestamps[_head] = now;
    _head = (_head + 1) % CLOCK_WINDOW;
    if (_sampleCount < CLOCK_WINDOW) {
      _sampleCount++;
    } else {
      unsigned long oldest = _timestamps[_head];
      unsigned long duration = now - oldest;
      if (duration > 0) {
        float instantBpm = 60000000.0f / duration;
        if (_bpm == 0) {
          _bpm = instantBpm;
        } else {
          _bpm = (_bpm * 0.9f) + (instantBpm * 0.1f);
        }
      }
    }
  }

  void triggerMeasureSync() {
    if (_state == IDLE || _state == LOCKED) {
      _state = LEARNING;
      _tickCounter = 0; // Align phase to the "One"
      _absBeatCount = 1;
      _lastStompedBeat = 1;
      _totalBeatCount++; // Trigger a beat event immediately
      _currentBeatOfMeasure = 1;
      _totalMeasureCount++;
    } 
    else if (_state == LEARNING) {
      if (_absBeatCount > _lastStompedBeat) {
        // Valid stomp in a new beat window
        _lastStompedBeat = _absBeatCount;
      }
    }
  }

  void resetSync() {
    _state = IDLE;
    _beatsPerMeasure = 4;
    _currentBeatOfMeasure = 1;
  }

  void handleStart() {
    _tickCounter = 23; // So next clock is Tick 0
    _currentBeatOfMeasure = 0; // So next beat is Beat 1
  }

  void handleContinue() {
     if (micros() - _lastTickMicros > 500000) {
       _sampleCount = 0;
     }
  }

  void handleStop() {
  }

  float getBpm() {
    if (micros() - _lastTickMicros > 1000000) {
      return 0.0f;
    }
    return _bpm;
  }

  uint32_t getTotalBeats() const { return _totalBeatCount; }
  uint32_t getTotalMeasures() const { return _totalMeasureCount; }
  int getCurrentBeat() const { return _currentBeatOfMeasure; }
  int getBeatsPerMeasure() const { return _beatsPerMeasure; }
  bool isLocked() const { return _state == LOCKED; }

  void reset() {
    _head = 0;
    _sampleCount = 0;
    _bpm = 0.0f;
    _lastTickMicros = 0;
    _tickCounter = 0;
    _absBeatCount = 0;
    _lastStompedBeat = 0;
    _totalBeatCount = 0;
    _totalMeasureCount = 0;
    _currentBeatOfMeasure = 1;
    memset(_timestamps, 0, sizeof(_timestamps));
  }

private:
  static const int CLOCK_WINDOW = 25; // Needs 25 samples to measure 24 intervals (1 Beat)
  unsigned long _timestamps[CLOCK_WINDOW];
  int _head;
  int _sampleCount;
  unsigned long _lastTickMicros;
  float _bpm;

  // Sync / Quantization logic
  SyncState _state;
  int _tickCounter;
  int _absBeatCount;
  int _lastStompedBeat;
  int _beatsPerMeasure;
  int _currentBeatOfMeasure;
  
  volatile uint32_t _totalBeatCount;
  volatile uint32_t _totalMeasureCount;
};

#endif // MIDI_CLOCK_H
