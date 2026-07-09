#include "drivers/FanPWMController.h"

// 25 kHz Intel Standard, 8-bit resolution
#define PWM_FREQ 25000
#define PWM_RES  8

FanPWMController::FanPWMController() 
    : _currentPercent(0), _targetPercent(0), _rampTimeSec(10), 
      _rampStartTime(0), _rampStartPercent(0), 
      _kickStartActive(false), _kickStartTime(0) {}

void FanPWMController::begin() {
    for (int i = 0; i < NUM_FANS; i++) {
        ledcSetup(i, PWM_FREQ, PWM_RES);
        ledcAttachPin(_pwmChannels[i], i);
        ledcWrite(i, 0); // Start off
    }
    
    xTaskCreate(fanTask, "Fan_PWM_Task", 4096, this, 5, NULL);
}

void FanPWMController::setTargetSpeed(uint8_t targetPercent, uint16_t rampTimeSec) {
    if (_targetPercent == targetPercent) return; // No change

    // Enforce 20% stall floor unless turning off completely
    if (targetPercent > 0 && targetPercent < 20) {
        targetPercent = 20;
    }

    _rampStartPercent = _currentPercent;
    _targetPercent = targetPercent;
    _rampTimeSec = rampTimeSec;
    _rampStartTime = millis();

    // Trigger Kick-Start if transitioning from a dead stop
    if (_currentPercent == 0 && _targetPercent > 0) {
        _kickStartActive = true;
        _kickStartTime = millis();
        updatePWMOutputs(100); // Blast 100% duty cycle
        log_i("Kick-start engaged.");
    }
}

void FanPWMController::forceSpeedNow(uint8_t targetPercent) {
    if (targetPercent > 0 && targetPercent < 20) targetPercent = 20;
    
    _targetPercent = targetPercent;
    _currentPercent = targetPercent;
    _kickStartActive = false;
    updatePWMOutputs(_currentPercent);
}

uint8_t FanPWMController::getCurrentSpeed() {
    return _currentPercent;
}

void FanPWMController::updatePWMOutputs(uint8_t dutyPercent) {
    uint32_t dutyValue = (dutyPercent * 255) / 100;
    for (int i = 0; i < NUM_FANS; i++) {
        ledcWrite(i, dutyValue);
    }
}

void FanPWMController::fanTask(void* pvParameters) {
    FanPWMController* controller = static_cast<FanPWMController*>(pvParameters);
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 50ms control loop

    for (;;) {
        uint32_t now = millis();

        // 1. Handle Kick-start phase (500ms block)
        if (controller->_kickStartActive) {
            if (now - controller->_kickStartTime >= 500) {
                controller->_kickStartActive = false; // Kick-start finished
            } else {
                vTaskDelay(xFrequency);
                continue; // Hold 100%
            }
        }

        // 2. Linear Ramp Math
        if (controller->_currentPercent != controller->_targetPercent) {
            uint32_t elapsed = now - controller->_rampStartTime;
            uint32_t totalRampMs = controller->_rampTimeSec * 1000;

            if (elapsed >= totalRampMs || totalRampMs == 0) {
                controller->_currentPercent = controller->_targetPercent;
            } else {
                float progress = (float)elapsed / totalRampMs;
                int16_t difference = controller->_targetPercent - controller->_rampStartPercent;
                controller->_currentPercent = controller->_rampStartPercent + (uint8_t)(difference * progress);
            }
            
            controller->updatePWMOutputs(controller->_currentPercent);
        }

        vTaskDelay(xFrequency);
    }
}
