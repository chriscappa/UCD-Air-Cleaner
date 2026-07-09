//I2C Driver for the Sensirion differential pressure sensor.
// SDP810_Pressure.h
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "CommonDefs.h"

class SDP810_Pressure {
public:
    SDP810_Pressure();
    bool begin();
    bool readPressure(int16_t &pressure); // Returns false on I2C fault
};
