#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#define DEBUG_MODE 1

#if DEBUG_MODE
    #define LOG(...) do { Serial.printf(__VA_ARGS__); Serial.println(); } while (0)
#else
    #define LOG(...)
#endif

// --- Hardware ---
#define POT_CALIB_MIN 0
#define POT_CALIB_MAX 1021
#define HEADPHONE_VOLUME 0.8f

// --- Audio Settings ---
#define BIT_RATE 16
#define SAMPLE_RATE 44100
#define TOTAL_SRAM_SAMPLES 8388608
#define BLOCK_SIZE 128
#define LOOP_BUFFER_SIZE 2048
#define NUM_LOOPS 8
#define FEEDBACK_MULTIPLIER 0.95f

// Helpers
#define SAMPLES_TO_BYTES(x) (x * (BIT_RATE / 8))
#define MS_TO_SAMPLES(x) ((x * SAMPLE_RATE) / 1000)
#define SAMPLES_TO_MS(x) ((x * 1000) / SAMPLE_RATE)
#define SAMPLE_LIMITER(x) (x > 32767 ? 32767 : (x < -32768 ? -32768 : x))
#define BLOCKS_TO_ADDR(x) ((x) * BLOCK_SIZE * 2)

// Fade Settings
#define FADE_DURATION_BLOCKS 3

// --- Stomp-Forgiveness Settings ---
#define FORGIVENESS_MS 300
#define FORGIVENESS_BLOCKS (MS_TO_SAMPLES(FORGIVENESS_MS) / BLOCK_SIZE + 1)

#endif // DEFINITIONS_H
