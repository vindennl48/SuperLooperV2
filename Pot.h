#ifndef POT_H
#define POT_H

#include <Arduino.h>
#include <cmath>
#include "BALibrary.h"
#include "Definitions.h"

// Class to manage Analog Potentiometer state
class Pot {
public:
    // Constructor
    // pin: Physical pin number
    // swap: Invert direction (0.0 <-> 1.0)
    // Uses global POT_CALIB_MIN/MAX from Definitions.h
    Pot(BALibrary::BAPhysicalControls& controls, uint8_t pin, bool swap = false)
        : m_controls(controls), m_val(0.0f), m_changed(false),
          m_min(POT_CALIB_MIN), m_max(POT_CALIB_MAX), m_swap(swap),
          m_isLocked(true), m_storedVal(0.0f)
    {
        // Register pot with global calibration defines
        m_handle = m_controls.addPot(pin, m_min, m_max, m_swap);
    }

    // Update function to be called in main loop
    // Returns true if the effective value (physical or stored) has changed.
    bool update() {
        // Update the physical reading
        bool physicalChanged = m_controls.checkPotValue(m_handle, m_val);

        if (m_isLocked) {
            // Normal operation: Report change if physical hardware changed
            m_changed = physicalChanged;
            return m_changed;
        } else {
            // Soft Takeover / Unlocked Mode
            
            // Check if physical knob has "caught up" to the stored value
            // We use a small window (hysteresis) or simple crossing detection.
            // Here we check if the physical value is close enough to the stored value.
            const float TOLERANCE = 0.05f; // 5% window to catch the value
            
            if (std::abs(m_val - m_storedVal) < TOLERANCE) {
                m_isLocked = true; // Re-lock!
                // We don't report m_changed here because the value is effectively the same,
                // but now control is handed back to the user.
                m_changed = false; 
            } else {
                // Still waiting for user to turn knob to the stored position.
                // Output remains constant at m_storedVal.
                m_changed = false;
            }
            return false;
        }
    }

    // Returns true if the value changed during the last update() call
    bool changed() const {
        return m_changed;
    }

    // Get current effective value.
    // If Locked: Returns actual physical position.
    // If Unlocked: Returns the stored preset value.
    float getValue() const {
        return m_isLocked ? m_val : m_storedVal;
    }

    // --- Soft Takeover Control ---

    // Call this when loading a preset. 
    // This sets the output value immediately to `val` and disconnects 
    // the physical pot until the user moves it to match `val`.
    void setInitialValue(float val) {
        m_storedVal = val;
        m_isLocked = false;
        // Note: We assume the physical knob is NOT currently at 'val'. 
        // If it happens to be, the next update() will immediately lock.
    }

    // Check if the pot is currently locked (in control) or waiting for takeover
    bool isLocked() const {
        return m_isLocked;
    }

    // --- Configuration ---

    // Set polarity/direction of the pot
    // false: Normal (Min -> Max = 0.0 -> 1.0)
    // true:  Swapped (Min -> Max = 1.0 -> 0.0)
    void setPolarity(bool swap) {
        if (m_swap != swap) {
            m_swap = swap;
            // Update the underlying hardware calibration
            m_controls.setCalibrationValues(m_handle, m_min, m_max, m_swap);
        }
    }

private:
    BALibrary::BAPhysicalControls& m_controls;
    unsigned m_handle;
    
    float m_val;    // Current normalized physical value (0.0 - 1.0)
    bool m_changed; // Flag if value changed in last frame
    
    // Calibration storage
    unsigned m_min;
    unsigned m_max;
    bool m_swap;

    // Soft Takeover State
    bool m_isLocked;   // True if physical knob is in control
    float m_storedVal; // The "Preset" value we hold until locked
};

#endif // POT_H
