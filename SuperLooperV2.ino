#include <Audio.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <SerialFlash.h>
#include "BALibrary.h"
#include "Definitions.h"
#include "Track.h"
#include "AudioLooper.h"
#include "Led.h"
#include "Footswitch.h"
#include "Pot.h"

// #define USB_AUDIO
// #ifndef USB_AUDIO
// #error "Please select USB Type: 'Audio', 'Serial + MIDI + Audio', or similar in the Tools menu to enable USB Audio features."
// #endif

using namespace BALibrary;

// Hardware Controls
// 1 Switch (FS1), 0 Pots, 0 Encoders, 1 Output (LED1)
BAPhysicalControls controls(1, 0, 0, 1);

// Hardware Objects
Led led1(controls, BA_EXPAND_LED1_PIN);
Footswitch fs1(controls, BA_EXPAND_SW1_PIN);
Pot pot1(controls, BA_EXPAND_POT1_PIN);

// Audio System
BAAudioControlWM8731 codecControl;
AudioInputI2S        i2sIn;
AudioInputUSB        usbIn;       // USB Audio Input
AudioMixer4          inputMixer;  // Mixer for Hardware + USB
AudioOutputI2S       i2sOut;
AudioOutputUSB       usbOut;      // USB Audio Output
AudioLooper          looper; 

// Audio Connections
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

void handleFootswitch();
void handleLed();

void setup() {
    Serial.begin(9600);
    delay(200); // Allow Serial to initialize

    // Initialize BALibrary hardware for TGA Pro MKII Rev 1
    TGA_PRO_MKII_REV1();
    SPI_MEM0_64M();
    SPI_MEM1_64M();

    // Audio Memory
    AudioMemory(128);
    
    // Initialize Looper (Allocate Memory/SD)
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
    
    // Settle controls
    for (unsigned i = 0; i < controls.getNumSwitches(); i++) {
        bool dummy;
        controls.hasSwitchChanged(i, dummy);
        delay(10);
    }

    LOG("Setup Complete!");
}

void loop() {
    // 1. Poll Looper (Critical for SD operations)
    looper.poll();

    // 2. Handle Inputs
    handleFootswitch();
    
    // 3. Update Feedback
    handleLed();
}

void handleFootswitch() {
    fs1.update();

    if (fs1.pressed()) {
        looper.trigger();
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
            led1.on(); // Solid ON for recording
            break;
        case AudioLooper::PLAYBACK:
            led1.blink(500); // Slow blink for playback
            break;
    }
}
