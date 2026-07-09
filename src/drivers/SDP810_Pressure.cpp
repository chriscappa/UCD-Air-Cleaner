#include "drivers/SDP810_Pressure.h"

SDP810_Pressure::SDP810_Pressure() {}

bool SDP810_Pressure::begin() {
    // Rely on main.cpp to have initialized globally shared Wire if necessary, 
    // or instantiate safely here targeting pin constants.
    Wire.begin(I2C_SDA, I2C_SCL, 100000); // 100kHz standard I2C speed boundary
    return triggerContinuousMeasurement();
}

bool SDP810_Pressure::triggerContinuousMeasurement() {
    Wire.beginTransmission(SDP810_I2C_ADDR);
    Wire.write(0x36); // Continuous measurement command MSB
    Wire.write(0x03); // Continuous measurement command LSB (Mass Flow, Average Zero)
    return (Wire.endTransmission() == 0);
}

float SDP810_Pressure::readPressurePascal() {
    // Request 3 bytes: 2 bytes Data + 1 byte CRC checksum
    uint8_t bytesReceived = Wire.requestFrom(SDP810_I2C_ADDR, 3);
    if (bytesReceived < 2) {
        log_e("SDP810 Read Timeout/Fault. Re-triggering initialization...");
        triggerContinuousMeasurement(); // Attempt self-recovery
        return 0.0f;
    }

    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();
    if (bytesReceived == 3) Wire.read(); // Flush CRC byte

    int16_t rawValue = (msb << 8) | lsb;
    
    // Scale factor for SDP810-500Pa variant is 60
    return (float)rawValue / 60.0f;
}
