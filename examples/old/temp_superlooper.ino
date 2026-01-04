#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include "BALibrary.h"
#include "Definitions.h"
#include "TrackManager.h"

using namespace BALibrary;

// --- Hardware Calibration ---
#define POT_CALIB_MIN 0
#define POT_CALIB_MAX 1021

// --- Audio System ---
BAAudioControlWM8731 codecControl;
AudioInputI2S        i2sIn;
AudioOutputI2S       i2sOut;

// --- Track Manager ---
TrackManager trackManager;

// --- Audio Connections ---
AudioConnection      patchOutL(i2sIn, 0, i2sOut, 0);
AudioConnection      patchOutR(i2sIn, 0, i2sOut, 1); 

// --- Physical Controls ---
BAPhysicalControls controls(1, 0, 0, 0);

// --- Timing & Logic ---
const unsigned long LONG_PRESS_MS = 500;
unsigned long switch1PressTime = 0;
bool switch1IsHeld = false;

void setup() {
  TGA_PRO_MKII_REV1();
  SPI_MEM0_64M(); 
  SPI_MEM1_64M(); 
  delay(5);

  Serial.begin(9600);

  codecControl.disable();
  delay(100);
  codecControl.enable();
  delay(100);

  AudioMemory(128); 

  controls.addSwitch(BA_EXPAND_SW1_PIN);
  controls.addOutput(BA_EXPAND_LED1_PIN); 
  controls.addPot(BA_EXPAND_POT1_PIN, POT_CALIB_MIN, POT_CALIB_MAX);

  LOG("Settling controls...");
  /* Fixes issue with buttons not initializing with the correct state */
  for (unsigned int i = 0; i < controls.getNumSwitches(); i++) {
    bool dummy;
    controls.hasSwitchChanged(i, dummy);
    delay(10);
  }

  trackManager.init();

  LOG("SuperLooper 8-Track Initialized (IDLE)");
}

void loop() {
  handleFootswitch();
  updateLEDs();
  handlePots();
  trackManager.update();
}

void handleFootswitch() {
  // bool sw1;
  // if (controls.hasSwitchChanged(0, sw1)) {
  //   if (sw1) { // Button Pressed
  //     // looper.trigger();
  //     switch1PressTime = millis();
  //     switch1IsHeld = true;
  //   } else { // Button Released
  //     switch1IsHeld = false;
  //   }
  // }
  //
  // if (switch1IsHeld && (millis() - switch1PressTime >= LONG_PRESS_MS)) {
  //   looper.stopAndClear();
  //   switch1IsHeld = false; 
  // }
}

void updateLEDs() {
  // AudioLooper::State state = looper.getState();
  // bool ledState = false;
  //
  // switch (state) {
  //   case AudioLooper::STATE_RECORD_MASTER:
  //   case AudioLooper::STATE_RECORD_SLAVE:
  //     ledState = true;
  //     break;
  //
  //   case AudioLooper::STATE_ARM_RECORD:
  //   case AudioLooper::STATE_ARM_STOP:
  //     if ((millis() / 200) % 2 == 0) {
  //       ledState = true;
  //     }
  //     break;
  //
  //   case AudioLooper::STATE_IDLE:
  //   case AudioLooper::STATE_PLAY:
  //   default:
  //     ledState = false;
  //     break;
  // }
  //
  // controls.setOutput(0, ledState);
}

void handlePots() {
  // float val;
  // if (controls.checkPotValue(0, val)) {
  //   looper.setLoopDepth(1.0f - val);
  // }
}
