#ifndef MEMORY_H
#define MEMORY_H

#include <Arduino.h>
#include <SD.h>
#include <Audio.h>
#include "BALibrary.h"
#include "LibMemoryManagement.h"
#include "Definitions.h"

using namespace BALibrary;

// Base class for RAM-only ring buffer operations
class MemoryRam {
public:
    /**
     * @brief Constructor for the MemoryRam class.
     * @param memChipIndex The index of the MEM chip to use (0 or 1).
     * @param sizeInBlocks The size of the allocation in AUDIO_BLOCK_SAMPLES.
     */
    MemoryRam(int memChipIndex, size_t sizeInBlocks) 
        : m_memChipIndex(memChipIndex), m_sizeInBlocks(sizeInBlocks) 
    {
        // Allocate the ring buffer on the requested memory chip
        if (!allocateRingBuffer()) {
            LOG("ERROR: Failed to allocate ring buffer on MEM%d with size %d blocks", m_memChipIndex, m_sizeInBlocks);
        }
    }

    virtual ~MemoryRam() {
        // No specific cleanup needed for RAM slot as manager handles it / persistent
    }

    /**
     * @brief Adds an audio block to the ring buffer.
     * @details If the buffer is full, the new data is discarded.
     * @param block Pointer to the audio block to be added.
     * @return true if added, false if discarded (full).
     */
    bool push(audio_block_t* block) {
        if (m_storedBlocks >= m_sizeInBlocks) {
            return false;
        }

        // Write data to memory
        // writeAdvance16 handles the wrapping of the memory slot pointer
        bool success = m_memSlot.writeAdvance16(block->data, AUDIO_BLOCK_SAMPLES);

        if (success) {
            // Increment count safely
            __disable_irq();
            m_storedBlocks++;
            __enable_irq();
        } else {
            LOG("ERROR: MemoryRam writeAdvance16 failed (MEM%d)", m_memChipIndex);
        }

        return success;
    }

    /**
     * @brief Pulls an audio block from the ring buffer.
     * @details Retrieves data in FIFO order.
     * @param block Pointer to the audio block where data will be written.
     * @return true if data was retrieved, false if buffer was empty.
     */
    bool pop(audio_block_t* block) {
        if (m_storedBlocks == 0) {
            return false;
        }

        // Read data from memory
        // readAdvance16 handles the wrapping of the memory slot pointer
        bool success = m_memSlot.readAdvance16(block->data, AUDIO_BLOCK_SAMPLES);

        if (success) {
            // Decrement count safely
            __disable_irq();
            m_storedBlocks--;
            __enable_irq();
        } else {
            LOG("ERROR: MemoryRam readAdvance16 failed (MEM%d)", m_memChipIndex);
        }

        return success;
    }

    /**
     * @brief Resets the ring buffer pointers and block count.
     */
    virtual void reset() {
        m_memSlot.setWritePosition(0);
        m_memSlot.setReadPosition(0);
        
        __disable_irq();
        m_storedBlocks = 0;
        __enable_irq();
    }

protected:
    int m_memChipIndex;
    size_t m_sizeInBlocks;
    ExtMemSlot m_memSlot;
    
    // Volatile to ensure atomic access isn't optimized away
    volatile size_t m_storedBlocks = 0;

    // Helper to get a persistent ExternalSramManager.
    static ExternalSramManager* getSramManager() {
        static ExternalSramManager* manager = new ExternalSramManager();
        return manager;
    }

    /**
     * @brief allocates a ring buffer of the supplied size onto the configured MEM chip.
     */
    bool allocateRingBuffer() {
        size_t sizeBytes = m_sizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
        MemSelect memSelect = (m_memChipIndex == 0) ? MemSelect::MEM0 : MemSelect::MEM1;
        
        // Request memory from the manager. 
        return getSramManager()->requestMemory(&m_memSlot, sizeBytes, memSelect, false);
    }
};

// Derived class that adds SD card functionality
class MemorySd : public MemoryRam {
public:
    /**
     * @brief Constructor for the MemorySd class.
     * @param memChipIndex The index of the MEM chip to use (0 or 1).
     * @param sizeInBlocks The size of the allocation in AUDIO_BLOCK_SAMPLES.
     */
    MemorySd(int memChipIndex, size_t sizeInBlocks) 
        : MemoryRam(memChipIndex, sizeInBlocks) 
    {
        ensureSdInit();
        
        // Programmatically create a unique ID for this instance
        m_uniqueId = getNextId();
        
        // Create and keep the BIN file open on the SD card
        createAndOpenFile();
    }

    /**
     * @brief Destructor to ensure the file is closed properly.
     */
    ~MemorySd() override {
        if (m_file) {
            m_file.close();
        }
    }

    /**
     * @brief Resets the RAM buffer and schedules a file reset.
     *        File deletion/recreation happens in update().
     */
    void clearLoop() {
        // Reset the RAM buffer first (instant)
        MemoryRam::reset();
        
        m_playhead = 0;
        m_fileSizeInBlocks = 0;
        
        // Schedule heavy SD operations for the next update() call
        m_shouldClear = true;
    }

    /**
     * @brief Resets the playhead to the beginning of the SD file.
     *        This triggers a reset of the RAM buffer.
     */
    void resetPlayhead() {
        m_playhead = 0;
        
        // Clear the RAM buffer so we start fresh from the new playhead position
        MemoryRam::reset();
    }

    /**
     * @brief Appends an audio block to the SD card file.
     * @param block Pointer to the audio block to be written.
     */
    void writeToSd(audio_block_t* block) {
        if (m_shouldClear) return; // Wait until cleared

        if (!m_file) {
            LOG("ERROR: writeToSd failed - File not open (ID: %d)", m_uniqueId);
            return;
        }
        
        // Calculate seek position based on tracked block count to avoid m_file.size() call
        size_t writePos = m_fileSizeInBlocks * (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));

        // Seek to the end of the file to append
        if (!m_file.seek(writePos)) {
            LOG("ERROR: writeToSd failed to seek to pos %d (ID: %d)", writePos, m_uniqueId);
        }
        
        if (m_file.write((uint8_t*)block->data, sizeof(block->data)) == sizeof(block->data)) {
            m_fileSizeInBlocks++;
        } else {
            LOG("ERROR: writeToSd failed to write full block (ID: %d)", m_uniqueId);
        }
    }

    /**
     * @brief Updates the RAM buffer by pulling data from the SD card.
     *        Should be called regularly (e.g., in loop()).
     *        Fills the RAM buffer until it is full or SD data runs out.
     *        Handles looping back to the start of the SD file.
     */
    void update() {
        // Handle deferred clear
        if (m_shouldClear) {
            // Close and recreate the file
            if (m_file) {
                m_file.close();
            }
            
            if (SD.exists(m_binFileName.c_str())) {
                SD.remove(m_binFileName.c_str());
            }
            
            m_file = SD.open(m_binFileName.c_str(), FILE_WRITE);
            if (!m_file) {
                LOG("ERROR: MemorySd clearLoop failed to recreate file: %s", m_binFileName.c_str());
            }
            
            m_shouldClear = false;
            return; // Don't try to read immediately after clear
        }

        if (!m_file) {
             return;
        }
        
        if (m_fileSizeInBlocks == 0) return;

        // Calculate file size in bytes from blocks
        size_t fileSize = m_fileSizeInBlocks * (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));

        // Keep filling until RAM is full
        while (m_storedBlocks < m_sizeInBlocks) {
            // Handle Looping
            if (m_playhead >= fileSize) {
                m_playhead = 0;
            }

            // Create a temporary block to read into
            audio_block_t tempBlock; 

            // Seek and Read
            if (!m_file.seek(m_playhead)) {
                LOG("ERROR: update failed to seek to playhead %d (ID: %d)", m_playhead, m_uniqueId);
                break;
            }
            
            int bytesRead = m_file.read((uint8_t*)tempBlock.data, sizeof(tempBlock.data));

            if (bytesRead > 0) {
                // Attempt to push to RAM
                if (push(&tempBlock)) {
                    m_playhead += bytesRead;
                } else {
                    // RAM became full
                    break;
                }
            } else {
                // Could not read data (EOF or Error)
                if (m_playhead < fileSize) {
                     LOG("ERROR: update read 0 bytes but not at EOF (ID: %d)", m_uniqueId);
                }
                break;
            }
        }
    }

    /**
     * @brief Gets the current size of the SD file in audio blocks.
     * @return The number of audio blocks stored in the file.
     */
    size_t getFileSizeInBlocks() const {
        return m_fileSizeInBlocks;
    }

    /**
     * @brief Static member function to remove all files from the SD card.
     */
    static void removeAllFiles() {
        ensureSdInit();
        
        // Ensure we open the root directory
        File root = SD.open("/");
        if (!root) {
            LOG("ERROR: Failed to open SD root");
            return;
        }

        while (true) {
            File entry = root.openNextFile();
            if (!entry) break;
            
            String name = entry.name();
            // We verify it is not a directory before removing (simple flat file system assumption)
            if (!entry.isDirectory()) {
                entry.close(); // Important: Close the file handle before removing
                if (!SD.remove(name.c_str())) {
                    LOG("ERROR: Failed to remove: %s", name.c_str());
                }
            } else {
                entry.close();
            }
        }
        root.close();
    }

private:
    int m_uniqueId;
    String m_binFileName;
    File m_file;
    size_t m_playhead = 0; // Current read position in the SD file (bytes)
    size_t m_fileSizeInBlocks = 0; // Total number of blocks written to the file
    bool m_shouldClear = false; // Flag to trigger deferred file reset

    // Helper to generate a unique ID for each instance
    static int getNextId() {
        static int idCounter = 0;
        return idCounter++;
    }
    
    // Static helper to ensure SD is initialized exactly once
    static void ensureSdInit() {
        static bool initialized = false;
        if (!initialized) {
            if (!SD.begin(BUILTIN_SDCARD)) {
                LOG("MemorySd: SD Init Failed!");
            } else {
                initialized = true;
                LOG("MemorySd: SD Init OK.");
            }
        }
    }

    void createAndOpenFile() {
        m_binFileName = "track_" + String(m_uniqueId) + ".bin";
        m_fileSizeInBlocks = 0;
        
        // Remove if it already exists to ensure a fresh file
        if (SD.exists(m_binFileName.c_str())) {
            SD.remove(m_binFileName.c_str());
        }
        
        // Create the file and keep it open for read/write
        // FILE_WRITE opens for reading and writing, starting at the end of the file.
        m_file = SD.open(m_binFileName.c_str(), FILE_WRITE);
        
        if (!m_file) {
            LOG("ERROR: MemorySd failed to create file: %s", m_binFileName.c_str());
        }
    }
};

#endif // MEMORY_H
