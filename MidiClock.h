#ifndef MIDI_CLOCK_H
#define MIDI_CLOCK_H

#include <Arduino.h>

class MidiClock {
public:
  MidiClock() {
    reset();
  }

  void handleClock() {
    unsigned long now = micros();
    
    // Check for timeout/reset condition (e.g. if we haven't received a clock in a long time)
    if (_lastTickMicros > 0 && (now - _lastTickMicros > 500000)) { // 500ms timeout
       reset();
    }
    _lastTickMicros = now;

    // Store timestamp in circular buffer
    _timestamps[_head] = now;
    
    // Move head
    _head = (_head + 1) % CLOCK_WINDOW;
    
    if (_sampleCount < CLOCK_WINDOW) {
      _sampleCount++;
    } else {
      // We have a full window (24 ticks = 1 quarter note)
      // Calculate duration of the last beat
      // The oldest sample is at _head (since we just wrote to _head-1 and incremented)
      unsigned long oldest = _timestamps[_head];
      unsigned long duration = now - oldest;
      
      if (duration > 0) {
        // BPM = 60 seconds / duration of 1 beat (in seconds)
        // BPM = 60,000,000 micros / duration micros
        float instantBpm = 60000000.0f / duration;
        
        // Apply slight smoothing to the BPM value itself to reduce display jitter
        if (_bpm == 0) {
          _bpm = instantBpm;
        } else {
          _bpm = (_bpm * 0.9f) + (instantBpm * 0.1f);
        }
      }
    }
  }

  void handleStart() {
    reset();
  }

  void handleContinue() {
    // Usually we don't reset on continue, just pick up, 
    // but we might want to reset the smoothing if the gap was long.
    // For now, treat like Play (don't hard reset if recently clocked, else reset)
     if (micros() - _lastTickMicros > 500000) {
       reset();
     }
  }

  void handleStop() {
    // Optional
  }

  float getBpm() {
    if (micros() - _lastTickMicros > 1000000) { // 1 second timeout
      return 0.0f;
    }
    return _bpm;
  }

  void reset() {
    _head = 0;
    _sampleCount = 0;
    _bpm = 0.0f;
    _lastTickMicros = 0;
    // Clear buffer not strictly necessary as we use _sampleCount, but good for sanity
    memset(_timestamps, 0, sizeof(_timestamps));
  }

private:
  static const int CLOCK_WINDOW = 24; // 24 PPQN = 1 Beat
  unsigned long _timestamps[CLOCK_WINDOW];
  int _head;
  int _sampleCount;
  unsigned long _lastTickMicros;
  float _bpm;
};

#endif // MIDI_CLOCK_H
