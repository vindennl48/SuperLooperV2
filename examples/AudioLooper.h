#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <Audio.h>
#include "BALibrary.h"
#include "TrackManager.h"

using namespace BALibrary;

class AudioLooper : public AudioStream {
public:
    enum State {
        EMPTY,
        RECORD_MASTER,    // Recording Track 1
        PLAY,             // Playing all recorded tracks
        ARM_RECORD,       // Waiting for Loop Start to begin recording next track
        RECORD_SLAVE,     // Recording next track (quantized)
        STATE_ARM_STOP,         // Waiting for Loop End to stop recording
        STATE_FADE_OUT          // Fading out before clearing
    };

    AudioLooper();
    virtual void update(void);

    // Initialization
    void setLoopDepth(float depth); // 0.0 to 1.0

    // Controls
    void trigger(); 
    void stopAndClear(); 

    // Getters
    State getState() { return state; }
    bool isClearRequested() { return m_clearRequested; }
    void acknowledgeClear() { m_clearRequested = false; }

private:
    TrackManager trackManager;
    State state;
    uint32_t quantizeLoopLength;
    uint32_t currentSamplePos;   // 0 to masterLoopLength
    int activeTrack;       // currently activated track
    int visibleTrackCount; // Number of tracks currently audible

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
