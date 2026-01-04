#ifndef LED_H
#define LED_H

#include <Arduino.h>
#include "BALibrary.h"
#include "Definitions.h"

// Class to manage LED state including non-blocking blinking
class Led {
public:
    enum State {
        OFF,
        ON,
        BLINKING
    };

    enum BlinkMode {
        BLINK_INFINITE,
        BLINK_DURATION,
        BLINK_COUNT
    };

    // Constructor takes reference to physical controls and the physical pin number
    // It registers itself with controls and stores the handle privately
    Led(BALibrary::BAPhysicalControls& controls, uint8_t pin)
        : m_controls(controls), m_state(OFF), 
          m_blinkMode(BLINK_INFINITE), m_blinkState(false),
          m_blinkInterval(500), m_lastUpdate(0),
          m_blinkStartTime(0), m_blinkDuration(0),
          m_blinkCount(0), m_returnState(OFF)
    {
        m_handle = m_controls.addOutput(pin);
    }

    // Turn LED solid ON
    void on() {
        if (m_state != ON) LOG("LED ON");
        m_state = ON;
        m_controls.setOutput(m_handle, 1);
    }

    // Turn LED solid OFF
    void off() {
        if (m_state != OFF) LOG("LED OFF");
        m_state = OFF;
        m_controls.setOutput(m_handle, 0);
    }

    // Set LED to specific boolean state (stops blinking)
    void set(bool isActive) {
        if (isActive) on();
        else off();
    }

    // Toggle logic: If ON or BLINKING -> OFF. If OFF -> ON.
    void toggle() {
        if (m_state == ON || m_state == BLINKING) {
            off();
        } else {
            on();
        }
    }

    // Start blinking indefinitely with symmetrical on/off duration
    void blink(unsigned long intervalMs) {
        // If already blinking indefinitely at the same interval, do nothing
        if (m_state == BLINKING && m_blinkMode == BLINK_INFINITE && m_blinkInterval == intervalMs) {
            return;
        }
        setupBlink(intervalMs);
        m_blinkMode = BLINK_INFINITE;
    }

    // Blink for a specific total duration, then return to a specific state
    void blinkForDuration(unsigned long durationMs, unsigned long intervalMs, State returnState) {
        setupBlink(intervalMs);
        m_blinkMode = BLINK_DURATION;
        m_blinkDuration = durationMs;
        m_blinkStartTime = millis();
        m_returnState = returnState;
    }

    // Blink a specific number of times, then return to a specific state
    void blinkCount(unsigned int count, unsigned long intervalMs, State returnState) {
        if (count == 0) {
            set(returnState == ON);
            return;
        }
        setupBlink(intervalMs);
        m_blinkMode = BLINK_COUNT;
        m_blinkCount = count;
        m_returnState = returnState;
    }

    // Update function to be called in main loop()
    // Handles non-blocking timing for blinking
    void update() {
        if (m_state == BLINKING) {
            unsigned long currentMs = millis();
            
            // Handle Duration Limit
            if (m_blinkMode == BLINK_DURATION) {
                if (currentMs - m_blinkStartTime >= m_blinkDuration) {
                     set(m_returnState == ON);
                     return;
                }
            }

            // Handle Blinking Toggles
            if (currentMs - m_lastUpdate >= m_blinkInterval) {
                m_lastUpdate = currentMs;

                // Handle Count Limit (Decrement when finishing an OFF phase, completing a full cycle)
                if (m_blinkMode == BLINK_COUNT && !m_blinkState) {
                    m_blinkCount--;
                    if (m_blinkCount == 0) {
                        set(m_returnState == ON);
                        return;
                    }
                }

                m_blinkState = !m_blinkState;
                m_controls.setOutput(m_handle, m_blinkState ? 1 : 0);
            }
        }
    }

    // Check if LED is considered ON (includes BLINKING)
    bool isOn() const {
        return m_state == ON || m_state == BLINKING;
    }

private:
    // Helper to initialize blink parameters and reset timer
    void setupBlink(unsigned long intervalMs) {
        LOG("LED BLINK START (Interval: %lu ms)", intervalMs);
        m_state = BLINKING;
        m_blinkInterval = intervalMs;
        m_blinkState = true; // Always start in ON phase
        m_controls.setOutput(m_handle, 1);
        m_lastUpdate = millis();
    }

    BALibrary::BAPhysicalControls& m_controls;
    unsigned m_handle;
    State m_state;

    // Blinking internals
    BlinkMode m_blinkMode;
    bool m_blinkState; // True if physically ON during blink cycle
    unsigned long m_blinkInterval;
    unsigned long m_lastUpdate;
    
    // Duration/Count internals
    unsigned long m_blinkStartTime;
    unsigned long m_blinkDuration;
    unsigned int m_blinkCount;
    State m_returnState;
};

#endif // LED_H
