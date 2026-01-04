#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <Audio.h>
#include "BALibrary.h"
#include "TrackManager.h"
#include "Definitions.h"

using namespace BALibrary;

class AudioLooper : public AudioStream {
public:
    enum State {
        STATE_IDLE,             // No tracks recorded
        STATE_RECORD_MASTER,    // Recording Track 1
        STATE_PLAY,             // Playing all recorded tracks
        STATE_ARM_RECORD,       // Waiting for Loop Start to begin recording next track
        STATE_RECORD_SLAVE,     // Recording next track (quantized)
        STATE_ARM_STOP          // Waiting for Loop End to stop recording
    };

    AudioLooper();
    virtual void update(void);

    // Initialization
    void setTrackManager(TrackManager* tm);
    void setLoopDepth(float depth); // 0.0 to 1.0

    // Controls
    void trigger(); 
    void stopAndClear(); 

    // Getters
    State getState() { return m_state; }

    // Removed: isClearRequested, acknowledgeClear (managed by TrackManager state now)

private:
    State m_state;
    TrackManager* m_trackManager;
    
    // Track State
    bool m_trackActive[NUM_AUDIO_TRACKS]; // 9 Tracks
    int m_activeTrackIdx; // The track currently being recorded/targeted
    
    // Logic
    bool m_isFirstCycle;
    uint32_t m_masterLoopLength;
    uint32_t m_currentPos; // 0 to masterLoopLength
    int m_visibleTrackCount; // Number of tracks currently audible (1..9)

    audio_block_t *m_inputQueueArray[1];

    // Helpers
    void handleRecordMaster(audio_block_t *in, audio_block_t *out);
    void handlePlay(audio_block_t *in, audio_block_t *out); // Used by PLAY, ARM_RECORD, and ARM_STOP
    // handleRecordSlave integrated into handlePlay for simplicity
};

#endif
