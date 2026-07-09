//This class implements a non-blocking debounce and multi-click 
//detection engine for the QUIET_DOWN_PIN. It identifies short presses 
//for the 25% incremental manual override loop and tracks multi-click
//timing windows for OTA pairing sequences.
#pragma once

#include <Arduino.h>
#include "CommonDefs.h"

enum class ButtonEvent {
    NONE,
    SHORT_PRESS,        // 1 Press: Manual override step (50ms - 1000ms)
    TRIPLE_PRESS,       // 3 Presses within 5s: Trigger Pairing Beacon
    QUINTUPLE_PRESS     // 5 Presses within 10s: Memory Wipe & Unpair
};

class ButtonInterface {
public:
    ButtonInterface();
    void begin();
    
    // Retrieves and consumes the most recent button event
    ButtonEvent getEvent();

    // Override timer logic
    bool isManualOverrideActive();
    void setManualOverride(bool active);
    uint16_t getRemainingManualMinutes();
    void decrementManualTimer(); // Called once per minute by the main task

private:
    static void buttonTask(void* pvParameters);

    volatile ButtonEvent _lastEvent;
    
    bool _manualOverrideActive;
    uint16_t _remainingManualMinutes;

    // Multi-click tracking state
    uint8_t _clickCount;
    uint32_t _firstClickTime;
    uint32_t _pressStartTime;
    bool _isPressed;
};
