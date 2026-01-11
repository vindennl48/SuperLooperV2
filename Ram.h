#ifndef RAM_H
#define RAM_H

#include <Arduino.h>
#include <Audio.h>
#include "BALibrary.h"
#include "Definitions.h"

using namespace BALibrary;

class Ram {
public:
  Ram() : 
    mem0(BALibrary::SpiDeviceId::SPI_DEVICE0),
    mem1(BALibrary::SpiDeviceId::SPI_DEVICE1),
    mem0Size(0),
    totalSize(0)
  {}
  
  virtual ~Ram(){}

  void begin() {
    // Configure the hardware definitions first
    SPI_MEM0_64M();
    SPI_MEM1_64M();

    mem0.begin();
    mem1.begin();

    // Retrieve the size of MEM0 to know the boundary
    mem0Size = BALibrary::BAHardwareConfig.getSpiMemSizeBytes(BALibrary::MemSelect::MEM0);
    size_t mem1Size = BALibrary::BAHardwareConfig.getSpiMemSizeBytes(BALibrary::MemSelect::MEM1);
    totalSize = mem0Size + mem1Size;

    LOG("Ram: Initialized. MEM0 Size: %d, MEM1 Size: %d, Total: %d", mem0Size, mem1Size, totalSize);
  }

  // --- 8-bit Access ---

  void write(size_t address, uint8_t data) {
    if (address < mem0Size) {
      mem0.write(address, data);
    } else if (address < totalSize) {
      mem1.write(address - mem0Size, data);
    }
  }

  uint8_t read(size_t address) {
    uint8_t retVal = 0;
    if (address < mem0Size) {
      retVal = mem0.read(address);
    } else if (address < totalSize) {
      retVal = mem1.read(address - mem0Size);
    }
    return retVal;
  }

  void write(size_t address, uint8_t *data, size_t length) {
    if (address >= totalSize) return;

    if (address < mem0Size) {
      size_t availableInMem0 = mem0Size - address;
      if (length <= availableInMem0) {
        // Fits entirely in MEM0
        mem0.write(address, data, length);
      } else {
        // Spans MEM0 and MEM1
        mem0.write(address, data, availableInMem0);
        size_t remaining = length - availableInMem0;
        // Ensure we don't write past the end of MEM1
        if (remaining > totalSize - mem0Size) remaining = totalSize - mem0Size; 
        mem1.write(0, data + availableInMem0, remaining);
      }
    } else {
      // Entirely in MEM1
      mem1.write(address - mem0Size, data, length);
    }
  }

  void read(size_t address, uint8_t *dest, size_t length) {
    if (address >= totalSize) return;

    if (address < mem0Size) {
      size_t availableInMem0 = mem0Size - address;
      if (length <= availableInMem0) {
        // Fits entirely in MEM0
        mem0.read(address, dest, length);
      } else {
        // Spans MEM0 and MEM1
        mem0.read(address, dest, availableInMem0);
        size_t remaining = length - availableInMem0;
        if (remaining > totalSize - mem0Size) remaining = totalSize - mem0Size;
        mem1.read(0, dest + availableInMem0, remaining);
      }
    } else {
      // Entirely in MEM1
      mem1.read(address - mem0Size, dest, length);
    }
  }

  // --- 16-bit Access ---

  void write16(size_t address, int16_t data) {
    // Address is now a WORD index (sample number)
    size_t byteAddress = address * 2;
    
    if (byteAddress < mem0Size) {
      mem0.write16(byteAddress, (uint16_t)data);
    } else if (byteAddress < totalSize) {
      mem1.write16(byteAddress - mem0Size, (uint16_t)data);
    }
  }

  int16_t read16(size_t address) {
    // Address is now a WORD index (sample number)
    size_t byteAddress = address * 2;
    int16_t retVal = 0;
    
    if (byteAddress < mem0Size) {
      retVal = (int16_t)mem0.read16(byteAddress);
    } else if (byteAddress < totalSize) {
      retVal = (int16_t)mem1.read16(byteAddress - mem0Size);
    }
    return retVal;
  }

  void write16(size_t address, int16_t *data, size_t length) {
    // Address and Length are in WORDS (16-bit)
    size_t mem0SizeWords = mem0Size / 2;
    size_t totalSizeWords = totalSize / 2;

    if (address >= totalSizeWords) return;

    if (address < mem0SizeWords) {
      size_t availableInMem0Words = mem0SizeWords - address;

      if (length <= availableInMem0Words) {
        mem0.write16(address * 2, (uint16_t*)data, length);
      } else {
        mem0.write16(address * 2, (uint16_t*)data, availableInMem0Words);
        size_t remainingWords = length - availableInMem0Words;
        // Check bounds for MEM1
        size_t mem1SizeWords = totalSizeWords - mem0SizeWords;
        if (remainingWords > mem1SizeWords) remainingWords = mem1SizeWords;
        mem1.write16(0, (uint16_t*)(data + availableInMem0Words), remainingWords);
      }
    } else {
      mem1.write16((address - mem0SizeWords) * 2, (uint16_t*)data, length);
    }
  }

  void read16(size_t address, int16_t *dest, size_t length) {
    // Address and Length are in WORDS
    size_t mem0SizeWords = mem0Size / 2;
    size_t totalSizeWords = totalSize / 2;

    if (address >= totalSizeWords) return;

    if (address < mem0SizeWords) {
      size_t availableInMem0Words = mem0SizeWords - address;

      if (length <= availableInMem0Words) {
        mem0.read16(address * 2, (uint16_t*)dest, length);
      } else {
        mem0.read16(address * 2, (uint16_t*)dest, availableInMem0Words);
        size_t remainingWords = length - availableInMem0Words;
        size_t mem1SizeWords = totalSizeWords - mem0SizeWords;
        if (remainingWords > mem1SizeWords) remainingWords = mem1SizeWords;
        mem1.read16(0, (uint16_t*)(dest + availableInMem0Words), remainingWords);
      }
    } else {
      mem1.read16((address - mem0SizeWords) * 2, (uint16_t*)dest, length);
    }
  }

private:
  BALibrary::BASpiMemory mem0;
  BALibrary::BASpiMemory mem1;
  size_t mem0Size;
  size_t totalSize;
};


#endif
