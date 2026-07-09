//Hardware timer-based scanner. Steps through the 74HC4051, 
//measures the pulse sequence on the interrupt pin, and calculates the 
//absolute RPM, eventually mapped to 0-100%.
#pragma once

#include <Arduino.h>
#include "CommonDefs.h"

class TachMultiplexer {
public:
    TachMultiplexer(uint16_t maxRpm = 3000);
    void begin();
    
    uint8_t getNormalizedSpeed();
    uint8_t getFaultMask();

private:
    static void IRAM_ATTR pulseISR();
    static void scannerTask(void* pvParameters);

    uint16_t _maxRpm;
    static volatile uint32_t _pulseCount;
    
    uint16_t _rpm[NUM_FANS];
    uint8_t _normalizedPercentage[NUM_FANS];
    uint8_t _faultMask;
};
