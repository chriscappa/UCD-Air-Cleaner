#include "network/ModbusServer.h"

// Basic CRC16 Calculator matching Industrial Modbus Specifications
uint16_t calculateCRC(const uint8_t* buffer, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

ModbusServer::ModbusServer(uint8_t slaveId) : _rs485(1), _slaveId(slaveId) {
    memset(_registerMap, 0, sizeof(_registerMap));
    _registerMap[0].fw_version_major = FW_VERSION_MAJOR;
    _registerMap[0].fw_version_minor = FW_VERSION_MINOR;
    _registerMap[0].hw_revision = HW_REVISION;
    _registerMap[0].protocol_version = PROTOCOL_VERSION;
}

void ModbusServer::begin() {
    pinMode(RS232_REDE, OUTPUT);
    digitalWrite(RS232_REDE, LOW); // Receiver mode enabled

    _rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    xTaskCreate(modbusTask, "Modbus_Task", 4096, this, 4, NULL);
}

void ModbusServer::updateTelemetryBlock(uint8_t nodeIndex, const ModbusDeviceBlock& data) {
    if (nodeIndex > MAX_CLIENTS) return;
    
    // Safety Thread Lock-out Protection: Retain parameters modified during parallel processes
    uint16_t savedTarget = _registerMap[nodeIndex].target_fan_percent;
    uint16_t savedRamp   = _registerMap[nodeIndex].ramp_time_sec;
    
    _registerMap[nodeIndex] = data;
    
    _registerMap[nodeIndex].target_fan_percent = savedTarget;
    _registerMap[nodeIndex].ramp_time_sec = savedRamp;
}

void ModbusServer::setTargetSpeedByIndex(uint8_t index, uint16_t speed) {
    if (index > MAX_CLIENTS) return;
    _registerMap[index].target_fan_percent = speed;
}

uint16_t ModbusServer::getTargetSpeed(uint8_t nodeIndex) {
    if (nodeIndex > MAX_CLIENTS) return 0;
    return _registerMap[nodeIndex].target_fan_percent;
}

void ModbusServer::modbusTask(void* pvParameters) {
    ModbusServer* server = static_cast<ModbusServer*>(pvParameters);
    uint8_t rxBuffer[256];
    size_t rxIndex = 0;
    uint32_t lastCharTime = 0;

    for (;;) {
        while (server->_rs485.available() > 0) {
            if (rxIndex < sizeof(rxBuffer)) {
                rxBuffer[rxIndex++] = server->_rs485.read();
                lastCharTime = millis();
            }
        }

        // Verify character frame silent-interval timeout rules (T3.5 validation metric)
        if (rxIndex > 0 && (millis() - lastCharTime >= 5)) {
            if (rxBuffer[0] == server->_slaveId && rxIndex >= 8) {
                uint16_t receivedCRC = (rxBuffer[rxIndex - 1] << 8) | rxBuffer[rxIndex - 2];
                if (calculateCRC(rxBuffer, rxIndex - 2) == receivedCRC) {
                    server->processFrame(rxBuffer, rxIndex);
                }
            }
            rxIndex = 0; // Flash memory bucket indices clear
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void ModbusServer::processFrame(const uint8_t* frame, size_t length) {
    uint8_t functionCode = frame[1];
    uint16_t startAddress = (frame[2] << 8) | frame[3];
    uint16_t quantity = (frame[4] << 8) | frame[5];

    if (functionCode == 0x03) { // Parse Holding Register Read Requests
        uint8_t response[256];
        response[0] = _slaveId;
        response[1] = 0x03;
        response[2] = quantity * 2;

        uint8_t* payload = &response[3];
        for (uint16_t i = 0; i < quantity; i++) {
            uint16_t currentAddr = startAddress + i;
            uint8_t blockIndex = currentAddr / MODBUS_BLOCK_SZ;
            uint16_t offset = currentAddr % MODBUS_BLOCK_SZ;
            uint16_t regValue = 0;

            if (blockIndex <= MAX_CLIENTS) {
                uint16_t* blockPtr = reinterpret_cast<uint16_t*>(&_registerMap[blockIndex]);
                if (offset <= 15 || (blockIndex == 0 && offset >= 20 && offset <= 29)) {
                    regValue = blockPtr[offset];
                }
            }
            *payload++ = (regValue >> 8) & 0xFF;
            *payload++ = regValue & 0xFF;
        }

        size_t respLen = 3 + (quantity * 2);
        uint16_t crc = calculateCRC(response, respLen);
        response[respLen++] = crc & 0xFF;
        response[respLen++] = (crc >> 8) & 0xFF;

        // Perform safe physical wire data flushes
        digitalWrite(RS232_REDE, HIGH); // Assert TX line
        server->_rs485.write(response, respLen);
        server->_rs485.flush();        // Block thread block until the UART FIFO clears
        digitalWrite(RS232_REDE, LOW);  // Revert back down to listen lines safely
    }
}
