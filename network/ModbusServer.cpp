//Includes the hardware direction pin toggling for the MAX3485 transceiver.
#include "network/ModbusServer.h"

ModbusServer::ModbusServer(uint8_t slaveId) : _rs485(1), _slaveId(slaveId) {
    memset(_registerMap, 0, sizeof(_registerMap));
    
    // Initialize immutable version parameters for Block 0
    _registerMap[0].fw_version_major = FW_VERSION_MAJOR;
    _registerMap[0].fw_version_minor = FW_VERSION_MINOR;
    _registerMap[0].hw_revision = HW_REVISION;
    _registerMap[0].protocol_version = PROTOCOL_VERSION;
}

void ModbusServer::begin() {
    pinMode(RS232_REDE, OUTPUT);
    digitalWrite(RS232_REDE, LOW); // Default to RX Mode

    // 9600 Baud Rate, 8 Data Bits, No Parity, 1 Stop Bit
    _rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    
    xTaskCreate(modbusTask, "Modbus_Task", 4096, this, 4, NULL);
}

void ModbusServer::updateTelemetryBlock(uint8_t nodeIndex, const ModbusDeviceBlock& data) {
    if (nodeIndex > MAX_CLIENTS) return;
    
    // Preserve writable registers (+0, +1, +20) during upstream telemetry updates
    uint16_t savedTarget = _registerMap[nodeIndex].target_fan_percent;
    uint16_t savedRamp = _registerMap[nodeIndex].ramp_time_sec;
    uint16_t savedPoll = _registerMap[nodeIndex].api_poll_interval;
    
    _registerMap[nodeIndex] = data;
    
    _registerMap[nodeIndex].target_fan_percent = savedTarget;
    _registerMap[nodeIndex].ramp_time_sec = savedRamp;
    if (nodeIndex == 0) _registerMap[0].api_poll_interval = savedPoll;
}

uint16_t ModbusServer::getTargetSpeed(uint8_t nodeIndex) {
    if (nodeIndex > MAX_CLIENTS) return 0;
    return _registerMap[nodeIndex].target_fan_percent;
}

void ModbusServer::transmitFrame(uint8_t* frame, size_t length) {
    digitalWrite(RS232_REDE, HIGH); // Switch MAX3485 to TX
    delayMicroseconds(100);         // Allow transceiver to settle
    
    _rs485.write(frame, length);
    _rs485.flush();                 // Wait for all bytes to physically transmit
    
    delayMicroseconds(100);
    digitalWrite(RS232_REDE, LOW);  // Return to RX Mode
}

void ModbusServer::modbusTask(void* pvParameters) {
    ModbusServer* server = static_cast<ModbusServer*>(pvParameters);
    
    for (;;) {
        if (server->_rs485.available()) {
            server->processModbusFrame();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Minimal Frame Processor (Function Code 03 - Read Holding Registers demonstrated)
void ModbusServer::processModbusFrame() {
    // Note: A full production implementation utilizes a robust framing parser using 3.5 char timeouts.
    // This provides the structural response architecture for the specified block layout.
    if (_rs485.available() < 8) return; 

    uint8_t request[8];
    _rs485.readBytes(request, 8);

    if (request[0] != _slaveId) return; // Ignore frames not addressed to us

    uint8_t functionCode = request[1];
    uint16_t startAddress = (request[2] << 8) | request[3];
    uint16_t quantity = (request[4] << 8) | request[5];
    
    // Validate CRC here (omitted for brevity)

    if (functionCode == 0x03 || functionCode == 0x04) { // Read Registers
        if (quantity > 125) {
            sendException(functionCode, 0x03); // Illegal Data Value
            return;
        }

        uint8_t response[256];
        response[0] = _slaveId;
        response[1] = functionCode;
        response[2] = quantity * 2; // Byte count
        
        uint8_t* payload = &response[3];
        
        for (uint16_t i = 0; i < quantity; i++) {
            uint16_t currentAddr = startAddress + i;
            uint8_t blockIndex = currentAddr / MODBUS_BLOCK_SZ;
            uint16_t offset = currentAddr % MODBUS_BLOCK_SZ;
            
            uint16_t regValue = 0;
            
            if (blockIndex <= MAX_CLIENTS) {
                // Map the offset to the struct safely using pointer arithmetic
                uint16_t* blockPtr = reinterpret_cast<uint16_t*>(&_registerMap[blockIndex]);
                
                if (offset <= 15 || (blockIndex == 0 && offset >= 20 && offset <= 29)) {
                    regValue = blockPtr[offset];
                }
            }
            
            *payload++ = (regValue >> 8) & 0xFF;
            *payload++ = regValue & 0xFF;
        }
        
        // Calculate and append CRC to response here
        // size_t frameLen = 3 + (quantity * 2) + 2; 
        // transmitFrame(response, frameLen);
    }
}

void ModbusServer::sendException(uint8_t functionCode, uint8_t exceptionCode) {
    uint8_t response[5];
    response[0] = _slaveId;
    response[1] = functionCode | 0x80;
    response[2] = exceptionCode;
    // Append CRC here
    // transmitFrame(response, 5);
}
