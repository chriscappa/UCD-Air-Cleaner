//This driver encapsulates the FreeRTOS task responsible for 
//asynchronous linear speed ramping, the 500ms kick-start bounds, and 20% minimum stall-torque floor rules.
#pragma once

#include <Arduino.h>
#include "CommonDefs.h"

class FanPWMController {
public:
    FanPWMController();
    void begin();

    // Set network/local targets and linear ramp duration
    void setTargetSpeed(uint8_t targetPercent, uint16_t rampTimeSec);
    
    // Immediate override (ignores ramping, used for fail-safe)
    void forceSpeedNow(uint8_t targetPercent);

    // Get current operational speed (for tracking and stall alarms)
    uint8_t getCurrentSpeed();

private:
    static void fanTask(void* pvParameters);
    void updatePWMOutputs(uint8_t dutyPercent);

    uint8_t _currentPercent;
    uint8_t _targetPercent;
    uint16_t _rampTimeSec;
    
    uint32_t _rampStartTime;
    uint8_t _rampStartPercent;
    
    bool _kickStartActive;
    uint32_t _kickStartTime;

    const uint8_t _pwmChannels[NUM_FANS] = {
        FAN1_PWM_GEN, FAN2_PWM_GEN, FAN3_PWM_GEN, FAN4_PWM_GEN,
        FAN5_PWM_GEN, FAN6_PWM_GEN, FAN7_PWM_GEN, FAN8_PWM_GEN
    };
};
