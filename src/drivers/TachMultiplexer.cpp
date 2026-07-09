#include "drivers/TachMultiplexer.h"

volatile uint32_t TachMultiplexer::_pulseCount = 0;

TachMultiplexer::TachMultiplexer(uint16_t maxRpm) 
    : _maxRpm(maxRpm), _faultMask(0) {
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
    const uint32_t sampleTimeMs = 125; 

    for (;;) {
        uint8_t localFaultMask = 0;

        for (uint8_t channel = 0; channel < NUM_FANS; channel++) {
            // Set 74HC4051 Hardware Multiplexer Addressing Lines
            digitalWrite(MPX_ADDR0, channel & 0x01);
            digitalWrite(MPX_ADDR1, (channel >> 1) & 0x01);
            digitalWrite(MPX_ADDR2, (channel >> 2) & 0x01);
            
            // Allow addressing propagation lines to electrical settle
            vTaskDelay(pdMS_TO_TICKS(5)); 
            
            // Critical Section: Isolate and reset the shared volatile counter
            portDISABLE_INTERRUPTS();
            _pulseCount = 0;
            portENABLE_INTERRUPTS();
            
            vTaskDelay(pdMS_TO_TICKS(sampleTimeMs));
            
            portDISABLE_INTERRUPTS();
            uint32_t pulses = _pulseCount;
            portENABLE_INTERRUPTS();
            
            // Standard 2 pulse per revolution calculations
            uint32_t calculatedRpm = (pulses * (60000 / sampleTimeMs)) / 2;
            tach->_rpm[channel] = calculatedRpm;
            
            // Normalize mapping boundary limits
            uint32_t percent = (calculatedRpm * 100) / tach->_maxRpm;
            tach->_normalizedPercentage[channel] = (percent > 100) ? 100 : percent;

            // Track individual fan failures (0 RPM detection)
            if (calculatedRpm == 0) {
                localFaultMask |= (1 << channel);
            }
        }
        tach->_faultMask = localFaultMask;
        vTaskDelay(pdMS_TO_TICKS(200)); // Delay between full multi-fan sweeping updates
    }
}

uint8_t TachMultiplexer::getNormalizedSpeed() {
    uint32_t sum = 0;
    uint8_t activeCount = 0;

    for (int i = 0; i < NUM_FANS; i++) {
        // Only include operational fans in the average computation
        if (_rpm[i] > 50) { 
            sum += _normalizedPercentage[i];
            activeCount++;
        }
    }
    
    if (activeCount == 0) return 0;
    return (uint8_t)(sum / activeCount);
}

uint8_t TachMultiplexer::getFaultMask() {
    return _faultMask;
}
