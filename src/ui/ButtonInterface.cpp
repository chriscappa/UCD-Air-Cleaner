#include "ui/ButtonInterface.h"

ButtonInterface::ButtonInterface() 
    : _lastEvent(ButtonEvent::NONE), _manualOverrideActive(false), 
      _remainingManualMinutes(0), _clickCount(0), 
      _firstClickTime(0), _pressStartTime(0), _isPressed(false) {}

void ButtonInterface::begin() {
    pinMode(QUIET_DOWN_PIN, INPUT_PULLUP);
    xTaskCreate(buttonTask, "Button_Task", 3072, this, 3, NULL);
}

ButtonEvent ButtonInterface::getEvent() {
    ButtonEvent event = _lastEvent;
    if (event != ButtonEvent::NONE) {
        _lastEvent = ButtonEvent::NONE; // Consume event
    }
    return event;
}

bool ButtonInterface::isManualOverrideActive() {
    return _manualOverrideActive;
}

void ButtonInterface::setManualOverride(bool active) {
    _manualOverrideActive = active;
    if (active) {
        _remainingManualMinutes = 120; // 120-minute override timeout
    } else {
        _remainingManualMinutes = 0;
    }
}

uint16_t ButtonInterface::getRemainingManualMinutes() {
    return _remainingManualMinutes;
}

void ButtonInterface::decrementManualTimer() {
    if (_manualOverrideActive && _remainingManualMinutes > 0) {
        _remainingManualMinutes--;
        if (_remainingManualMinutes == 0) {
            _manualOverrideActive = false; // Timeout expired, revert to Auto
            log_i("Manual override expired. Returning to Auto Mode.");
        }
    }
}

void ButtonInterface::buttonTask(void* pvParameters) {
    ButtonInterface* btn = static_cast<ButtonInterface*>(pvParameters);
    const TickType_t pollRate = pdMS_TO_TICKS(10);
    
    for (;;) {
        uint32_t now = millis();
        bool currentState = (digitalRead(QUIET_DOWN_PIN) == LOW); // Active LOW

        // State transition: Released -> Pressed
        if (currentState && !btn->_isPressed) {
            btn->_isPressed = true;
            btn->_pressStartTime = now;
            
            if (btn->_clickCount == 0) {
                btn->_firstClickTime = now;
            }
        } 
        // State transition: Pressed -> Released
        else if (!currentState && btn->_isPressed) {
            btn->_isPressed = false;
            uint32_t pressDuration = now - btn->_pressStartTime;

            if (pressDuration >= 50 && pressDuration <= 1000) {
                btn->_clickCount++;
            }
        }

        // Multi-click evaluation windows
        if (btn->_clickCount > 0) {
            uint32_t windowElapsed = now - btn->_firstClickTime;

            // Check for Quintuple Press (5 presses within 10 seconds)
            if (btn->_clickCount >= 5 && windowElapsed <= 10000) {
                btn->_lastEvent = ButtonEvent::QUINTUPLE_PRESS;
                btn->_clickCount = 0;
            }
            // Check for Triple Press (3 presses within 5 seconds)
            else if (btn->_clickCount == 3 && windowElapsed > 5000 && windowElapsed <= 10000) {
                // If 5 seconds passed but under 10s, it's a valid triple press 
                // waiting to see if it becomes a quintuple press. 
                // However, logic dictates we trigger exactly at the time boundaries.
            }
            // Window Expirations
            else if (windowElapsed > 10000) {
                // 10 second window expired. Evaluate what we have.
                if (btn->_clickCount == 3) {
                    btn->_lastEvent = ButtonEvent::TRIPLE_PRESS;
                } else if (btn->_clickCount == 1) {
                    btn->_lastEvent = ButtonEvent::SHORT_PRESS;
                }
                btn->_clickCount = 0;
            }
            // Fast-track short presses if it's been more than 1 second since the single click
            // to ensure UI responsiveness for simple manual overrides.
            else if (btn->_clickCount == 1 && windowElapsed > 1000 && !btn->_isPressed) {
                btn->_lastEvent = ButtonEvent::SHORT_PRESS;
                btn->_clickCount = 0;
            }
        }

        vTaskDelay(pollRate);
    }
}
