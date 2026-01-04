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

// Handles for controls
// (Pot handle replaced by object below)

// Hardware Objects
Led led1(controls, BA_EXPAND_LED1_PIN);
Footswitch fs1(controls, BA_EXPAND_SW1_PIN);
Pot pot1(controls, BA_EXPAND_POT1_PIN);

// Audio System
BAAudioControlWM8731 codecControl;
AudioInputI2S        i2sIn;
AudioOutputI2S       i2sOut;
AudioLooper          looper; // Looper Instance (Not connected yet)

// Audio Connections (Bypass for now)
AudioConnection      patch0(i2sIn, 0, i2sOut, 0);
AudioConnection      patch1(i2sIn, 0, i2sOut, 1);

void handleFootswitch();
void handlePots();

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

    // Turn on LED 1 to indicate setup complete
    led1.on();
}

void loop() {
    handleFootswitch();
    handlePots();
    led1.update();
}

void handleFootswitch() {
    fs1.update();

    if (fs1.pressed()) {
        // Short press action (start record/play) will go here
        led1.toggle(); // Toggle LED on press for feedback
    }

    if (fs1.longPressed()) {
        // Long press action (stop/clear) will go here
        // Blink LED to indicate long press: 1 second duration, 100ms interval, end state OFF
        led1.blinkForDuration(1000, 100, Led::OFF);
    }
}

void handlePots() {
    // Update pot reading
    if (pot1.update()) {
        float val = pot1.getValue();

        // Control LED blink rate based on pot position
        // 0.0 -> OFF
        // 1.0 -> 50ms interval (Fast blink)
        
        if (val < 0.02f) {
            led1.off();
        } else {
            // Map 0.02 - 1.0 to 1000ms - 50ms
            // Formula: Out = InMax - (NormVal * Range)
            // We want high val -> low interval
            unsigned long interval = (unsigned long)(1000.0f - ((val - 0.02f) / 0.98f * 950.0f));
            led1.blink(interval);
        }
    }
}
