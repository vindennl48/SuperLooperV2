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
AudioOutputI2S       i2sOut;
AudioLooper          looper; 

// Audio Connections
// Route Input -> Looper -> Output
AudioConnection      patch0(i2sIn, 0, looper, 0);
AudioConnection      patch1(looper, 0, i2sOut, 0);
AudioConnection      patch2(looper, 0, i2sOut, 1); // Mono out to both channels

void handleFootswitch();
void handleLed();

void setup() {
    // Initialize BALibrary hardware for TGA Pro MKII Rev 1
    TGA_PRO_MKII_REV1();
    SPI_MEM0_64M();
    SPI_MEM1_64M();

    // Audio Memory
    AudioMemory(128);

    // Enable Codec
    codecControl.disable();
    delay(100);
    codecControl.enable();
    delay(100);
    
    // Settle controls
    for (int i = 0; i < controls.getNumSwitches(); i++) {
        bool dummy;
        controls.hasSwitchChanged(i, dummy);
        delay(10);
    }

    // Indicate ready
    led1.on();
    delay(500);
    led1.off();
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
