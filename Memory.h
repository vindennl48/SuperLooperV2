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
// A Ring Buffer wrapper around the external memory slots.
// Handles circular wrapping and interrupt safety.
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
    // Persistent memory manager handles cleanup usually.
  }

  bool push(audio_block_t* block) {
    return push(block->data);
  }

  // Thread-safe Push
  bool push(int16_t* data) {
    __disable_irq(); // Begin Critical Section

    if (m_storedBlocks >= m_sizeInBlocks) {
      __enable_irq();
      return false;
    }

    // Force Position Update: Ensures chip address is correct even if shared
    // Assuming setWritePosition takes BYTES. 
    size_t offset = m_writeHead * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
    m_memSlot.setWritePosition(offset);

    bool success = m_memSlot.writeAdvance16(data, AUDIO_BLOCK_SAMPLES);

    if (success) {
      m_storedBlocks++;
      m_writeHead++;
      if (m_writeHead >= m_sizeInBlocks) m_writeHead = 0;
    }

    __enable_irq(); // End Critical Section
    return success;
  }

  bool pop(audio_block_t* block) {
    return popInternal(block->data);
  }

  // New helper to pop data into a raw buffer (for writing to SD)
  bool popToBuffer(int16_t* buffer) {
    return popInternal(buffer);
  }

  // Thread-safe Reset
  void reset() {
    __disable_irq();
    m_memSlot.setWritePosition(0);
    m_memSlot.setReadPosition(0);
    m_storedBlocks = 0;
    m_writeHead = 0;
    m_readHead = 0;
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
  size_t m_writeHead = 0;
  size_t m_readHead = 0;

  // Helper for popping to ensure consistent logic
  bool popInternal(int16_t* dst) {
    __disable_irq(); // Begin Critical Section

    if (m_storedBlocks == 0) {
      __enable_irq();
      return false;
    }

    // Force Position Update
    size_t offset = m_readHead * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
    m_memSlot.setReadPosition(offset);

    bool success = m_memSlot.readAdvance16(dst, AUDIO_BLOCK_SAMPLES);

    if (success) {
      m_storedBlocks--;
      m_readHead++;
      if (m_readHead >= m_sizeInBlocks) m_readHead = 0;
    }

    __enable_irq(); // End Critical Section
    return success;
  }

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
  static inline size_t s_usageMEM0 = 0;
  static inline size_t s_usageMEM1 = 0;
  static const size_t MAX_BLOCKS_PER_CHIP = 32768; // 8MB / 256 bytes

  MemorySd(size_t bufferSizeBlocks) 
  {
    ensureSdInit();
    m_uniqueId = getNextId();

    size_t needed = bufferSizeBlocks * 2; // Input + Output buffers
    int chipIndex = 0;

    if (s_usageMEM0 + needed <= MAX_BLOCKS_PER_CHIP) {
      chipIndex = 0;
      s_usageMEM0 += needed;
    } else if (s_usageMEM1 + needed <= MAX_BLOCKS_PER_CHIP) {
      chipIndex = 1;
      s_usageMEM1 += needed;
    } else {
      LOG("ERROR: Out of Memory for Track %d", m_uniqueId);
      chipIndex = 0; // Fallback
    }

    m_assignedChipIndex = chipIndex;
    m_assignedSizeBlocks = needed;

    LOG("Allocating Track %d on MEM%d (Usage: %d/%d blocks)", m_uniqueId, chipIndex, (chipIndex==0 ? s_usageMEM0 : s_usageMEM1), MAX_BLOCKS_PER_CHIP);

    // Allocate Input and Output buffers
    m_inputBuffer = new MemoryRam(chipIndex, bufferSizeBlocks);
    m_outputBuffer = new MemoryRam(chipIndex, bufferSizeBlocks);

    createAndOpenFile();
  }

  ~MemorySd() {
    if (m_inputBuffer) delete m_inputBuffer;
    if (m_outputBuffer) delete m_outputBuffer;
    if (m_file) m_file.close();

    // Return memory to the pool
    if (m_assignedChipIndex == 0) {
      if (s_usageMEM0 >= m_assignedSizeBlocks) s_usageMEM0 -= m_assignedSizeBlocks;
      else s_usageMEM0 = 0;
    } else {
      if (s_usageMEM1 >= m_assignedSizeBlocks) s_usageMEM1 -= m_assignedSizeBlocks;
      else s_usageMEM1 = 0;
    }
    LOG("Freed Track %d from MEM%d", m_uniqueId, m_assignedChipIndex);
  }

  // --- Audio Thread Interface (ISR Safe via MemoryRam) ---

  void writeSample(audio_block_t* block) {
    if (m_inputBuffer) m_inputBuffer->push(block);
  }

  bool readSample(audio_block_t* block) {
    if (m_outputBuffer) return m_outputBuffer->pop(block);
    return false;
  }

  // --- Main Loop Interface (Maintenance) ---

  void update() {
    if (m_shouldClear) {
      performClear();
      return;
    }

    if (!m_file) return;

    // 1. FLUSH INPUT: Move data from Input RAM -> SD
    if (m_inputBuffer && m_inputBuffer->getStoredBlocks() > 0) {
      flushInputToSd();
    }

    // 2. CHECK FLUSH COMPLETION
    if (m_waitingForFlush) {
      if (m_inputBuffer && m_inputBuffer->getStoredBlocks() == 0) {
        m_isLoopClosed = true;
        m_waitingForFlush = false;
        LOG("MemorySd: Loop Closed. Total Blocks: %d", m_fileSizeInBlocks);
      }
    }

    // 3. REFILL OUTPUT: Move data from SD -> Output RAM
    // Only if file has data
    if (m_outputBuffer && m_fileSizeInBlocks > 0) {
      // Handle Looping logic at file level
      size_t fileSizeBytes = m_fileSizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
      if (m_readCursor >= fileSizeBytes) {
        if (m_isLoopClosed) {
          m_readCursor = 0;
        } else {
          // Do nothing here, fetchSdToOutput will break
        }
      }

      if (m_outputBuffer->getSpaceBlocks() > 0) {
        fetchSdToOutput();
      }
    }
  }

  void clearLoop() {
    if (m_inputBuffer) m_inputBuffer->reset();
    if (m_outputBuffer) m_outputBuffer->reset();

    m_readCursor = 0;
    m_writeCursor = 0;
    m_fileSizeInBlocks = 0;

    m_shouldClear = true; // Defer file operations to update()
  }

  void restartPlayback() {
    m_readCursor = 0;
    if (m_outputBuffer) m_outputBuffer->reset();
  }

  void finishRecording() { 
    m_waitingForFlush = true; 
    LOG("MemorySd: Finish Recording requested. Waiting for flush...");
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
        // Careful: only remove our track files if strict, but instructions said "remove all"
        if (name.startsWith("track_")) {
          SD.remove(name.c_str());
        }
      } else {
        entry.close();
      }
    }
    root.close();
    LOG("Removed temp SD Files");
  }

private:
  int m_uniqueId;
  String m_binFileName;
  File m_file;

  int m_assignedChipIndex = 0;
  size_t m_assignedSizeBlocks = 0;

  MemoryRam* m_inputBuffer = nullptr;
  MemoryRam* m_outputBuffer = nullptr;

  // File Cursors (in bytes)
  size_t m_readCursor = 0;
  size_t m_writeCursor = 0;
  size_t m_fileSizeInBlocks = 0;

  bool m_shouldClear = false;
  bool m_isLoopClosed = false;
  bool m_waitingForFlush = false;

  // Buffering constants
  static const int BATCH_SIZE = 32; // 8KB buffer for optimal SD writes
  int16_t m_tempBuffer[AUDIO_BLOCK_SAMPLES * BATCH_SIZE];

  void flushInputToSd() {
    while (m_inputBuffer->getStoredBlocks() > 0) {
      size_t available = m_inputBuffer->getStoredBlocks();
      size_t toWrite = (available > BATCH_SIZE) ? BATCH_SIZE : available;

      if (toWrite == 0) break;

      if (!m_file.seek(m_writeCursor)) {
        LOG("Err: seek write failed");
        break;
      }

      // Pop from RAM into Temp Buffer (IRQ protected inside popToBuffer)
      for (size_t i = 0; i < toWrite; i++) {
        m_inputBuffer->popToBuffer(&m_tempBuffer[i * AUDIO_BLOCK_SAMPLES]);
      }

      // Write to SD (Blocking, IRQs enabled)
      size_t bytesToWrite = toWrite * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
      size_t written = m_file.write((uint8_t*)m_tempBuffer, bytesToWrite);

      m_writeCursor += written;
      m_fileSizeInBlocks += (written / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t)));

      if (written < bytesToWrite) break;
    }
  }

  void fetchSdToOutput() {
    while (m_outputBuffer->getSpaceBlocks() > 0) {
      size_t fileSizeBytes = m_fileSizeInBlocks * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);

      // Handle Looping Logic
      if (m_readCursor >= fileSizeBytes) {
        if (m_isLoopClosed) {
          m_readCursor = 0; // Wrap around if loop is finalized
        } else {
          break; // Wait for more data if still recording/flushing
        }
      }

      size_t bytesRemaining = fileSizeBytes - m_readCursor;
      size_t blocksRemaining = bytesRemaining / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));

      if (blocksRemaining == 0) break;

      size_t space = m_outputBuffer->getSpaceBlocks();
      size_t toRead = (space > BATCH_SIZE) ? BATCH_SIZE : space;

      if (toRead > blocksRemaining) toRead = blocksRemaining;
      if (toRead == 0) break;

      if (!m_file.seek(m_readCursor)) break;

      // Read from SD (Blocking)
      size_t bytesToRead = toRead * AUDIO_BLOCK_SAMPLES * sizeof(int16_t);
      size_t read = m_file.read((uint8_t*)m_tempBuffer, bytesToRead);

      if (read > 0) {
        size_t blocksRead = read / (AUDIO_BLOCK_SAMPLES * sizeof(int16_t));
        // Push to Output RAM (IRQ protected inside push)
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
    m_isLoopClosed = false;
    m_waitingForFlush = false;
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
