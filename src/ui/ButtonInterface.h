// Manages button states, debouncing, multi-click parsing, 
// and manual override timing state vectors.
#pragma once

#include <Arduino.h>
#include "CommonDefs.h"
#include "storage/StorageManager.h" // Added dependency tracking

enum class ButtonEvent {
    NONE,
    SHORT_PRESS,
    QUINTUPLE_PRESS
};

class ButtonInterface {
public:
    ButtonInterface();
    ~ButtonInterface();

    // Pass storage dependency on initialization
    void begin(StorageManager* storage);
    
    ButtonEvent getEvent();
    
    bool isManualOverrideActive() const;
    void setManualOverride(bool active);
    
    uint16_t getRemainingManualMinutes() const;
    void decrementManualTimer();

private:
    StorageManager* _storage; // Shared storage instance pointer
    
    bool _manualOverrideActive;
    uint16_t _manualOverrideTimerMinutes;
    
    // Non-blocking Debounce State Machines
    bool _lastButtonState;
    uint32_t _lastEdgeTime;
    
    // Multi-click Window Vectors
    uint8_t _clickCount;
    uint32_t _firstClickTime;
    bool _shortPressQueued;
};
