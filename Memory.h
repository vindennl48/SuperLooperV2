#ifndef MEMORY_H
#define MEMORY_H

#include <Arduino.h>
#include <SD.h>
#include <Audio.h>
#include "BALibrary.h"
#include "LibMemoryManagement.h"
#include "Definitions.h"

using namespace BALibrary;

// -------------------------------------------------------------------------
// MemoryRam
// A simple Ring Buffer wrapper around the external memory slots.
// -------------------------------------------------------------------------
class MemoryRam {
public:
    MemoryRam(int memChipIndex, size_t sizeInBlocks) 
        : m_memChipIndex(memChipIndex), m_sizeInBlocks(sizeInBlocks) 
    {
        if (!allocateRingBuffer()) {
            LOG("ERROR: Failed to allocate ring buffer on MEM%d", m_memChipIndex);
        }
    }

    virtual ~MemoryRam() {
        // Persistent memory manager handles cleanup usually, 
        // but explicit cleanup could go here if the library supported it.
    }

    bool push(audio_block_t* block) {
        return push(block->data);
    }

    bool push(int16_t* data) {
        if (m_storedBlocks >= m_sizeInBlocks) return false;
        
        bool success = m_memSlot.writeAdvance16(data, AUDIO_BLOCK_SAMPLES);
        if (success) {
            __disable_irq();
            m_storedBlocks++;
            __enable_irq();
        }
        return success;
    }

    bool pop(audio_block_t* block) {
        if (m_storedBlocks == 0) return false;

        bool success = m_memSlot.readAdvance16(block->data, AUDIO_BLOCK_SAMPLES);
        if (success) {
            __disable_irq();
            m_storedBlocks--;
            __enable_irq();
        }
        return success;
    }

    // New helper to pop data into a raw buffer (for writing to SD)
    bool popToBuffer(int16_t* buffer) {
        if (m_storedBlocks == 0) return false;
        
        bool success = m_memSlot.readAdvance16(buffer, AUDIO_BLOCK_SAMPLES);
        if (success) {
            __disable_irq();
            m_storedBlocks--;
            __enable_irq();
        }
        return success;
    }

    void reset() {
        m_memSlot.setWritePosition(0);
        m_memSlot.setReadPosition(0);
        __disable_irq();
        m_storedBlocks = 0;
        __enable_irq();
    }

    size_t getStoredBlocks() const { return m_storedBlocks; }
    size_t getSpaceBlocks() const { return m_sizeInBlocks - m_storedBlocks; }
    size_t getSizeInBlocks() const { return m_sizeInBlocks; }

protected:
    int m_memChipIndex;
    size_t m_sizeInBlocks;
    ExtMemSlot m_memSlot;
    volatile size_t m_storedBlocks = 0;

    static ExternalSramManager* getSramManager() {
        static ExternalSramManager* manager = new ExternalSramManager();
        return manager;
    }

    bool allocateRingBuffer() {
        size_t sizeBytes = m_sizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
        MemSelect memSelect = (m_memChipIndex == 0) ? MemSelect::MEM0 : MemSelect::MEM1;
        return getSramManager()->requestMemory(&m_memSlot, sizeBytes, memSelect, false);
    }
};

// -------------------------------------------------------------------------
// MemorySd
// Manages a specific Track/Loop.
// Encapsulates an Input RAM Buffer, an Output RAM Buffer, and an SD File.
// -------------------------------------------------------------------------
class MemorySd {
public:
    MemorySd(int ramChipIndex, size_t bufferSizeBlocks) 
    {
        ensureSdInit();
        m_uniqueId = getNextId();
        
        // Allocate Input and Output buffers
        // We put both on the same chip for simplicity, or we could split them if needed.
        m_inputBuffer = new MemoryRam(ramChipIndex, bufferSizeBlocks);
        m_outputBuffer = new MemoryRam(ramChipIndex, bufferSizeBlocks);
        
        createAndOpenFile();
    }

    ~MemorySd() {
        if (m_inputBuffer) delete m_inputBuffer;
        if (m_outputBuffer) delete m_outputBuffer;
        if (m_file) m_file.close();
    }

    // --- Audio Thread Interface (ISR Safe) ---

    /**
     * @brief Writes audio data to the Input Buffer (Recording).
     */
    void writeSample(audio_block_t* block) {
        // If buffer is full, we drop samples (overrun)
        if (m_inputBuffer) m_inputBuffer->push(block);
    }

    /**
     * @brief Reads audio data from the Output Buffer (Playback).
     * @return true if data was available, false if silence/underrun.
     */
    bool readSample(audio_block_t* block) {
        if (m_outputBuffer) return m_outputBuffer->pop(block);
        return false;
    }

    // --- Main Loop Interface (Maintenance) ---

    /**
     * @brief Manages data transfer between RAM buffers and SD Card.
     *        Must be called frequently in the main loop.
     */
    void update() {
        if (m_shouldClear) {
            performClear();
            return;
        }

        if (!m_file) return;

        // 1. FLUSH INPUT: Move data from Input RAM -> SD
        // We check if we have data waiting to be written
        if (m_inputBuffer && m_inputBuffer->getStoredBlocks() > 0) {
            flushInputToSd();
        }

        // 2. REFILL OUTPUT: Move data from SD -> Output RAM
        // We check if we have space and if there is data left to read
        // Note: We only read if the file has data (m_fileSizeInBlocks > 0)
        if (m_outputBuffer && m_fileSizeInBlocks > 0) {
            // Check if we need to wrap the read cursor (Looping)
            size_t fileSizeBytes = m_fileSizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
            if (m_readCursor >= fileSizeBytes) {
                m_readCursor = 0;
            }

            if (m_outputBuffer->getSpaceBlocks() > 0) {
                fetchSdToOutput();
            }
        }
    }

    /**
     * @brief Clears the loop data (RAM and SD) and resets cursors.
     */
    void clearLoop() {
        if (m_inputBuffer) m_inputBuffer->reset();
        if (m_outputBuffer) m_outputBuffer->reset();
        
        m_readCursor = 0;
        m_writeCursor = 0;
        m_fileSizeInBlocks = 0;
        
        m_shouldClear = true; // Defer file operations to update()
    }
    
    /**
     * @brief Resets playhead to start (for simple re-triggering).
     */
    void restartPlayback() {
        m_readCursor = 0;
        if (m_outputBuffer) m_outputBuffer->reset();
    }

    bool isClearing() const { return m_shouldClear; }
    size_t getRecordedBlocks() const { return m_fileSizeInBlocks; }

    static void removeAllFiles() {
        ensureSdInit();
        File root = SD.open("/");
        while (true) {
            File entry = root.openNextFile();
            if (!entry) break;
            String name = entry.name();
            if (!entry.isDirectory()) {
                entry.close();
                SD.remove(name.c_str());
            } else {
                entry.close();
            }
        }
        root.close();
        LOG("Removed all SD Files");
    }

private:
    int m_uniqueId;
    String m_binFileName;
    File m_file;
    
    MemoryRam* m_inputBuffer = nullptr;
    MemoryRam* m_outputBuffer = nullptr;
    
    // File Cursors (in bytes)
    size_t m_readCursor = 0;
    size_t m_writeCursor = 0;
    size_t m_fileSizeInBlocks = 0;
    
    bool m_shouldClear = false;

    // Buffering constants
    static const int BATCH_SIZE = 32; // 8KB buffer for optimal SD writes
    int16_t m_tempBuffer[AUDIO_BLOCK_SAMPLES * BATCH_SIZE];

    void flushInputToSd() {
        // Loop until we have processed all stored blocks
        while (m_inputBuffer->getStoredBlocks() > 0) {
            // We can write up to BATCH_SIZE blocks at a time
            size_t available = m_inputBuffer->getStoredBlocks();
            size_t toWrite = (available > BATCH_SIZE) ? BATCH_SIZE : available;
            
            if (toWrite == 0) break;

            // 1. Seek
            if (!m_file.seek(m_writeCursor)) {
                LOG("Err: seek write failed");
                break;
            }

            // 2. Pop from RAM into Temp Buffer
            for (size_t i = 0; i < toWrite; i++) {
                m_inputBuffer->popToBuffer(&m_tempBuffer[i * AUDIO_BLOCK_SAMPLES]);
            }

            // 3. Write to SD
            size_t bytesToWrite = toWrite * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
            size_t written = m_file.write((uint8_t*)m_tempBuffer, bytesToWrite);

            // 4. Update State
            m_writeCursor += written;
            m_fileSizeInBlocks += (written / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t)));
            
            // Safety break if write failed or was partial
            if (written < bytesToWrite) {
                 LOG("Err: SD Write partial/failed");
                 break;
            }
        }
    }

    void fetchSdToOutput() {
        // Loop until output buffer is full or file data runs out
        while (m_outputBuffer->getSpaceBlocks() > 0) {
            // Calculate available file data
            size_t fileSizeBytes = m_fileSizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
            
            // Handle EOF/Looping logic higher up, but check safety here
            if (m_readCursor >= fileSizeBytes) break;

            size_t bytesRemaining = fileSizeBytes - m_readCursor;
            size_t blocksRemaining = bytesRemaining / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
            
            if (blocksRemaining == 0) break;

            size_t space = m_outputBuffer->getSpaceBlocks();
            size_t toRead = (space > BATCH_SIZE) ? BATCH_SIZE : space;
            
            if (toRead > blocksRemaining) toRead = blocksRemaining;
            if (toRead == 0) break;

            // 1. Seek
            if (!m_file.seek(m_readCursor)) {
                LOG("Err: seek read failed");
                break;
            }

            // 2. Read from SD
            size_t bytesToRead = toRead * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
            size_t read = m_file.read((uint8_t*)m_tempBuffer, bytesToRead);

            if (read > 0) {
                // 3. Push to Output RAM
                size_t blocksRead = read / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
                for (size_t i = 0; i < blocksRead; i++) {
                    m_outputBuffer->push(&m_tempBuffer[i * AUDIO_BLOCK_SAMPLES]);
                }
                m_readCursor += read;
            } else {
                break;
            }
        }
    }

    void performClear() {
        if (m_file) m_file.close();
        if (SD.exists(m_binFileName.c_str())) SD.remove(m_binFileName.c_str());
        
        m_file = SD.open(m_binFileName.c_str(), FILE_WRITE);
        if (!m_file) LOG("Err: Failed recreate file");
        
        m_shouldClear = false;
        m_fileSizeInBlocks = 0;
        m_readCursor = 0;
        m_writeCursor = 0;
    }

    void createAndOpenFile() {
        m_binFileName = "track_" + String(m_uniqueId) + ".bin";
        if (SD.exists(m_binFileName.c_str())) SD.remove(m_binFileName.c_str());
        m_file = SD.open(m_binFileName.c_str(), FILE_WRITE);
    }

    static int getNextId() {
        static int id = 0;
        return id++;
    }

    static void ensureSdInit() {
        static bool init = false;
        if (!init) {
            init = SD.begin(BUILTIN_SDCARD);
            if (init) LOG("SD Init OK");
            else LOG("SD Init FAIL");
        }
    }
};

#endif // MEMORY_H