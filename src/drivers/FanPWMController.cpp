#include "drivers/FanPWMController.h"

// 25 kHz Intel Standard, 8-bit resolution (0-255 duty cycle steps)
#define PWM_FREQ 25000
#define PWM_RES  8

FanPWMController::FanPWMController() 
    : _currentPercent(0), _targetPercent(0), _rampTimeSec(10), 
      _rampStartTime(0), _rampStartPercent(0), 
      _kickStartActive(false), _kickStartTime(0) {}

void FanPWMController::begin() {
    for (int i = 0; i < NUM_FANS; i++) {
        // Modern ESP32 Arduino Core unified API matching PlatformIO build trees
        ledcAttach(_pwmChannels[i], PWM_FREQ, PWM_RES);
        ledcWrite(_pwmChannels[i], 0); // Start safely at a dead stop (0% duty)
    }
    
    xTaskCreate(fanTask, \"Fan_PWM_Task\", 4096, this, 5, NULL);
}

void FanPWMController::setTargetSpeed(uint8_t targetPercent, uint16_t rampTimeSec) {
    if (_targetPercent == targetPercent) return; // Guard against redundant re-calculations

    // Enforce 20% electrical stall floor unless explicit system shutdown is commanded
    if (targetPercent > 0 && targetPercent < 20) {
        targetPercent = 20;
    }

    _rampStartPercent = _currentPercent;
    _targetPercent = targetPercent;
    _rampTimeSec = rampTimeSec;
    _rampStartTime = millis();

    // Trigger explicit high-torque kick-start if initiating movement from a dead stop
    if (_currentPercent == 0 && _targetPercent > 0) {
        _kickStartActive = true;
        _kickStartTime = millis();
        updatePWMOutputs(100); // Blast 100% duty cycle to break magnetic static friction
        log_i(\"Fan Hardware: Kick-starting rotors at 100%% duty cycle.\");
    }
}

void FanPWMController::forceSpeedNow(uint8_t targetPercent) {
    // Immediate override bypass: disables linear ramping timers completely
    _targetPercent = targetPercent;
    _currentPercent = targetPercent;
    _kickStartActive = false; // Cancel active kickstarts if an safety emergency is asserted
    updatePWMOutputs(targetPercent);
    log_w(\"Fan Hardware: Immediate Safety Force-Speed Override Asserted: %d%%\", targetPercent);
}

uint8_t FanPWMController::getCurrentSpeed() {
    return _currentPercent;
}

void FanPWMController::updatePWMOutputs(uint8_t dutyPercent) {
    // Map standard 0-100 percentage values down to hardware 8-bit registers (0-255 scaling bounds)
    uint32_t dutyValue = (dutyPercent * 255) / 100;
    for (int i = 0; i < NUM_FANS; i++) {
        ledcWrite(_pwmChannels[i], dutyValue);
    }
}

void FanPWMController::fanTask(void* pvParameters) {
    FanPWMController* controller = static_cast<FanPWMController*>(pvParameters);
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // 50ms smooth linear execution cycles
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        uint32_t now = millis();

        // 1. Evaluate high-torque break-away phase status
        if (controller->_kickStartActive) {
            if (now - controller->_kickStartTime >= 500) {
                controller->_kickStartActive = false; // Kickstart duration elapsed, drop back to ramp math
                controller->_rampStartTime = now;    // Reset ramp baseline starting point reference
            } else {
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
                continue; // Hold 100% line execution
            }
        }

        // 2. Continuous Linear Ramping Evaluation
        if (controller->_currentPercent != controller->_targetPercent) {
            uint32_t elapsed = now - controller->_rampStartTime;
            uint32_t totalRampMs = controller->_rampTimeSec * 1000;

            if (elapsed >= totalRampMs || totalRampMs == 0) {
                controller->_currentPercent = controller->_targetPercent;
            } else {
                float progress = (float)elapsed / (float)totalRampMs;
                int16_t difference = (int16_t)controller->_targetPercent - (int16_t)controller->_rampStartPercent;
                controller->_currentPercent = controller->_rampStartPercent + (uint8_t)(difference * progress);
            }
            
            // Push calculation down to the LEDC hardware timers
            controller->updatePWMOutputs(controller->_currentPercent);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
