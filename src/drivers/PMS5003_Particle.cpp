// PMS5003_Particle.cpp
#include "drivers/PMS5003_Particle.h"

PMS5003_Particle::PMS5003_Particle() 
    : _pmsSerial(2), _pm1_0(0), _pm2_5(0), _pm10(0), _particleCount(0) {}

void PMS5003_Particle::begin() {
    pinMode(PMS5003_SET, OUTPUT);
    pinMode(PMS5003_RST, OUTPUT);
    digitalWrite(PMS5003_RST, HIGH);
    digitalWrite(PMS5003_SET, HIGH); // Awake Mode Asserted

    // Open UART2 channel
    _pmsSerial.begin(9600, SERIAL_8N1, PMS5003_RX, PMS5003_TX);

    // Spawn an isolating low-priority task thread to handle packet extraction
    xTaskCreate(parseUartTask, "PMS5003_Parser", 4096, this, 2, NULL);
}

void PMS5003_Particle::parseUartTask(void* pvParameters) {
    PMS5003_Particle* sensor = static_cast<PMS5003_Particle*>(pvParameters);
    uint8_t buffer[32];

    for (;;) {
        if (sensor->_pmsSerial.available() >= 32) {
            // Peek lookahead matching preamble magic boundaries
            if (sensor->_pmsSerial.read() == 0x42) {
                if (sensor->_pmsSerial.read() == 0x4D) {
                    buffer[0] = 0x42;
                    buffer[1] = 0x4D;
                    
                    // Ingest the remaining 30 payload bytes
                    sensor->_pmsSerial.readBytes(&buffer[2], 30);

                    // 1. Compute Checksum Verification
                    uint16_t computedChecksum = 0;
                    for (int i = 0; i < 30; i++) {
                        computedChecksum += buffer[i];
                    }
                    uint16_t frameChecksum = (buffer[30] << 8) | buffer[31];

                    // 2. Map structural layout parameters upon validation check passing
                    if (computedChecksum == frameChecksum) {
                        // Standard mass densities (CF=1)
                        sensor->_pm1_0        = (buffer[4] << 8)  | buffer[5];
                        sensor->_pm2_5        = (buffer[6] << 8)  | buffer[7];
                        sensor->_pm10         = (buffer[8] << 8)  | buffer[9];
                        
                        // Particle counts above > 0.3um size definitions
                        sensor->_particleCount = (buffer[16] << 8) | buffer[17];
                    } else {
                        log_w("PMS5003 UART Checksum Error detected.");
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Allow serial ring-buffers to populate
    }
}

uint16_t PMS5003_Particle::getPM1_0() const { return _pm1_0; }
uint16_t PMS5003_Particle::getPM2_5() const { return _pm2_5; }
uint16_t PMS5003_Particle::getPM10() const  { return _pm10; }
uint16_t PMS5003_Particle::getRawParticleCount() const { return _particleCount; }
