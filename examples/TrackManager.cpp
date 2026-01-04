#include "TrackManager.h"

TrackManager::TrackManager() : writeBufferWriteHead(0), writeBufferReadHead(0) {
  // Constructor
}

bool TrackManager::init() {
  // 1. Initialize SD Card
  if (!SD.begin(SD_CS_PIN)) {
    LOG("TrackManager: SD Init Failed!");
    return false;
  } else {
    LOG("TrackManager: SD Init OK.");
  }

  // Hardware balancing: 5 slots on MEM0, 5 on MEM1
  const int slotsPerBank = NUM_BUFFER_SLOTS / 2;
  const uint32_t sizeBytes = SAMPLES_TO_BYTES(BUFFER_SLOT_SIZE_SAMPLES);

  // 2. Initialize Playback Tracks (1-9)
  for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
    eraseTrack(i);

    Track& track = m_tracks[i];

    // Balancing: Indices 0-4 -> MEM0, 5-8 -> MEM1
    MemSelect bank = (i < slotsPerBank) ? MEM0 : MEM1;

    if (!m_sramManager.requestMemory(&track.sramSlot, (size_t)sizeBytes, bank, false)) {
      LOG("TrackManager: Failed SRAM for Track %d on Bank %d", i + 1, bank);
      return false;
    }

    // Open or Create files and keep them open for the duration of the session
    char filename[16];
    snprintf(filename, sizeof(filename), "TRACK%d.BIN", i + 1);
    
    track.file = SD.open(filename, (uint8_t)(O_RDWR | O_CREAT));
    if (!track.file) {
      LOG("TrackManager: Failed to open %s", filename);
      return false;
    }
    
    LOG("TrackManager: Track %d Init (Bank %d, %s - OPEN)", i + 1, bank, filename);
  }

  // 3. Initialize Write Buffer (The 10th slot)
  if (!m_sramManager.requestMemory(&m_writeBuffer.sramSlot, (size_t)sizeBytes, MEM1, false)) {
    LOG("TrackManager: Failed SRAM for Write Buffer on Bank 1");
    return false;
  }

  LOG("TrackManager: Write Buffer Init (Bank 1, SRAM-only)");

  return true;
}

void TrackManager::startRecording(int trackIndex) {
  eraseTrack(trackIndex);

  track->state = Track::RECORDING;

  // Reset Global Write Buffer
  writeBufferWriteHead = 0;
  writeBufferReadHead = 0;

  LOG("TrackManager: Started Recording Track %d", trackIndex + 1);
}

void TrackManager::stopRecording(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track || track->state != Track::RECORDING) return;

  // Non-blocking stop: Transition to FINISHING_RECORD.
  // The background update() loop will handle flushing the buffer 
  // and finalizing the transition to PLAYING.
  track->state = Track::FINISHING_RECORD;
  
  LOG("TrackManager: Track %d Stopping... (State: FINISHING_RECORD)", trackIndex + 1);
}

void TrackManager::playTrack(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return;
  
  if (track->state == Track::STOPPED || track->state == Track::MUTED) {
    track->state = Track::FADING_IN;
    track->nextState = Track::PLAYING; // Ideally goes here after fade
    track->fadeSamplesRemaining = FADE_SAMPLES;
    // Calculate step to reach 1.0 from currentGain
    track->fadeStep = (1.0f - track->currentGain) / (float)FADE_SAMPLES;
  }
}

void TrackManager::stopTrack(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return;
  
  if (track->state == Track::PLAYING || track->state == Track::MUTED || track->state == Track::FADING_IN) {
    track->state = Track::FADING_OUT;
    track->nextState = Track::PRIMING;
    track->primingResetNeeded = true;
    track->fadeSamplesRemaining = FADE_SAMPLES;
    // Calculate step to reach 0.0 from currentGain
    track->fadeStep = track->currentGain / (float)FADE_SAMPLES;
  }
}

void TrackManager::muteTrack(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return;
  
  if (track->state == Track::PLAYING || track->state == Track::FADING_IN) {
    track->state = Track::FADING_OUT;
    track->nextState = Track::MUTED;
    track->fadeSamplesRemaining = FADE_SAMPLES;
    // Calculate step to reach 0.0 from currentGain
    track->fadeStep = track->currentGain / (float)FADE_SAMPLES;
  }
}

void TrackManager::unmuteTrack(int trackIndex) {
  // Alias for play, as logic is identical (fade in from whatever gain)
  playTrack(trackIndex);
}

bool TrackManager::isTrackEmpty(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return true;
  return track->state == Track::EMPTY || track->state == Track::RECORDING ? true : false;
}

uint32_t TrackManager::getTrackLoopLength(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return true;
  return track->loopLengthSamples;
}

void TrackManager::eraseTrack(int trackIndex) {
  Track* track = getTrack(trackIndex);
  if (!track) return;

  track->state = Track::EMPTY;
  track->ramReadHead = 0;
  track->ramWriteHead = 0;
  track->ringBufferFull = false;
  track->sdReadPosition = 0;
  track->loopLengthSamples = 0;
  
  track->currentGain = 0.0f;
  track->fadeStep = 0.0f;
  track->fadeSamplesRemaining = 0;
  track->nextState = Track::STOPPED;
  track->primingResetNeeded = false;

  if (track->file) track->file->seek(0);
}

void TrackManager::update() {
  int16_t transferBuffer[BLOCK_SIZE_SAMPLES];

  for (int i = 0; i < NUM_AUDIO_TRACKS; i++) {
    Track& track = m_tracks[i];

    switch (track.state) {
      case Track::RECORDING:
      case Track::FINISHING_RECORD: {
        // --- RECORDING / FLUSHING: SRAM Write Buffer -> SD Card ---
        int32_t availableSamples = 0;
        if (writeBufferWriteHead >= writeBufferReadHead) {
          availableSamples = writeBufferWriteHead - writeBufferReadHead;
        } else {
          availableSamples = (BUFFER_SLOT_SIZE_SAMPLES - writeBufferReadHead) + writeBufferWriteHead;
        }

        // If FLUSHING, we write even if less than BLOCK_SIZE (if it's the last chunk)
        // But for simplicity, we stick to block logic until the very end?
        // No, we must flush EVERYTHING to transition.
        
        bool flushing = (track.state == Track::FINISHING_RECORD);
        int threshold = flushing ? 0 : BLOCK_SIZE_SAMPLES; 

        if (availableSamples > threshold) {
          // Determine how much to write. In flushing mode, we might write odd amounts?
          // SD writes are efficient in blocks. 
          // Let's stick to blocks, but if flushing and < block, we pad or just write bytes?
          // file.write accepts bytes.
          
          // Simplified: Just write whatever is there if flushing.
          // But keeping block alignment is good for SD performance.
          // Since audio is 16-bit, multiple of 2 is mandatory.
          
          int samplesToWrite = availableSamples;
          if (!flushing && samplesToWrite > BLOCK_SIZE_SAMPLES) samplesToWrite = BLOCK_SIZE_SAMPLES;
          
          // Cap to BLOCK_SIZE for one loop iteration to avoid blocking too long
          if (samplesToWrite > BLOCK_SIZE_SAMPLES) samplesToWrite = BLOCK_SIZE_SAMPLES;

          if (samplesToWrite > 0) {
            uint32_t samplesToEnd = BUFFER_SLOT_SIZE_SAMPLES - writeBufferReadHead;
            
            if (samplesToEnd >= (uint32_t)samplesToWrite) {
              m_writeBuffer.sramSlot.read16(writeBufferReadHead, transferBuffer, samplesToWrite);
              track.file.write((uint8_t*)transferBuffer, samplesToWrite * 2);
              writeBufferReadHead += samplesToWrite;
            } else {
              uint32_t firstChunk = samplesToEnd;
              uint32_t secondChunk = samplesToWrite - firstChunk;
              m_writeBuffer.sramSlot.read16(writeBufferReadHead, transferBuffer, firstChunk);
              m_writeBuffer.sramSlot.read16(0, transferBuffer + firstChunk, secondChunk);
              track.file.write((uint8_t*)transferBuffer, samplesToWrite * 2);
              writeBufferReadHead = secondChunk;
            }
            
            if (writeBufferReadHead >= BUFFER_SLOT_SIZE_SAMPLES) writeBufferReadHead = 0;
          }
        }

        // Check if done flushing
        if (flushing && writeBufferReadHead == writeBufferWriteHead) {
          // Buffer is empty. Finalize.
          track.loopLengthSamples = track.file.position() / 2;
          track.file.flush();
          track.sdReadPosition = track.ramWriteHead;
          
          // Transition to PLAYING via FADING_IN to prevent clicks
          track.state = Track::FADING_IN;
          track.nextState = Track::PLAYING;
          track.currentGain = 0.0f;
          track.fadeSamplesRemaining = FADE_SAMPLES;
          track.fadeStep = 1.0f / (float)FADE_SAMPLES;
          
          LOG("TrackManager: Track %d Flushing Complete. Fading In. Len: %u", i + 1, track.loopLengthSamples);
        }
        break;
      }

      case Track::PLAYING:
      case Track::MUTED:
      case Track::FADING_IN:
      case Track::FADING_OUT:
      case Track::PRIMING: {
        // --- PRIMING Logic: Reset and Fill ---
        if (track.state == Track::PRIMING) {
          if (track.primingResetNeeded) {
            track.file.seek(0);
            track.sdReadPosition = 0;
            track.ramReadHead = 0;
            track.ramWriteHead = 0; // Reset write head
            track.primingResetNeeded = false;
            // LOG("TrackManager: Track %d Priming Reset", i + 1);
          }
        }
        
        // --- SD Card -> SRAM Ring Buffer (Common for Play, Mute, Fade, Prime) ---
        int32_t occupied = 0;
        if (track.ramWriteHead >= track.ramReadHead) {
          occupied = track.ramWriteHead - track.ramReadHead;
        } else {
          occupied = (BUFFER_SLOT_SIZE_SAMPLES - track.ramReadHead) + track.ramWriteHead;
        }
        
        int32_t freeSpace = BUFFER_SLOT_SIZE_SAMPLES - occupied - 1;

        // Only read if we have space for a block
        if (freeSpace >= BLOCK_SIZE_SAMPLES) {
          uint32_t samplesToRead = BLOCK_SIZE_SAMPLES;
          uint32_t samplesUntilEnd = track.loopLengthSamples - track.sdReadPosition;

          if (samplesUntilEnd < samplesToRead) {
            // --- Split Read (Wrap Around) ---
            // We are near the end of the loop. Read the tail, then wrap to start.
            uint32_t part1 = samplesUntilEnd;
            uint32_t part2 = samplesToRead - part1;

            // Read Part 1 (Tail)
            if (part1 > 0) {
              track.file.seek(track.sdReadPosition * 2);
              track.file.read((uint8_t*)transferBuffer, part1 * 2);
            }

            // Read Part 2 (Head)
            if (part2 > 0) {
              track.file.seek(0);
              track.file.read((uint8_t*)transferBuffer + (part1 * 2), part2 * 2);
            }

            // Update Position: We represent the position *after* the wrap
            track.sdReadPosition = part2;

          } else {
            // --- Normal Read ---
            track.file.seek(track.sdReadPosition * 2);
            track.file.read((uint8_t*)transferBuffer, samplesToRead * 2);
            track.sdReadPosition += samplesToRead;
            
            // Edge case: If we landed exactly on the end
            if (track.sdReadPosition >= track.loopLengthSamples) {
              track.sdReadPosition = 0;
            }
          }
          
          // We always have a full buffer now (assuming loop > block size)
          int samplesRead = samplesToRead;

          if (samplesRead > 0) {
            uint32_t samplesToEnd = BUFFER_SLOT_SIZE_SAMPLES - track.ramWriteHead;
            
            if (samplesToEnd >= (uint32_t)samplesRead) {
              track.sramSlot.write16(track.ramWriteHead, transferBuffer, samplesRead);
              track.ramWriteHead += samplesRead;
            } else {
              uint32_t firstChunk = samplesToEnd;
              uint32_t secondChunk = samplesRead - firstChunk;
              track.sramSlot.write16(track.ramWriteHead, transferBuffer, firstChunk);
              track.sramSlot.write16(0, transferBuffer + firstChunk, secondChunk);
              track.ramWriteHead = secondChunk;
            }
            
            if (track.ramWriteHead >= BUFFER_SLOT_SIZE_SAMPLES) track.ramWriteHead = 0;
          }
          
          // --- End of Priming Check ---
          if (track.state == Track::PRIMING) {
            // Check if buffer is sufficiently full. 
            // Simple check: If occupied space is now near full (e.g. > 75%) or we hit end of a short loop
            int32_t newOccupied;
            if (track.ramWriteHead >= track.ramReadHead) newOccupied = track.ramWriteHead - track.ramReadHead;
            else newOccupied = (BUFFER_SLOT_SIZE_SAMPLES - track.ramReadHead) + track.ramWriteHead;
            
            // If buffer is mostly full OR we have loaded the entire file (short loop)
            if ((uint32_t)newOccupied > (BUFFER_SLOT_SIZE_SAMPLES - BLOCK_SIZE_SAMPLES * 2) || 
                (track.loopLengthSamples < BUFFER_SLOT_SIZE_SAMPLES && (uint32_t)newOccupied >= track.loopLengthSamples)) {
                
                track.state = Track::STOPPED;
                // LOG("TrackManager: Track %d Primed and Stopped.", i + 1);
            }
          }
        }
        break;
      }

      default:
        break;
    }
  }
}

void TrackManager::pushToRecord(int trackIndex, int16_t sample) {
  Track* track = getTrack(trackIndex);
  // Strict check: Only record in RECORDING state.
  // If state is FINISHING_RECORD, we stop accepting new input.
  if (!track || track->state != Track::RECORDING) return;

  m_writeBuffer.sramSlot.write16(writeBufferWriteHead, &sample, 1);
  writeBufferWriteHead++;
  if (writeBufferWriteHead >= BUFFER_SLOT_SIZE_SAMPLES) writeBufferWriteHead = 0;

  if (!track->ringBufferFull) {
    track->sramSlot.write16(track->ramWriteHead, &sample, 1);
    track->ramWriteHead++;
    
    if (track->ramWriteHead >= BUFFER_SLOT_SIZE_SAMPLES - 1) {
      track->ringBufferFull = true;
    }
  }
}

int16_t TrackManager::pullForPlayback(int trackIndex) {
  Track* track = getTrack(trackIndex);
  // Allow playback during FINISHING_RECORD, PLAYING, FADING, MUTED
  if (!track) return 0;

  if (track->state == Track::STOPPED || track->state == Track::PRIMING || track->state == Track::EMPTY || track->state == Track::RECORDING) {
    return 0;
  }

  int16_t sample = 0;
  track->sramSlot.read16(track->ramReadHead, &sample, 1);
  
  // Increment Read Head (Advance buffer)
  track->ramReadHead++;
  if (track->ramReadHead >= BUFFER_SLOT_SIZE_SAMPLES) {
    track->ramReadHead = 0;
  }

  // Process State Logic (Gain & Transitions)
  if (track->state == Track::MUTED) {
    return 0; // Buffer advanced, but audio silenced
  } 
  else if (track->state == Track::PLAYING || track->state == Track::FINISHING_RECORD) {
    return sample; // Full volume (or add master volume here later)
  }
  else if (track->state == Track::FADING_IN) {
    float out = (float)sample * track->currentGain;
    track->currentGain += track->fadeStep;
    
    if (track->fadeSamplesRemaining > 0) track->fadeSamplesRemaining--;
    
    if (track->fadeSamplesRemaining == 0 || track->currentGain >= 1.0f) {
      track->currentGain = 1.0f;
      track->state = Track::PLAYING;
    }
    return (int16_t)out;
  }
  else if (track->state == Track::FADING_OUT) {
    float out = (float)sample * track->currentGain;
    track->currentGain -= track->fadeStep;

    if (track->fadeSamplesRemaining > 0) track->fadeSamplesRemaining--;

    if (track->fadeSamplesRemaining == 0 || track->currentGain <= 0.0f) {
      track->currentGain = 0.0f;
      track->state = track->nextState;
      // Note: If transitioning to PRIMING, update() handles the reset.
    }
    return (int16_t)out;
  }

  return 0;
}

Track* TrackManager::getTrack(int index) {
  if (index < 0 || index >= NUM_AUDIO_TRACKS) {
    return nullptr;
  }
  return &m_tracks[index];
}

Track* TrackManager::getWriteBuffer() {
  return &m_writeBuffer;
}
