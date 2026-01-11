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
AudioMixer4          outputMixer; // Mixer for Dry + Wet
AudioOutputI2S       i2sOut;
AudioOutputUSB       usbOut;      // USB Audio Output
AudioLooper          looper; 

// 1. Mix Inputs (Hardware + USB Left/Mono)
AudioConnection      patchMix0(i2sIn, 0, inputMixer, 0);
AudioConnection      patchMix1(usbIn, 0, inputMixer, 1);

// 2. Route Mix -> Looper
AudioConnection      patchLoopIn(inputMixer, 0, looper, 0);

// 3. Route Signals -> Output Mixer
AudioConnection      patchLoopOut(looper, 0, outputMixer, 0); // Wet (Looper)
AudioConnection      patchDryOut(inputMixer, 0, outputMixer, 1); // Dry (Thru)

// 4. Route Output Mixer -> Hardware & USB
AudioConnection      patchOut0(outputMixer, 0, i2sOut, 0);
AudioConnection      patchOut1(outputMixer, 0, i2sOut, 1); // Mono out to both channels
AudioConnection      patchUsb0(outputMixer, 0, usbOut, 0);
AudioConnection      patchUsb1(outputMixer, 0, usbOut, 1);

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

  // --- RAM TEST ---
  LOG("--- Performing RAM Self-Test ---");
  int16_t testPattern[128];
  int16_t readBack[128];
  for(int i=0; i<128; i++) testPattern[i] = (int16_t)(i * 100);
    
  // Write to address 0 (using the looper's internal ram object would be ideal, 
  // but since it's private, we will rely on the fact that Ram works if this works.
  // Actually, we can't access looper.ram directly. 
  // We will assume the Ram class works if we instantiate a temporary one or 
  // just trust the rest of the code if the issue is logic.
  // Wait, we can't verify looper.ram easily without exposing it.
  // Let's Skip the explicit object test and trust the logic fix first.
  // ACTUALLY, let's create a temporary Ram object just to test the bus.
  Ram testRam;
  testRam.begin();
  testRam.write16(0, testPattern, 128);
  testRam.read16(0, readBack, 128);
    
  bool ramPass = true;
  for(int i=0; i<128; i++) {
    if(readBack[i] != testPattern[i]) {
      LOG("RAM FAILURE at index %d: Expected %d, Got %d", i, testPattern[i], readBack[i]);
      ramPass = false;
      break;
    }
  }
  if(ramPass) LOG("RAM Self-Test: PASSED");
  else LOG("RAM Self-Test: FAILED");
  // ----------------

  // Mixer Gain Settings (Unity)
  inputMixer.gain(0, 1.0f); // Hardware Input
  inputMixer.gain(1, 1.0f); // USB Input
  inputMixer.gain(2, 0.0f);
  inputMixer.gain(3, 0.0f);

  outputMixer.gain(0, 1.0f); // Wet
  outputMixer.gain(1, 1.0f); // Dry
  outputMixer.gain(2, 0.0f);
  outputMixer.gain(3, 0.0f);

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
  handlePot();
  handleFootswitch();
  handleLed();
}

void handlePot() {
  // Check if the Looper requested a Pot Reset (e.g. after starting a new layer)
  if (looper.popRequestPotReset()) {
    pot1.setInitialValue(1.0f);
    LOG("System: Pot Reset Requested -> Unlocked to 1.0");
  }

  // 1. Handle Inputs
  // Update Potentiometer (Handles Soft Takeover)
  if (pot1.update()) {
    // Update Smart Mute State based on Pot
    looper.updateSmartMute(pot1.getValue());
  }
}

void handleFootswitch() {
  fs1.update();
  fs2.update();

  if (fs1.pressed()) {
    looper.trigger();
  }

  if (fs2.pressed()) {
    looper.reset();
  }
}

void handleLed() {
  led1.update();

  // Blink if waiting for a state change (Quantized start/stop)
  if (looper.isWaiting()) {
    led1.blink(250);
    return;
  }

  // Solid state feedback
  if (looper.isIdle()) {
    led1.off();
  } else if (looper.isRecording() || looper.isPlaying()) {
    led1.on();
  } else {
    // Fallback (e.g. stopped but not idle, though currently reset handles that)
    led1.off();
  }
}
