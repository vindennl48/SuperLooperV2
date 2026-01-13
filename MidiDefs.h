#ifndef MIDI_DEFS_H
#define MIDI_DEFS_H

// MIDI Status Bytes
#define MIDI_STATUS_NOTE_OFF       0x80
#define MIDI_STATUS_NOTE_ON        0x90
#define MIDI_STATUS_POLY_PRESSURE  0xA0
#define MIDI_STATUS_CONTROL_CHANGE 0xB0
#define MIDI_STATUS_PROGRAM_CHANGE 0xC0
#define MIDI_STATUS_CHANNEL_PRESS  0xD0
#define MIDI_STATUS_PITCH_BEND     0xE0

// System Common / Realtime
#define MIDI_STATUS_SYSEX          0xF0
#define MIDI_STATUS_TIME_CODE      0xF1
#define MIDI_STATUS_SONG_POS       0xF2
#define MIDI_STATUS_SONG_SEL       0xF3
#define MIDI_STATUS_TUNE_REQ       0xF6
#define MIDI_STATUS_CLOCK          0xF8
#define MIDI_STATUS_START          0xFA
#define MIDI_STATUS_CONTINUE       0xFB
#define MIDI_STATUS_STOP           0xFC
#define MIDI_STATUS_ACTIVE_SENSE   0xFE
#define MIDI_STATUS_RESET          0xFF

#endif // MIDI_DEFS_H
