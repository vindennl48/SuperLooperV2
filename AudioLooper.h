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
        activeTrackCount = 0;
        state = IDLE;
        globalPlayhead = 0;
        timelineLength = 0;
        quantizationBlocks = 0;
        
        // Initialize History
        historyHead = 0;
        memset(inputHistory, 0, sizeof(inputHistory));
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

    // New function to handle Potentiometer Muting Logic
    void updateMuteState(float potVal) {
        // Only allow muting changes if we are not currently recording or waiting to record
        // We also don't mute if we have 0 or 1 track (Track 0 is always unmuted)
        if (state == RECORDING || state == WAITING_TO_RECORD || state == WAITING_TO_FINISH || activeTrackCount <= 1) {
            return;
        }

        // Determine the split point (first muted track index) based on Pot Value
        // Range: 1 to activeTrackCount
        // (Track 0 is always unmuted, so minimum cutoff is 1)
        int cutoff = (int)(potVal * activeTrackCount) + 1;
        if (cutoff > activeTrackCount) cutoff = activeTrackCount;

        // Apply mute state to all active tracks
        for (int i = 0; i < activeTrackCount; i++) {
            if (i < cutoff) {
                tracks[i]->unmute();
            } else {
                tracks[i]->mute();
            }
        }

        // Update pointer to the cut-off point
        // If we are appending, this will be activeTrackCount.
        // If we are branching (muted), this will be the index of the first muted track.
        currentTrackIndex = cutoff;
        
        // Safety clamp for array access (e.g. if activeTrackCount == NUM_LOOPS)
        if (currentTrackIndex >= NUM_LOOPS) {
            currentTrackIndex = NUM_LOOPS - 1;
        }

        // Logic Check: If we are pointing at an existing track (Branching), 
        // ensure we are in PLAYBACK mode to allow re-recording.
        // (If we were in FULL_PLAYBACK, we must transition to allow trigger() to record)
        if (currentTrackIndex < activeTrackCount && state == FULL_PLAYBACK) {
            state = PLAYBACK;
        }
    }

    void trigger() {
        Track* currentTrack = tracks[currentTrackIndex];
        if (!currentTrack) return;

        // Calculate timing relative to quantization grid
        uint32_t blocksLate = 0;
        if (quantizationBlocks > 0) {
            blocksLate = globalPlayhead % quantizationBlocks;
        }

        // Helper: Check if we are within the forgiveness window (Late Start/Stop)
        // We only forgive if we have an established timeline (quantizationBlocks > 0)
        // and we are slightly past the mark (blocksLate > 0).
        bool isLate = (quantizationBlocks > 0) && (blocksLate > 0) && (blocksLate <= FORGIVENESS_BLOCKS);

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
                } else if (isLate) {
                    // --- Late Start Forgiveness ---
                    LOG("AudioLooper: IDLE -> RECORDING (Track %d) [Late Start Fix: %d blocks]", currentTrackIndex, blocksLate);
                    
                    AudioNoInterrupts();
                    
                    // 1. Start Recording (clears counters)
                    currentTrack->record();
                    state = RECORDING;

                    // 2. Inject History
                    // We need to retrieve blocks from T-blocksLate to T-1
                    // The buffer is at historyHead (which is where the NEXT sample goes, so historyHead-1 is the newest)
                    audio_block_t tempBlock;
                    
                    for (uint32_t i = blocksLate; i > 0; i--) {
                        // Calculate index: (head - i + size) % size
                        int idx = (historyHead - i + FORGIVENESS_BLOCKS) % FORGIVENESS_BLOCKS;
                        
                        memcpy(tempBlock.data, inputHistory[idx], sizeof(tempBlock.data));
                        currentTrack->injectBlock(&tempBlock);
                    }
                    
                    AudioInterrupts();

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
                    
                    // Update Active Count
                    if (currentTrackIndex >= activeTrackCount) {
                        activeTrackCount = currentTrackIndex + 1;
                    }
                    
                    LOG("AudioLooper: Quantization set to %d blocks", quantizationBlocks);

                    if (currentTrackIndex < NUM_LOOPS - 1) {
                        currentTrackIndex++;
                        state = PLAYBACK;
                    } else {
                        state = FULL_PLAYBACK;
                    }
                } else {
                    // Subsequent Loops
                    if (isLate) {
                        // --- Late Stop Forgiveness ---
                        LOG("AudioLooper: RECORDING -> PLAYBACK (Track %d) [Late Stop Fix: %d blocks]", currentTrackIndex, blocksLate);
                        
                        AudioNoInterrupts();
                        currentTrack->trim(blocksLate); // Cut the extra blocks
                        currentTrack->play();
                        AudioInterrupts();
                        
                        // Advance State Logic (Copied from WAITING_TO_FINISH handler)
                        if (currentTrackIndex >= activeTrackCount) {
                            activeTrackCount = currentTrackIndex + 1;
                        }
                        // Update Global Timeline if extended (unlikely if trimming, but safety check)
                        if (currentTrack->getLengthInBlocks() > timelineLength) {
                            timelineLength = currentTrack->getLengthInBlocks();
                        }
                        
                        if (currentTrackIndex < NUM_LOOPS - 1) {
                            currentTrackIndex++;
                            state = PLAYBACK; 
                        } else {
                            state = FULL_PLAYBACK;
                        }
                        
                    } else {
                        LOG("AudioLooper: RECORDING -> WAITING_TO_FINISH (Track %d)", currentTrackIndex);
                        state = WAITING_TO_FINISH;
                    }
                }
                break;
            
            case WAITING_TO_FINISH:
                // Already waiting. Do nothing.
                break;

            case PLAYBACK:
                // Triggered while playing back: Start Recording Next Loop (or Overwrite Branch)
                
                // BRANCHING LOGIC: If we are recording into a slot that was previously active (or muted branch)
                if (currentTrackIndex < activeTrackCount) {
                     LOG("AudioLooper: Branching Detected at %d. Clearing future tracks.", currentTrackIndex);
                     // Clear this track and all future tracks
                     for (int i = currentTrackIndex; i < NUM_LOOPS; i++) {
                         tracks[i]->clear();
                         tracks[i]->unmute(); // Reset mute state
                     }
                     // Reset active count to this index (effectively deleting the branch)
                     activeTrackCount = currentTrackIndex;
                }

                // Must be quantized start
                if (quantizationBlocks > 0 && globalPlayhead % quantizationBlocks == 0) {
                    LOG("AudioLooper: PLAYBACK -> RECORDING (Track %d) [Immediate]", currentTrackIndex);
                    tracks[currentTrackIndex]->record();
                    state = RECORDING;
                } else if (isLate) {
                    // --- Late Start Forgiveness (Branching/Overdub/Next Track) ---
                    LOG("AudioLooper: PLAYBACK -> RECORDING (Track %d) [Late Start Fix: %d blocks]", currentTrackIndex, blocksLate);

                    AudioNoInterrupts();
                    // 1. Start
                    tracks[currentTrackIndex]->record();
                    state = RECORDING;
                    
                    // 2. Inject
                    audio_block_t tempBlock;
                    for (uint32_t i = blocksLate; i > 0; i--) {
                        int idx = (historyHead - i + FORGIVENESS_BLOCKS) % FORGIVENESS_BLOCKS;
                        memcpy(tempBlock.data, inputHistory[idx], sizeof(tempBlock.data));
                        tracks[currentTrackIndex]->injectBlock(&tempBlock);
                    }
                    AudioInterrupts();
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

                // Full reset everything
                currentTrackIndex = 0;
                // We keep activeTrackCount? No, if we reset timelineLength, we break sync.
                // Let's Reset Completely for now to avoid complexity of "Resume".
                // Or better: Just Stop.
                 for(int i=0; i<NUM_LOOPS; i++) {
                    if(tracks[i]) tracks[i]->clear();
                }
                activeTrackCount = 0;
                
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
            if (tracks[i]) {
                tracks[i]->clear();
                tracks[i]->unmute();
            }
        }
        currentTrackIndex = 0;
        activeTrackCount = 0;
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

        // --- Input History Management ---
        if (inBlock) {
            // Copy input to history buffer for Stomp Forgiveness
            memcpy(inputHistory[historyHead], inBlock->data, sizeof(inputHistory[0]));
        } else {
            // Record silence if no block
            memset(inputHistory[historyHead], 0, sizeof(inputHistory[0]));
        }
        // Advance Head
        historyHead++;
        if (historyHead >= FORGIVENESS_BLOCKS) historyHead = 0;


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
                 // BRANCHING LOGIC CHECK (Deferred from Trigger)
                 if (currentTrackIndex < activeTrackCount) {
                     LOG("AudioLooper: Branching Executed at %d (Quantized).", currentTrackIndex);
                     for (int i = currentTrackIndex; i < NUM_LOOPS; i++) {
                         tracks[i]->clear();
                         tracks[i]->unmute();
                     }
                     activeTrackCount = currentTrackIndex;
                 }
                 
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
                
                // Update Active Count
                if (currentTrackIndex >= activeTrackCount) {
                    activeTrackCount = currentTrackIndex + 1;
                }
                
                // Update Global Timeline
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
    int getActiveTrackCount() { return activeTrackCount; }
    int getCurrentTrackIndex() { return currentTrackIndex; }
    
    // Returns true if we are pointing at the next empty slot (appending), 
    // false if we are pointing at an existing track (branching/overwriting).
    bool isAtEndOfList() { return currentTrackIndex == activeTrackCount; }

private:
    audio_block_t *inputQueueArray[1];
    Track* tracks[NUM_LOOPS];
    int currentTrackIndex;
    int activeTrackCount;
    
    volatile LooperState state;
    uint32_t globalPlayhead;
    uint32_t timelineLength;
    uint32_t quantizationBlocks;

    // Forgiveness History
    int16_t inputHistory[FORGIVENESS_BLOCKS][AUDIO_BLOCK_SAMPLES];
    int historyHead;
};

#endif // AUDIO_LOOPER_H
