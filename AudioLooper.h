#ifndef AUDIO_LOOPER_H
#define AUDIO_LOOPER_H

#include <AudioStream.h>
#include "Definitions.h"
#include "Track.h"

class AudioLooper : public AudioStream {
public:
    enum LooperState {
        IDLE,
        WAITING_TO_RECORD,
        RECORDING,
        WAITING_TO_FINISH,
        PLAYBACK,
        FULL_PLAYBACK
    };

    AudioLooper(void) : AudioStream(1, inputQueueArray) {
        for (int i = 0; i < NUM_LOOPS; i++) {
            tracks[i] = new Track();
        }
        currentTrackIndex = 0;
        state = IDLE;
        globalPlayhead = 0;
        timelineLength = 0;
        quantizationBlocks = 0;
    }

    void begin() {
        // Clear SD card state on startup for a fresh session
        MemorySd::removeAllFiles();

        for (int i = 0; i < NUM_LOOPS; i++) {
            if (tracks[i]) {
                tracks[i]->begin();
                LOG("AudioLooper: Track %d initialized", i);
            }
        }
    }

    // Call this from the main loop() function frequently
    void poll() {
        for (int i = 0; i < NUM_LOOPS; i++) {
            if (tracks[i]) tracks[i]->poll();
        }
    }

    void trigger() {
        Track* currentTrack = tracks[currentTrackIndex];
        if (!currentTrack) return;

        switch (state) {
            case IDLE:
                // Start Recording
                // If this is the very first loop (timelineLength == 0) OR we are at 0, start immediately
                // However, requirement says "only ever start ... if globalPlayhead is at 0".
                // If timelineLength is 0, globalPlayhead is 0.
                if (quantizationBlocks == 0 || (quantizationBlocks > 0 && globalPlayhead % quantizationBlocks == 0)) {
                    LOG("AudioLooper: IDLE -> RECORDING (Track %d) [Immediate]", currentTrackIndex);
                    currentTrack->record();
                    state = RECORDING;
                    
                    // If this is the absolute first start, ensure timeline is reset (redundant but safe)
                    if (timelineLength == 0) {
                        // First loop defines the timeline
                    }
                } else {
                    LOG("AudioLooper: IDLE -> WAITING_TO_RECORD (Track %d)", currentTrackIndex);
                    state = WAITING_TO_RECORD;
                }
                break;

            case WAITING_TO_RECORD:
                // Optional: Allow cancelling? For now, do nothing or maybe force start?
                // Let's stick to strict quantization logic.
                break;

            case RECORDING:
                // Stop Recording
                if (quantizationBlocks == 0) {
                    // First Loop Logic: Finish Immediately
                    LOG("AudioLooper: RECORDING -> PLAYBACK (Track %d) [First Loop Set]", currentTrackIndex);
                    currentTrack->play();
                    
                    // Set Quantization and Timeline
                    quantizationBlocks = currentTrack->getLengthInBlocks();
                    timelineLength = quantizationBlocks;
                    
                    LOG("AudioLooper: Quantization set to %d blocks", quantizationBlocks);

                    if (currentTrackIndex < NUM_LOOPS - 1) {
                        currentTrackIndex++;
                        state = PLAYBACK;
                    } else {
                        state = FULL_PLAYBACK;
                    }
                } else {
                    // Subsequent Loops: Wait for Quantization
                    LOG("AudioLooper: RECORDING -> WAITING_TO_FINISH (Track %d)", currentTrackIndex);
                    state = WAITING_TO_FINISH;
                }
                break;
            
            case WAITING_TO_FINISH:
                // Already waiting. Do nothing.
                break;

            case PLAYBACK:
                // Triggered while playing back: Start Recording Next Loop
                // Must be quantized start
                if (quantizationBlocks > 0 && globalPlayhead % quantizationBlocks == 0) {
                    LOG("AudioLooper: PLAYBACK -> RECORDING (Track %d) [Immediate]", currentTrackIndex);
                    tracks[currentTrackIndex]->record();
                    state = RECORDING;
                } else {
                    LOG("AudioLooper: PLAYBACK -> WAITING_TO_RECORD (Track %d)", currentTrackIndex);
                    state = WAITING_TO_RECORD;
                }
                break;
                
            case FULL_PLAYBACK:
                // Triggered while everything is full: Stop All
                LOG("AudioLooper: FULL_PLAYBACK -> IDLE (Reset)");
                for(int i=0; i<NUM_LOOPS; i++) {
                    if(tracks[i]) tracks[i]->stop();
                }
                currentTrackIndex = 0;
                timelineLength = 0;
                globalPlayhead = 0;
                quantizationBlocks = 0;
                state = IDLE;
                break;
        }
    }

    void reset() {
        LOG("AudioLooper: RESETTING ALL");
        for (int i = 0; i < NUM_LOOPS; i++) {
            if (tracks[i]) tracks[i]->clear();
        }
        currentTrackIndex = 0;
        state = IDLE;
        globalPlayhead = 0;
        timelineLength = 0;
        quantizationBlocks = 0;
    }

    virtual void update(void) {
        audio_block_t *inBlock = receiveReadOnly(0);
        audio_block_t *outBlock = allocate();

        if (!outBlock) {
            if (inBlock) release(inBlock);
            return;
        }

        // Initialize Output with Dry Signal or Silence
        if (inBlock) {
            memcpy(outBlock->data, inBlock->data, sizeof(outBlock->data));
        } else {
            memset(outBlock->data, 0, sizeof(outBlock->data));
        }

        // --- Handle State Transitions (Quantized) ---
        
        // 1. Waiting to Record -> Recording
        if (state == WAITING_TO_RECORD) {
            if (quantizationBlocks == 0 || (quantizationBlocks > 0 && globalPlayhead % quantizationBlocks == 0)) {
                 tracks[currentTrackIndex]->record();
                 state = RECORDING;
            }
        }

        // --- Process Tracks ---
        
        // Temporary buffer for mixing
        audio_block_t tempTrackOut; 

        for (int i = 0; i < NUM_LOOPS; i++) {
            Track* t = tracks[i];
            if (!t) continue;

            // Render track
            // NOTE: If we are WAITING_TO_FINISH, we are technically still in RECORDING state in the Track object
            // so t->tick() will continue to write to memory.
            t->tick(inBlock, &tempTrackOut);

            // Mix if playing
            if (t->getState() == Track::PLAYBACK || t->getState() == Track::IDLE) {
                 for (int s = 0; s < AUDIO_BLOCK_SAMPLES; s++) {
                    int32_t sample = outBlock->data[s] + tempTrackOut.data[s];
                    
                    // Simple Hard Limiter
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                    
                    outBlock->data[s] = (int16_t)sample;
                }
            }
        }

        // --- Handle State Transitions (Post-Process) ---

        // 2. Waiting to Finish -> Playback
        if (state == WAITING_TO_FINISH) {
            // Check if we hit the quantization multiple
            // Note: Track::tick() just ran, so length is updated.
            size_t len = tracks[currentTrackIndex]->getLengthInBlocks();
            if (len > 0 && quantizationBlocks > 0 && (len % quantizationBlocks == 0)) {
                tracks[currentTrackIndex]->play();
                
                // Update Global Timeline if this track is the longest
                // (Though with quantization, it should be a multiple of existing, 
                // so we just ensure timelineLength accommodates it if we want to loop the whole thing)
                // For a simple looper, usually the first loop sets the 'master' cycle.
                // But if we record a longer loop (e.g. 2x), we might want to extend the global cycle?
                // Let's assume we extend timeline to match longest loop for correct wrapping.
                if (len > timelineLength) {
                    timelineLength = len;
                }
                
                if (currentTrackIndex < NUM_LOOPS - 1) {
                    currentTrackIndex++;
                    state = PLAYBACK; 
                } else {
                    state = FULL_PLAYBACK;
                }
            }
        }

        // --- Update Global Timeline ---
        if (timelineLength > 0) {
            // Only advance playhead if we are active
            // Note: We advance even if WAITING_*, as we are waiting for a specific time point
            if (state != IDLE) {
                globalPlayhead++;
                if (globalPlayhead >= timelineLength) {
                    globalPlayhead = 0;
                }
            }
        }

        transmit(outBlock, 0);
        release(outBlock);
        if (inBlock) release(inBlock);
    }
    
    LooperState getState() { return state; }
    uint32_t getPlayhead() { return globalPlayhead; }
    uint32_t getTimelineLength() { return timelineLength; }

private:
    audio_block_t *inputQueueArray[1];
    Track* tracks[NUM_LOOPS];
    int currentTrackIndex;
    
    volatile LooperState state;
    uint32_t globalPlayhead;
    uint32_t timelineLength;
    uint32_t quantizationBlocks;
};

#endif // AUDIO_LOOPER_H