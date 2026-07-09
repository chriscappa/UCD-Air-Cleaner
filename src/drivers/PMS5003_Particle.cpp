// PMS5003_Particle.cpp
#include "drivers/PMS5003_Particle.h"

PMS5003_Particle::PMS5003_Particle() : _pmsSerial(2) {}

void PMS5003_Particle::begin() {
    pinMode(PMS5003_SET, OUTPUT);
    pinMode(PMS5003_RST, OUTPUT);
    digitalWrite(PMS5003_RST, HIGH);
    digitalWrite(PMS5003_SET, HIGH); // Active mode

    _pmsSerial.begin(9600, SERIAL_8N1, PMS5003_RX, PMS5003_TX);
}

bool PMS5003_Particle::readData(uint16_t &pm1_0, uint16_t &pm2_5, uint16_t &pm10, uint16_t &particles_0_3) {
    if (_pmsSerial.available() < 32) return false;

    if (_pmsSerial.read() == 0x42 && _pmsSerial.read() == 0x4D) {
        uint8_t buf[30];
        _pmsSerial.readBytes(buf, 30);
        
        // standard particle mass (CF=1)
        pm1_0 = (buf[2] << 8) | buf[3];
        pm2_5 = (buf[4] << 8) | buf[5];
        pm10  = (buf[6] << 8) | buf[7];
        
        // Particles > 0.3um in 0.1L air
        particles_0_3 = (buf[14] << 8) | buf[15];
        
        return true;
    }
    return false;
}
