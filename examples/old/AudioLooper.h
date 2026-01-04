#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <Audio.h>
#include "BALibrary.h"
#include "MemoryManager.h"

// --- Debug Configuration ---
#define DEBUG_MODE 1

#if DEBUG_MODE
    #define LOG(...) Serial.printf(__VA_ARGS__); Serial.println()
#else
    #define LOG(...)
#endif

using namespace BALibrary;

class AudioLooper : public AudioStream {
public:
    enum State {
        STATE_IDLE,             // No tracks recorded
        STATE_RECORD_MASTER,    // Recording Track 1
        STATE_PLAY,             // Playing all recorded tracks
        STATE_ARM_RECORD,       // Waiting for Loop Start to begin recording next track
        STATE_RECORD_SLAVE,     // Recording next track (quantized)
        STATE_ARM_STOP,         // Waiting for Loop End to stop recording
        STATE_FADE_OUT          // Fading out before clearing
    };

    AudioLooper();
    virtual void update(void);

    // Initialization
    void setMemoryManager(MemoryManager* mem);
    void setSlaveOverdubMode(bool mode) { m_slaveOverdubMode = mode; }
    void setLoopDepth(float depth); // 0.0 to 1.0

    // Controls
    void trigger(); 
    void stopAndClear(); 

    // Getters
    State getState() { return m_state; }
    bool isClearRequested() { return m_clearRequested; }
    void acknowledgeClear() { m_clearRequested = false; }

private:
    State m_state;
    MemoryManager* m_memory;
    volatile bool m_clearRequested; // Flag for main loop to perform heavy SD clear
    bool m_trackActive[8];
    int m_activeTrackIdx; // The track currently being recorded/targeted
    bool m_slaveOverdubMode; // true = Layering, false = Overwrite
    bool m_isFirstCycle;
    uint32_t m_masterLoopLength;
    uint32_t m_currentPos; // 0 to masterLoopLength
    int m_visibleTrackCount; // Number of tracks currently audible (1..8)
    float m_trackGain[8];
    float m_masterFade; // 0.0 to 1.0
    static constexpr float MUTE_FADE_STEP = 0.005f; 
    static const uint32_t FADE_SAMPLES = 441; // 10ms @ 44.1kHz

    audio_block_t *m_inputQueueArray[1];

    // Helpers
    void handleIdle(audio_block_t *in, audio_block_t *out);
    void handleRecordMaster(audio_block_t *in, audio_block_t *out);
    void handlePlay(audio_block_t *in, audio_block_t *out); // Used by PLAY, ARM_RECORD, and ARM_STOP
    void handleRecordSlave(audio_block_t *in, audio_block_t *out);

    // Buffer for reading from SPI
    int16_t m_readBuffer[AUDIO_BLOCK_SAMPLES];
};

#endif