# SuperLooper Project Goals: 8-Track Parallel Looper (SD + 9-Slot Architecture)

## 1. Core Concept
Build a synchronized, multi-track looper pedal where up to **8 independent loop tracks** play simultaneously. The system uses a **Hybrid SD/PSRAM** architecture to support infinite loop lengths with zero audio latency.

### Architecture
*   **Tracks:** 8 Independent Playback Tracks.
*   **Memory Strategy (Double-Buffered):** The 16MB PSRAM is split into **9 Slots** of ~1.5 MB each (~17 seconds).
    *   **8 x Read Buffers:** One dedicated playback cache for each track.
    *   **1 x Write Buffer:** A shared "stream buffer" for recording new audio.
*   **Physical Mapping:**
    *   **MEM0 (8MB):** Read Buffers 1-4 + Global Write Buffer.
    *   **MEM1 (8MB):** Read Buffers 5-8.
*   **Storage Backend:** SD Card files (`TRACK1.BIN` ... `TRACK8.BIN`).

## 2. Looper Lifecycle

### A. Initialization
*   **Boot:** Initialize SD Card and allocate 9 PSRAM slots.
*   **Clear All:** Reset all tracks, truncate files, reset pointers. Done in main loop to avoid audio glitches.

### B. Track 1 (Master) Logic
*   **Recording:**
    *   **Audio Engine:** Writes audio to **Read Buffer 1** (up to 1.5MB) AND **Global Write Buffer**.
    *   **Background:** MemoryManager flushes **Global Write Buffer** to `TRACK1.BIN`.
*   **Completion:**
    *   Instant transition to `PLAY`.
    *   **Read Buffer 1** is already populated with the start of the loop -> Instant playback.
    *   If loop > 17s, Background Manager begins pre-loading subsequent chunks from SD into **Read Buffer 1**.

### C. Tracks 2-8 (Slaves) Logic
*   **Quantized Start:** Wait for Master Loop to wrap to 0.
*   **Recording:**
    *   **Audio Engine:** Writes audio to **Read Buffer N** (up to 1.5MB) AND **Global Write Buffer**.
    *   **Background:** MemoryManager flushes **Global Write Buffer** to `TRACKn.BIN`.
*   **Completion:**
    *   Instant transition to `PLAY`.
    *   **Read Buffer N** is ready. No copying required.

### D. Overdub Logic (Streaming)
*   **Audio Engine:**
    *   Reads `Previous Take` from **Read Buffer N**.
    *   Mixes `Input + Previous Take`.
    *   Writes result to **Global Write Buffer**.
*   **Background:**
    *   Pre-loads `Previous Take` from SD -> **Read Buffer N** (Read-Ahead).
    *   Flushes **Global Write Buffer** -> `TRACKn_NEXT.BIN` (Write-Behind).
*   **Completion:**
    *   Rename `TRACKn_NEXT.BIN` -> `TRACKn.BIN`.
    *   Swap logic ensures next playback uses new file.

## 3. Control Mapping
*   **Footswitch 1:**
    *   **Press:** Trigger Action (Record/Play/Overdub) immediately.
    *   **Hold (500ms):** Stop & Clear All.
*   **Pot 1 (Loop Depth):** Controls how many tracks are audible.
    *   **CCW (0):** All tracks.
    *   **CW (1.0):** Master only.
    *   **Destructive Record:** Recording while tracks are muted overwrites the oldest muted track.

## 4. Technical Requirements
*   **Zero Latency:** No SD card calls inside Audio Interrupt.
*   **Pop Prevention:** Micro-fades (10ms) on loop seams and mute transitions.
*   **Safety:** Read/Write pointers are decoupled (Physical RAM separation) to prevent "Duplicate Tail" glitches.
