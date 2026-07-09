//Hardware UART driver parsing the standard 32-byte passive stream format.
// PMS5003_Particle.h
#pragma once
#include <Arduino.h>
#include "CommonDefs.h"

class PMS5003_Particle {
public:
    PMS5003_Particle();
    void begin();
    bool readData(uint16_t &pm1_0, uint16_t &pm2_5, uint16_t &pm10, uint16_t &particles_0_3);

private:
    HardwareSerial _pmsSerial;
};
