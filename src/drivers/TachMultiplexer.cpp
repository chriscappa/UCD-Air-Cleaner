#include "drivers/TachMultiplexer.h"

volatile uint32_t TachMultiplexer::_pulseCount = 0;

TachMultiplexer::TachMultiplexer(uint16_t maxRpm) : _maxRpm(maxRpm) {
    memset(_rpm, 0, sizeof(_rpm));
    memset(_normalizedPercentage, 0, sizeof(_normalizedPercentage));
}

void IRAM_ATTR TachMultiplexer::pulseISR() {
    _pulseCount++;
}

void TachMultiplexer::begin() {
    pinMode(MPX_ADDR0, OUTPUT);
    pinMode(MPX_ADDR1, OUTPUT);
    pinMode(MPX_ADDR2, OUTPUT);
    
    pinMode(MPX_READ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MPX_READ), pulseISR, FALLING);

    xTaskCreate(scannerTask, "Tach_MUX_Task", 4096, this, 6, NULL);
}

void TachMultiplexer::scannerTask(void* pvParameters) {
    TachMultiplexer* tach = static_cast<TachMultiplexer*>(pvParameters);
    const uint32_t sampleTimeMs = 125; // Milliseconds per channel

    for (;;) {
        for (uint8_t channel = 0; channel < NUM_FANS; channel++) {
            // Set MUX address
            digitalWrite(MPX_ADDR0, channel & 0x01);
            digitalWrite(MPX_ADDR1, (channel >> 1) & 0x01);
            digitalWrite(MPX_ADDR2, (channel >> 2) & 0x01);
            
            // Settle time
            vTaskDelay(pdMS_TO_TICKS(5)); 
            
            // Sample
            _pulseCount = 0;
            vTaskDelay(pdMS_TO_TICKS(sampleTimeMs));
            
            // Most PC fans generate 2 pulses per revolution
            uint32_t pulses = _pulseCount;
            tach->_rpm[channel] = (pulses * (60000 / sampleTimeMs)) / 2;
            
            // Normalize to 0-100%
            uint32_t percent = (tach->_rpm[channel] * 100) / tach->_maxRpm;
            tach->_normalizedPercentage[channel] = (percent > 100) ? 100 : percent;
        }
    }
}

uint8_t TachMultiplexer::getNormalizedSpeed() {
    // Return average speed of functioning fans
    uint32_t sum = 0;
    for (int i = 0; i < NUM_FANS; i++) {
        sum += _normalizedPercentage[i];
    }
    return (uint8_t)(sum / NUM_FANS);
}

uint8_t TachMultiplexer::getFaultMask() {
    uint8_t mask = 0;
    for (int i = 0; i < NUM_FANS; i++) {
        if (_rpm[i] == 0) {
            mask |= (1 << i);
        }
    }
    return mask;
}
