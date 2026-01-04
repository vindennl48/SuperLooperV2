# SuperLooperV2

SuperLooperV2 is a powerful and flexible guitar looping pedal built on the Teensy 4.1 platform. This project aims to provide a feature-rich and intuitive looping experience for musicians, with a focus on creative flexibility and performance.

## Features

- **Multi-Track Looping:** Create complex and layered compositions with multiple independent loops. The number of tracks is configurable in `Definitions.h`.
- **Seamless Overdubbing:** Easily record new layers on top of existing loops.
- **Quantized Loops:** Ensure your loops are perfectly timed with the built-in quantization feature. The first loop sets the tempo for all subsequent loops.
- **Branching:** Create alternative versions of your loops by recording over muted tracks. This allows for creating different song sections or experimenting with different ideas without losing your original loop.
- **Global Timeline and Playhead:** A global timeline and playhead keep all your loops in sync, allowing for complex and intricate arrangements.
- **USB Audio:** Connect the looper to your computer via USB and record your loops directly into your DAW.
- **SD Card and External RAM:** The looper uses a high-speed SD card and two external RAM chips for storing and accessing your loops.
- **Intuitive Controls:** The looper is controlled by two footswitches and a rotary pot, making it easy to use in a live performance setting.
- **LED Feedback:** An LED provides visual feedback on the looper's status.

## Hardware

The SuperLooperV2 is built around the Teensy 4.1 and a custom prototype board. The hardware includes:

- 3x Rotary Pots
- 2x Footswitches
- 2x LEDs
- 2x 64Mb RAM chips (MEM0 and MEM1)
- 1x 64GB SD card

## Software

The SuperLooperV2 firmware is written in C++ and uses the Teensy Audio Library. The code is organized into several classes, each responsible for a specific part of the looper's functionality.

### Classes

- **`SuperLooperV2.ino`:** The main sketch file. This is where the `setup()` and `loop()` functions are located.
- **`AudioLooper.h`:** The main audio processing class. This class is responsible for recording, playing back, and mixing the loops.
- **`Track.h`:** This class represents a single track in the looper. It is responsible for managing the audio data for a single loop.
- **`Memory.h`:** This class provides an interface for reading and writing to the external RAM chips and the SD card.
- **`Footswitch.h`:** This class represents a footswitch. It provides a simple interface for reading the state of a footswitch.
- **`Led.h`:** This class represents an LED. It provides a simple interface for turning an LED on and off.
- **`Pot.h`:** This class represents a rotary pot. It provides a simple interface for reading the value of a pot.
- **`Definitions.h`:** This file contains all the defines, flags, macros, and compile flags used in the project.

## Getting Started

To get started with the SuperLooperV2, you will need to:

1.  Assemble the hardware.
2.  Install the Teensyduino IDE.
3.  Install the Teensy Audio Library.
4.  Clone this repository.
5.  Open the `SuperLooperV2.ino` file in the Teensyduino IDE.
6.  Select your Teensy 4.1 board from the `Tools > Board` menu.
7.  Select the `USB Audio` option from the `Tools > USB Type` menu.
8.  Click the `Upload` button to upload the firmware to your Teensy.

## How to Use

1.  **Record your first loop:** Press footswitch 1 to start recording. The first loop you record will set the tempo for all subsequent loops. Press footswitch 1 again to stop recording and start playback.
2.  **Record additional loops:** Press footswitch 1 to start recording a new loop. The new loop will be quantized to the tempo of the first loop. Press footswitch 1 again to stop recording and start playback.
3.  **Mute and unmute tracks:** Use pot 1 to mute and unmute tracks. The pot's position determines which tracks are muted.
4.  **Branch your loops:** Mute a track and then record over it to create a new branch. This allows you to create alternative versions of your loops.
5.  **Clear all loops:** Press footswitch 2 to clear all loops and reset the looper.

## Future Development

- MIDI clock sync
- More effects
- Support for stereo loops
- A more advanced user interface

## Contributing

Contributions are welcome! Please feel free to submit a pull request or open an issue.

## License

This project is licensed under the MIT License.
