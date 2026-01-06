#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <SerialFlash.h>
#include "BALibrary.h"
#include "Definitions.h"
#include "AudioLooper.h"
#include "Led.h"
#include "Footswitch.h"
#include "Pot.h"

// #define USB_AUDIO
// #ifndef USB_AUDIO
// #error "Please select USB Type: 'Audio', 'Serial + MIDI + Audio', or similar in the Tools menu to enable USB Audio features."
// #endif

using namespace BALibrary;

// -------------------------------------------------------------------------
// Hardware Controls
// -------------------------------------------------------------------------
// 1 Switch (FS1), 0 Pots, 0 Encoders, 1 Output (LED1)
BAPhysicalControls controls(2, 0, 0, 1);

Led led1(controls, BA_EXPAND_LED1_PIN);
Footswitch fs1(controls, BA_EXPAND_SW1_PIN);
Footswitch fs2(controls, BA_EXPAND_SW2_PIN);
Pot pot1(controls, BA_EXPAND_POT1_PIN, true);

// -------------------------------------------------------------------------
// Audio System
// -------------------------------------------------------------------------
BAAudioControlWM8731 codecControl;
AudioInputI2S        i2sIn;
AudioInputUSB        usbIn;       // USB Audio Input
AudioMixer4          inputMixer;  // Mixer for Hardware + USB
AudioOutputI2S       i2sOut;
AudioOutputUSB       usbOut;      // USB Audio Output
AudioLooper          looper; 

// 1. Mix Inputs (Hardware + USB Left/Mono)
AudioConnection      patchMix0(i2sIn, 0, inputMixer, 0);
AudioConnection      patchMix1(usbIn, 0, inputMixer, 1);

// 2. Route Mix -> Looper
AudioConnection      patchLoopIn(inputMixer, 0, looper, 0);

// 3. Route Looper Output -> Hardware & USB
AudioConnection      patchOut0(looper, 0, i2sOut, 0);
AudioConnection      patchOut1(looper, 0, i2sOut, 1); // Mono out to both channels
AudioConnection      patchUsb0(looper, 0, usbOut, 0);
AudioConnection      patchUsb1(looper, 0, usbOut, 1);

// -------------------------------------------------------------------------
// Forward Declarations
// -------------------------------------------------------------------------
void handleFootswitch();
void handleLed();

// -------------------------------------------------------------------------
// Setup
// -------------------------------------------------------------------------
void setup() {
    Serial.begin(9600);
    delay(200); // Allow Serial to initialize

    LOG("Starting SuperLooperV2...");

    // Initialize BALibrary hardware for TGA Pro MKII Rev 1
    TGA_PRO_MKII_REV1();
    
    // Initialize External SPI Memory Chips
    SPI_MEM0_64M();
    SPI_MEM1_64M();

    // Allocate Audio Memory
    // 128 blocks is generally sufficient for standard audio path,
    // but the RingBuffers in MemoryRam manage their own external memory.
    AudioMemory(BLOCK_SIZE);
    
    // Initialize Looper (Allocates Memory/SD structures)
    looper.begin();

    // Mixer Gain Settings (Unity)
    inputMixer.gain(0, 1.0f); // Hardware Input
    inputMixer.gain(1, 1.0f); // USB Input
    inputMixer.gain(2, 0.0f);
    inputMixer.gain(3, 0.0f);

    // Enable Codec
    codecControl.disable();
    delay(100);
    codecControl.enable();
    delay(100);

    // Set Headphone Volume
    codecControl.setHeadphoneVolume(HEADPHONE_VOLUME);
    
    // Settle controls to avoid initial spurious reads
    for (unsigned i = 0; i < controls.getNumSwitches(); i++) {
        bool dummy;
        controls.hasSwitchChanged(i, dummy);
        delay(10);
    }
    pot1.setInitialValue(1.0f);

    LOG("Setup Complete!");
}

// -------------------------------------------------------------------------
// Main Loop
// -------------------------------------------------------------------------
void loop() {
    // 1. Poll Looper (Critical for SD operations)
    // This moves data between RAM and SD Card
    looper.poll();

    // 2. Handle Inputs
    // Update Potentiometer (Handles Soft Takeover)
    pot1.update();
    // if (pot1.changed()) LOG("Pot1 Value: %.2f", pot1.getValue());
    
    // Update Looper Mute State based on Pot
    looper.updateMuteState(pot1.getValue());

    handleFootswitch();
    
    // 3. Update Feedback
    handleLed();
}

void handleFootswitch() {
    fs1.update();
    fs2.update();

    if (fs1.pressed()) {
        // Check if we are currently in a "Branching" state (overwriting existing tracks)
        // vs simply appending a new track at the end.
        // We capture this BEFORE triggering because trigger() might reset the counts.
        bool isBranching = !looper.isAtEndOfList();

        AudioLooper::LooperState oldState = looper.getState();
        
        looper.trigger();
        
        AudioLooper::LooperState newState = looper.getState();
        
        // Detect if we just started a new recording session or track
        bool startedRecording = (oldState != AudioLooper::RECORDING && oldState != AudioLooper::WAITING_TO_RECORD) &&
                                (newState == AudioLooper::RECORDING || newState == AudioLooper::WAITING_TO_RECORD);

        if (startedRecording) {
            // Simplified Logic:
            // If we are recording over existing tracks (Branching), we MUST have muted them via the pot.
            // In this case, we always unlock to 1.0 to ensure the new recording is audible.
            // If we are just appending to the end, we trust the user's current pot setting (unless they mute the new track, 
            // but the user requested simplicity here).
            if (isBranching) {
                pot1.setInitialValue(1.0f);
                LOG("System: Branching Record Started -> Pot Unlocked to 1.0");
            }
        }
    }
    
    if (fs2.pressed()) {
        looper.reset();
    }
}

void handleLed() {
    led1.update();

    // Simple state feedback
    switch (looper.getState()) {
        case AudioLooper::IDLE:
            led1.off();
            break;
        case AudioLooper::RECORDING:
        case AudioLooper::PLAYBACK:
        case AudioLooper::FULL_PLAYBACK:
            led1.on(); 
            break;
        case AudioLooper::WAITING_TO_RECORD:
        case AudioLooper::WAITING_TO_FINISH:
            led1.blink(250);
            break;
    }
}
