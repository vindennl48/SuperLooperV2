#ifndef FOOTSWITCH_H
#define FOOTSWITCH_H

#include <Arduino.h>
#include "BALibrary.h"
#include "Definitions.h"

// Class to manage Footswitch state including press, hold, and long-press detection
class Footswitch {
public:
  // Constructor
  // pin: Physical pin number
  // longPressMs: Duration in milliseconds to trigger a long press event
  Footswitch(BALibrary::BAPhysicalControls& controls, uint8_t pin, unsigned long longPressMs = 500)
    : m_controls(controls), m_longPressMs(longPressMs), m_pressStartTime(0),
     m_isHeld(false), m_longPressTriggered(false),
     m_justPressed(false), m_justReleased(false), m_justLongPressed(false),
     m_justLongPressReleased(false)
  {
    m_handle = m_controls.addSwitch(pin);
  }

  // Update function to be called in the main loop()
  // Polls the hardware and updates internal state flags
  void update() {
    // Reset one-shot event flags
    m_justPressed = false;
    m_justReleased = false;
    m_justLongPressed = false;
    m_justLongPressReleased = false;

    bool switchState;
    // Check hardware for changes
    if (m_controls.hasSwitchChanged(m_handle, switchState)) {
      if (switchState) {
        // Button Pressed (Rising Edge)
        m_justPressed = true;
        m_isHeld = true;
        m_pressStartTime = millis();
        m_longPressTriggered = false; // Reset logic for a new press
        LOG("Footswitch Pressed");
      } else {
        // Button Released (Falling Edge)
        m_justReleased = true;
        m_isHeld = false;
        LOG("Footswitch Released");
                
        // If we were in a long press state, trigger the specific release event
        if (m_longPressTriggered) {
          m_justLongPressReleased = true;
          LOG("Footswitch Long Press Released");
        }
      }
    }

    // Check for Long Press condition
    if (m_isHeld && !m_longPressTriggered) {
      if (millis() - m_pressStartTime >= m_longPressMs) {
        m_justLongPressed = true;
        m_longPressTriggered = true; // Mark that long press has occurred
        LOG("Footswitch Long Pressed");
      }
    }
  }

  // Returns true only on the frame the switch was pressed down
  bool pressed() const {
    return m_justPressed;
  }

  // Returns true only on the frame the switch was released (regardless of duration)
  bool released() const {
    return m_justReleased;
  }

  // Returns true as long as the switch is held down
  bool held() const {
    return m_isHeld;
  }

  // Returns true only on the frame the long-press duration is reached
  bool longPressed() const {
    return m_justLongPressed;
  }

  // Returns true while the switch is held AND the long-press duration has passed
  bool longPressHeld() const {
    return m_isHeld && m_longPressTriggered;
  }

  // Returns true only on the frame the switch was released AFTER a long-press
  bool longPressReleased() const {
    return m_justLongPressReleased;
  }

  // Set a new duration for long press detection
  void setLongPressDuration(unsigned long durationMs) {
    m_longPressMs = durationMs;
  }

private:
  BALibrary::BAPhysicalControls& m_controls;
  unsigned m_handle;
  unsigned long m_longPressMs;

  // State tracking
  unsigned long m_pressStartTime;
  bool m_isHeld;           // Physically held down
  bool m_longPressTriggered; // True if long press event already fired for the current hold

  // Event flags (valid for one frame)
  bool m_justPressed;
  bool m_justReleased;
  bool m_justLongPressed;
  bool m_justLongPressReleased;
};

#endif // FOOTSWITCH_H
