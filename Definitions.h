#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#define DEBUG_MODE 1

#if DEBUG_MODE
    #define LOG(...) do { Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
    #define LOG(...)
#endif

// --- Hardware Calibration ---
#define POT_CALIB_MIN 0
#define POT_CALIB_MAX 1021

// --- Audio Settings ---
#define BIT_RATE 16
#define SAMPLE_RATE 44100
#define TOTAL_SRAM_SAMPLES 8388608
#define LOOP_BUFFER_SIZE 2048
#define NUM_LOOPS 8

#define HEADPHONE_VOLUME 0.8f

// --- Feature Settings ---
#define FORGIVENESS_MS 300
#define FORGIVENESS_BLOCKS (((FORGIVENESS_MS * SAMPLE_RATE) / 1000) / 128 + 1)

#endif // DEFINITIONS_H
