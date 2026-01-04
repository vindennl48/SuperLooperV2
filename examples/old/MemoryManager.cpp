#include "MemoryManager.h"

#define SD_CS_PIN BUILTIN_SDCARD

MemoryManager::MemoryManager() : m_sramManager(2) {
    for (int i = 0; i < 8; i++) {
        m_tracks[i].readSlot = nullptr;
        m_tracks[i].recState = REC_IDLE;
        m_tracks[i].fileLength = 0;
        m_tracks[i].audioReadHead = 0;
        m_tracks[i].sdLoadedHead = 0;
    }
    m_globalWriteHead = 0;
    m_sdFlushedHead = 0;
}

bool MemoryManager::begin() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("MemoryManager: SD Init Failed!");
    } else {
        Serial.println("MemoryManager: SD Init OK.");
    }

    size_t slotSize = 1572864; // 1.5MB bytes
    bool success = true;

    // MEM0: Read Slots 0-3, Write Slot
    for (int i = 0; i < 4; i++) {
        if (!m_sramManager.requestMemory(&m_readSlots[i], slotSize, MEM0, false)) success = false;
        else m_tracks[i].readSlot = &m_readSlots[i];
    }
    if (!m_sramManager.requestMemory(&m_globalWriteSlot, slotSize, MEM0, false)) success = false;

    // MEM1: Read Slots 4-7
    for (int i = 4; i < 8; i++) {
        if (!m_sramManager.requestMemory(&m_readSlots[i], slotSize, MEM1, false)) success = false;
        else m_tracks[i].readSlot = &m_readSlots[i];
    }

    // Open existing files
    for (int i = 0; i < 8; i++) {
        char filename[16];
        sprintf(filename, "TRACK%d.BIN", i + 1);
        m_tracks[i].file = SD.open(filename, FILE_WRITE); 
        if (!m_tracks[i].file) m_tracks[i].file = SD.open(filename, FILE_WRITE_BEGIN);
        if (m_tracks[i].file) m_tracks[i].fileLength = m_tracks[i].file.size() / sizeof(int16_t);
    }

    return success;
}

void MemoryManager::update() {
    // 1. Handle Recording (Global Buffer)
    // We check all tracks to see who owns the recording state, but sync is global
    int activeRecTrack = -1;
    for (int i = 0; i < 8; i++) {
        if (m_tracks[i].recState != REC_IDLE) {
            activeRecTrack = i;
            break;
        }
    }

    if (activeRecTrack != -1) {
        TrackData &t = m_tracks[activeRecTrack];
        
        switch (t.recState) {
            case REC_START_REQUESTED:
                // We use the file handle stored in the track? 
                // Or we assign m_activeRecFile?
                // Let's use t.file for simplicity, but we need to ensure we don't conflict with read?
                // If overdubbing, we read t.file and write to... new file?
                // MVP: Overwrite mode only for now (simplest).
                if (t.file) {
                    t.file.seek(0);
                    m_activeRecFile = t.file; // Point to it
                    m_globalWriteHead = 0;
                    m_sdFlushedHead = 0;
                }
                t.recState = REC_ACTIVE;
                break;

            case REC_ACTIVE:
                syncRecording();
                break;

            case REC_STOP_REQUESTED:
                syncRecording();
                if (m_sdFlushedHead >= m_globalWriteHead) {
                    if (m_activeRecFile) m_activeRecFile.flush();
                    t.fileLength = m_globalWriteHead;
                    
                    // Critical: Tell syncPlayback that RAM is already populated up to the limit
                    t.sdLoadedHead = (t.fileLength > SLOT_SIZE_SAMPLES) ? SLOT_SIZE_SAMPLES : t.fileLength;
                    
                    t.recState = REC_IDLE;
                    
                    // Reset Heads for next use
                    m_globalWriteHead = 0;
                    m_sdFlushedHead = 0;
                }
                break;
                
            default: break;
        }
    }

    // 2. Handle Playback (Read Buffers)
    for (int i = 0; i < 8; i++) {
        if (m_tracks[i].recState == REC_IDLE) { // Only sync playback if not recording/overdubbing this specific track
             syncPlayback(i);
        }
    }
}

void MemoryManager::syncRecording() {
    if (!m_activeRecFile) return;

    if (m_globalWriteHead > m_sdFlushedHead) {
        uint32_t samplesPending = m_globalWriteHead - m_sdFlushedHead;
        if (samplesPending >= CHUNK_SIZE) {
            uint32_t ramPos = m_sdFlushedHead % SLOT_SIZE_SAMPLES;
            
            // Handle wrap read from Write Slot
            if (ramPos + CHUNK_SIZE > SLOT_SIZE_SAMPLES) {
                uint32_t part1 = SLOT_SIZE_SAMPLES - ramPos;
                m_globalWriteSlot.read16(ramPos, m_ioBuffer, part1);
                m_globalWriteSlot.read16(0, m_ioBuffer + part1, CHUNK_SIZE - part1);
            } else {
                m_globalWriteSlot.read16(ramPos, m_ioBuffer, CHUNK_SIZE);
            }

            m_activeRecFile.write((uint8_t*)m_ioBuffer, CHUNK_SIZE * 2);
            m_sdFlushedHead += CHUNK_SIZE;
        }
    }
}

void MemoryManager::syncPlayback(int i) {
    TrackData &t = m_tracks[i];
    if (!t.file || t.fileLength == 0) return;
    if (t.fileLength <= SLOT_SIZE_SAMPLES) return;

    // Circular Buffer Logic for Read Slot
    int32_t dist = (int32_t)t.sdLoadedHead - (int32_t)t.audioReadHead;
    if (dist < 0) dist += t.fileLength;

    if (dist < (int32_t)LEAD_TIME_SAMPLES) {
        t.file.seek(t.sdLoadedHead * 2);
        
        uint32_t readSize = CHUNK_SIZE;
        if (t.sdLoadedHead + readSize > t.fileLength) {
            readSize = t.fileLength - t.sdLoadedHead;
        }

        t.file.read((uint8_t*)m_ioBuffer, readSize * 2);
        
        uint32_t ramPos = t.sdLoadedHead % SLOT_SIZE_SAMPLES;
        
        if (ramPos + readSize > SLOT_SIZE_SAMPLES) {
            uint32_t part1 = SLOT_SIZE_SAMPLES - ramPos;
            t.readSlot->write16(ramPos, m_ioBuffer, part1);
            t.readSlot->write16(0, m_ioBuffer + part1, readSize - part1);
        } else {
            t.readSlot->write16(ramPos, m_ioBuffer, readSize);
        }

        t.sdLoadedHead += readSize;
        if (t.sdLoadedHead >= t.fileLength) t.sdLoadedHead = 0;
    }
}

// Audio Engine: Read from Track's Dedicated Read Buffer
bool MemoryManager::readTrack(int trackIdx, uint32_t pos, int16_t* data, uint32_t len) {
    if (trackIdx < 0 || trackIdx >= 8 || !m_tracks[trackIdx].readSlot) return false;
    
    m_tracks[trackIdx].audioReadHead = pos; // Update for sync
    uint32_t physPos = pos % SLOT_SIZE_SAMPLES;
    
    // Check Wrap
    if (physPos + len > SLOT_SIZE_SAMPLES) {
        uint32_t part1 = SLOT_SIZE_SAMPLES - physPos;
        m_tracks[trackIdx].readSlot->read16(physPos, data, part1);
        m_tracks[trackIdx].readSlot->read16(0, data + part1, len - part1);
        return true;
    } else {
        return m_tracks[trackIdx].readSlot->read16(physPos, data, len);
    }
}

// Audio Engine: Write to Track's Read Buffer (for First Pass / Instant Playback)
bool MemoryManager::writeToReadBuffer(int trackIdx, uint32_t pos, int16_t* data, uint32_t len) {
    if (trackIdx < 0 || trackIdx >= 8 || !m_tracks[trackIdx].readSlot) return false;
    
    // Only write if we are within the buffer limit (17s).
    // If loop > 17s, we stop writing to Read Buffer (it's full) and rely on SD later.
    if (pos + len > SLOT_SIZE_SAMPLES) return true; // Pretend success but drop data

    uint32_t physPos = pos; // Linear write for first pass
    return m_tracks[trackIdx].readSlot->write16(physPos, data, len);
}

// Audio Engine: Write to Global Write Buffer (for SD Stream)
bool MemoryManager::writeToGlobalBuffer(uint32_t pos, int16_t* data, uint32_t len) {
    uint32_t physPos = pos % SLOT_SIZE_SAMPLES;
    
    if (physPos + len > SLOT_SIZE_SAMPLES) {
        uint32_t part1 = SLOT_SIZE_SAMPLES - physPos;
        m_globalWriteSlot.write16(physPos, data, part1);
        m_globalWriteSlot.write16(0, data + part1, len - part1);
    } else {
        m_globalWriteSlot.write16(physPos, data, len);
    }
    
    m_globalWriteHead = pos + len;
    return true;
}

void MemoryManager::startRecording(int trackIdx) {
    if (trackIdx >= 0 && trackIdx < 8) {
        m_tracks[trackIdx].recState = REC_START_REQUESTED;
    }
}

void MemoryManager::stopRecording(int trackIdx, uint32_t length) {
    if (trackIdx >= 0 && trackIdx < 8) {
        m_tracks[trackIdx].recState = REC_STOP_REQUESTED;
    }
}

void MemoryManager::clearAll() {
    for (int i = 0; i < 8; i++) {
        m_tracks[i].recState = REC_IDLE;
        m_tracks[i].fileLength = 0;
        m_tracks[i].audioReadHead = 0;
        m_tracks[i].sdLoadedHead = 0;
        if (m_tracks[i].file) {
            m_tracks[i].file.seek(0);
            m_tracks[i].file.truncate(0); 
            m_tracks[i].file.flush();
        }
    }
}
