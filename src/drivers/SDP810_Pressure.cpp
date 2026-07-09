// SDP810_Pressure.cpp
#include "drivers/SDP810_Pressure.h"

SDP810_Pressure::SDP810_Pressure() {}

bool SDP810_Pressure::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Send continuous measurement command (Mass flow, Zero avg)
    Wire.beginTransmission(SDP810_I2C_ADDR);
    Wire.write(0x36);
    Wire.write(0x03);
    if (Wire.endTransmission() != 0) {
        log_e("SDP810 I2C bus not responding.");
        return false;
    }
    return true;
}

bool SDP810_Pressure::readPressure(int16_t &pressure) {
    Wire.requestFrom(SDP810_I2C_ADDR, 2);
    if (Wire.available() < 2) return false;

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    
    int16_t raw_val = (msb << 8) | lsb;
    
    // Scale factor for SDP810-500Pa is 60. Modbus needs x10 scale.
    // P = (raw / 60) * 10
    pressure = (raw_val * 10) / 60; 
    return true;
}
