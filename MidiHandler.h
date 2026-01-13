#ifndef MIDI_HANDLER_H
#define MIDI_HANDLER_H

#include <Arduino.h>
#include <MIDI.h>
#include "AudioLooper.h"
#include "MidiDefs.h"
#include "Definitions.h"
#include "MidiClock.h"

// Define the Serial MIDI type alias for convenience
typedef midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> SerialMidiInterface;

class MidiHandler {
public:
  MidiHandler(AudioLooper& looper, SerialMidiInterface& midi, MidiClock& clock) 
    : _looper(looper), _midi(midi), _clock(clock) {}

  void update() {
    // 1. Process USB MIDI
    if (usbMIDI.read()) {
      byte type = usbMIDI.getType();
      byte channel = usbMIDI.getChannel();
      byte data1 = usbMIDI.getData1();
      byte data2 = usbMIDI.getData2();

      processCommonMidi(type, channel, data1, data2, "USB");

      // Thru to Hardware MIDI (Include System Realtime)
      if (type != MIDI_STATUS_SYSEX) { 
        _midi.send((midi::MidiType)type, data1, data2, channel);
      }
    }

    // 2. Process Hardware MIDI
    if (_midi.read()) {
      byte type = (byte)_midi.getType();
      byte channel = _midi.getChannel();
      byte data1 = _midi.getData1();
      byte data2 = _midi.getData2();

      processCommonMidi(type, channel, data1, data2, "Serial");

      // Thru to USB MIDI (Include System Realtime)
      if (type != MIDI_STATUS_SYSEX) {
        usbMIDI.send(type, data1, data2, channel, 0); 
        _midi.send((midi::MidiType)type, data1, data2, channel);
      }
    }
  }

private:
  AudioLooper& _looper;
  SerialMidiInterface& _midi;
  MidiClock& _clock;

  const char* getMidiName(byte type) {
    // Check Control Change (Channel Voice Message)
    if ((type & 0xF0) == MIDI_STATUS_CONTROL_CHANGE) {
        return "ControlChange";
    }

    // Check System Realtime Messages
    switch (type) {
      case MIDI_STATUS_CLOCK:    return "Clock";
      case MIDI_STATUS_START:    return "Start";
      case MIDI_STATUS_CONTINUE: return "Continue";
      case MIDI_STATUS_STOP:     return "Stop";
    }
    
    return "Unknown";
  }

  void processCommonMidi(byte type, byte channel, byte data1, byte data2, const char* source) {
    const char* name = getMidiName(type);
    
    // Application Logic
    if ((type & 0xF0) == MIDI_STATUS_CONTROL_CHANGE) {
      // Log to Serial Monitor only for CC messages
      LOG("MIDI %s: %s (%d), Ch=%d, D1=%d, D2=%d", source, name, type, channel, data1, data2);

      if (data1 == 10) {
        _looper.trigger();
      } else if (data1 == 11) {
        _looper.reset();
      }
    }
    // Realtime / Clock Logic
    else if (type == MIDI_STATUS_CLOCK) {
      _clock.handleClock();
    }
    else if (type == MIDI_STATUS_START) {
      _clock.handleStart();
    }
    else if (type == MIDI_STATUS_CONTINUE) {
      _clock.handleContinue();
    }
    else if (type == MIDI_STATUS_STOP) {
      _clock.handleStop();
    }
  }
};

#endif // MIDI_HANDLER_H
