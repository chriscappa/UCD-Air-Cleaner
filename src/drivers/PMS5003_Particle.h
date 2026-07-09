//Hardware UART driver parsing the standard 32-byte passive stream format.
// PMS5003_Particle.h
#pragma once
#include <Arduino.h>
#include "CommonDefs.h"

class PMS5003_Particle {
public:
    PMS5003_Particle();
    void begin();
    
    // Aligns perfectly with structural telemetry mapping tasks inside main.cpp
    uint16_t getPM1_0() const;
    uint16_t getPM2_5() const;
    uint16_t getPM10() const;
    uint16_t getRawParticleCount() const;

private:
    static void parseUartTask(void* pvParameters);
    
    HardwareSerial _pmsSerial;
    
    uint16_t _pm1_0;
    uint16_t _pm2_5;
    uint16_t _pm10;
    uint16_t _particleCount;
};
