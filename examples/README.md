# SuperLooper - 8-Track Parallel Looper

SuperLooper is a high-performance, multi-track audio looper designed for the **Teensy 4.1** microcontroller and the **BlackAddr TGA Pro MKII** audio shield. It features 8 synchronized parallel tracks, quantization, and unique "history branching" workflow controls.

## Features

*   **8 Parallel Tracks:** Record up to 8 independent audio loops that play back simultaneously.
*   **Master Synchronization:** The first track sets the master loop length. All subsequent tracks are quantized (locked) to this length for perfect sync.
*   **Dual-Mode Recording:**
    *   **First Pass:** Destructive Overwrite (ensures clean capture).
    *   **Subsequent Passes:** Additive Overdub (layering).
*   **Loop Depth Control (Undo/Redo):** Use Potentiometer 1 to progressively mute tracks from newest to oldest. Recording while tracks are muted destructively overwrites them, creating a new "branch" of history.
*   **Zero-Latency Triggering:** Actions trigger immediately on footswitch press, not release.
*   **Click-Free Audio:** 10ms micro-fades applied at loop seams and during mute/unmute transitions.
*   **Massive Storage:** Utilizes 16MB of external PSRAM (8 slots x 2MB each) for ~23 seconds of recording time per track at 44.1kHz 16-bit.

## Hardware Requirements

*   **Teensy 4.1:** Microcontroller.
*   **PSRAM:** 2x 8MB (64Mbit) PSRAM chips soldered to the Teensy 4.1 bottom pads.
*   **BlackAddr TGA Pro MKII:** Audio codec and breakout board.
*   **Footswitch:** Connected to `BA_EXPAND_SW1_PIN`.
*   **Potentiometer:** Connected to `BA_EXPAND_POT1_PIN`.
*   **LED:** Connected to `BA_EXPAND_LED1_PIN` (Red).

## Controls

### Footswitch 1
*   **Tap (Idle):** Start Recording Track 1 (Master).
*   **Tap (Recording):** Stop Recording -> Play.
*   **Tap (Playing):** Arm Recording for next empty track (waits for loop start).
*   **Hold (500ms):** Stop & Clear All (Fade out).

### Potentiometer 1 (Loop Depth)
*   **Full Counter-Clockwise (Left):** All active tracks are audible.
*   **Turn Clockwise (Right):** Progressively mutes tracks from Newest -> Oldest.
    *   *Example:* Turning right might mute Track 3, then Track 2.
    *   **Master Track (Track 1) is never muted.**
*   **Destructive Record:** If you mute tracks (e.g., mute 3 & 4) and press Record, the looper will overwrite Track 3, permanently deleting the old Track 3 & 4.

### LED Status
*   **Solid Red:** Recording.
*   **Fast Blink:** Armed (Waiting for Loop Start or Loop End).
*   **Off:** Idle or Playing.

## Architecture

The system uses a custom `AudioLooper` class inheriting from the Teensy `AudioStream`.
*   **Memory:** 16MB PSRAM split into 8 fixed slots (2MB each).
*   **Audio Engine:** Sums active tracks into the output buffer with software limiting.
*   **State Machine:** Implements `IDLE`, `RECORD_MASTER`, `PLAY`, `ARM_RECORD`, `RECORD_SLAVE`, `ARM_STOP`.

## Building

1.  Install **Arduino IDE** and **Teensyduino**.
2.  Install the **BALibrary** (BlackAddr Audio Library).
    *   *Note:* Ensure `BASpiMemory.cpp` in the library is patched to support `SPI1` on Teensy 4.1 if using external SPI chips, or ensure your PSRAM is correctly mapped if using QSPI.
3.  Open `SuperLooper.ino`.
4.  Select **Teensy 4.1** board.
5.  Compile and Upload.

## License

MIT License.
