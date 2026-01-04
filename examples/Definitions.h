#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#define DEBUG_MODE 1

#if DEBUG_MODE
    #define LOG(...) Serial.printf(__VA_ARGS__); Serial.println()
#else
    #define LOG(...)
#endif

// Audio Settings
#define BIT_RATE 16
#define SAMPLE_RATE 44100
#define TOTAL_SRAM_SAMPLES 8388608

// Buffer Management
#define NUM_BUFFER_SLOTS 10 // Total hardware slots (e.g. 5 on MEM0, 5 on MEM1)
#define NUM_AUDIO_TRACKS (NUM_BUFFER_SLOTS - 1) // 9 Playable Tracks, 1 write buffer
#define BUFFER_SLOT_SIZE_SAMPLES ((uint32_t)(TOTAL_SRAM_SAMPLES / 18))

// Helpers
#define SAMPLES_TO_BYTES(x) (x * (BIT_RATE / 8))
#define MS_TO_SAMPLES(x) ((x * SAMPLE_RATE) / 1000)
#define SAMPLES_TO_MS(x) ((x * 1000) / SAMPLE_RATE)
#define WRAP_NUM(i, add, max) (i % (max + add))

// Fade Settings
#define FADE_DURATION_MS 10
#define FADE_SAMPLES MS_TO_SAMPLES(FADE_DURATION_MS)

// SD Card Transfer Settings
#define SD_CS_PIN BUILTIN_SDCARD
#define BLOCK_SIZE_SAMPLES 256
#define SD_BLOCK_SIZE_BYTES SAMPLES_TO_BYTES(BLOCK_SIZE_SAMPLES)

#endif
