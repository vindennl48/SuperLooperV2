#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include "BALibrary.h"

using namespace BALibrary;

enum RecordState {
    REC_IDLE,
    REC_START_REQUESTED,
    REC_ACTIVE,
    REC_STOP_REQUESTED
};

struct TrackData {
    ExtMemSlot* readSlot;  // 1.5MB Playback Cache
    File file;             // SD Card File
    volatile RecordState recState; 
    uint32_t fileLength;   // Total length of valid audio (samples)

    // Sync Pointers
    volatile uint32_t audioReadHead;  
    uint32_t sdLoadedHead;     
};

class MemoryManager {
public:
    MemoryManager();
    bool begin(); 
    void update(); 

    // Audio Engine Interface
    bool readTrack(int trackIdx, uint32_t pos, int16_t* data, uint32_t len);
    
    // Dual-Write Logic
    bool writeToReadBuffer(int trackIdx, uint32_t pos, int16_t* data, uint32_t len);
    bool writeToGlobalBuffer(uint32_t pos, int16_t* data, uint32_t len);
    
    // Lifecycle
    void startRecording(int trackIdx);
    void stopRecording(int trackIdx, uint32_t length); 
    void clearAll();

    static const uint32_t SLOT_SIZE_SAMPLES = 786432; // 1.5MB / 2 bytes = 786432 samples (~17s)

private:
    TrackData m_tracks[8];
    ExternalSramManager m_sramManager;
    
    ExtMemSlot m_readSlots[8];
    ExtMemSlot m_globalWriteSlot;

    // Write Buffer Sync Tracking
    File m_activeRecFile;
    volatile uint32_t m_globalWriteHead; // Audio engine position
    uint32_t m_sdFlushedHead;            // SD saved position

    // SD I/O Buffers
    static const size_t CHUNK_SIZE = 512; 
    int16_t m_ioBuffer[CHUNK_SIZE];
    
    static const uint32_t LEAD_TIME_SAMPLES = 200000; 

    // Helpers
    void syncRecording(); // Flushes Global Write Buffer -> m_activeRecFile
    void syncPlayback(int i);
};

#endif
