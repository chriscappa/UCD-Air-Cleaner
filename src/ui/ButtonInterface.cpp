#include "ui/ButtonInterface.h"

#define DEBOUNCE_DELAY_MS 50
#define MULTI_CLICK_WINDOW_MS 2000 // 2-second capture window for multi-clicks

ButtonInterface::ButtonInterface() 
    : _storage(nullptr),
      _manualOverrideActive(false), 
      _manualOverrideTimerMinutes(0),
      _lastButtonState(HIGH), // Internal pullup defaults HIGH
      _lastEdgeTime(0),
      _clickCount(0),
      _firstClickTime(0),
      _shortPressQueued(false) {}

ButtonInterface::~ButtonInterface() {}

void ButtonInterface::begin(StorageManager* storage) {
    _storage = storage;
    pinMode(QUIET_DOWN_PIN, INPUT_PULLUP);
}

ButtonEvent ButtonInterface::getEvent() {
    bool currentState = digitalRead(QUIET_DOWN_PIN);
    uint32_t now = millis();
    ButtonEvent eventToReturn = ButtonEvent::NONE;

    // 1. Detect Pin Transitions (Edge Debouncing)
    if (currentState != _lastButtonState) {
        if ((now - _lastEdgeTime) > DEBOUNCE_DELAY_MS) {
            _lastEdgeTime = now;
            _lastButtonState = currentState;

            // Falling Edge = Button Press (Active LOW)
            if (currentState == LOW) {
                if (_clickCount == 0) {
                    _firstClickTime = now;
                }
                _clickCount++;
            }
        }
    }

    // 2. Evaluate the Multi-Click Expiration Window
    if (_clickCount > 0 && (now - _firstClickTime >= MULTI_CLICK_WINDOW_MS)) {
        if (_clickCount >= 5) {
            eventToReturn = ButtonEvent::QUINTUPLE_PRESS;
            // Immediate safety wipe of override tracking variables
            _manualOverrideActive = false;
            _manualOverrideTimerMinutes = 0;
        } else {
            // Treat any count under 5 as a singular interaction command event
            _shortPressQueued = true;
        }
        _clickCount = 0; // Reset count bucket
    }

    // Handle immediate processing if click limit threshold is hit before timer expires
    if (_clickCount >= 5) {
        eventToReturn = ButtonEvent::QUINTUPLE_PRESS;
        _manualOverrideActive = false;
        _manualOverrideTimerMinutes = 0;
        _clickCount = 0;
    }

    // 3. Extract Queued Short Presses
    if (_shortPressQueued && eventToReturn == ButtonEvent::NONE) {
        eventToReturn = ButtonEvent::SHORT_PRESS;
        _shortPressQueued = false;
    }

    return eventToReturn;
}

bool ButtonInterface::isManualOverrideActive() const {
    return _manualOverrideActive;
}

void ButtonInterface::setManualOverride(bool active) {
    _manualOverrideActive = active;
    if (active) {
        _manualOverrideTimerMinutes = 120; // 2-Hour Fixed Window limit
    } else {
        _manualOverrideTimerMinutes = 0;
    }
}

uint16_t ButtonInterface::getRemainingManualMinutes() const {
    return _manualOverrideTimerMinutes;
}

void ButtonInterface::decrementManualTimer() {
    if (!_manualOverrideActive) return;

    if (_manualOverrideTimerMinutes > 0) {
        _manualOverrideTimerMinutes--;
        
        // Timeout Event Triggered
        if (_manualOverrideTimerMinutes == 0) {
            _manualOverrideActive = false;
            log_i("Manual Override Window expired. Dropping back to network targets.");
            
            // Clean up the local storage target to prevent loops from locking to a stale speed
            if (_storage != nullptr) {
                _storage->setLocalTargetFanSpeed(0); 
            }
        }
    }
}
