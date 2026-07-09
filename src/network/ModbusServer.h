//This interface handles the strict RS-485 physical layer specifications (9600 8N1) 
//and exposes the structured 100-register blocks representing the Server and Client telemetry buffers.
#pragma once

#include <Arduino.h>
#include "CommonDefs.h"
#include "DataModels.h"

// Define the 100-Register Block Structure 
struct ModbusDeviceBlock {
    // Standard Offsets (+0 to +15)
    uint16_t target_fan_percent;        // +0 (R/W)
    uint16_t ramp_time_sec;             // +1 (R/W)
    int16_t  differential_pressure;     // +2 (R)
    uint16_t pm1_0;                     // +3 (R)
    uint16_t pm2_5;                     // +4 (R)
    uint16_t pm10;                      // +5 (R)
    uint16_t particle_count;            // +6 (R)
    uint16_t actual_fan_speed;          // +7 (R)
    uint16_t active_local_target;       // +8 (R)
    uint16_t status_bitfield;           // +9 (R)
    uint16_t seconds_since_telemetry;   // +10 (R)
    uint16_t remaining_manual_min;      // +11 (R)
    uint16_t fw_version_major;          // +12 (R)
    uint16_t fw_version_minor;          // +13 (R)
    uint16_t hw_revision;               // +14 (R)
    uint16_t protocol_version;          // +15 (R)
    
    // Server Specific Extensions (+20 to +29) - Only valid in Block 0
    uint16_t api_poll_interval;         // +20 (R/W)
    uint16_t zone_id;                   // +21 (R)
    uint16_t modbus_slave_id;           // +22 (R)
    uint16_t network_id;                // +23 (R)
    uint16_t auth_failure_count;        // +24 (R)
    uint16_t conn_failure_count;        // +25 (R)
    uint16_t consec_poll_failures;      // +26 (R)
    uint16_t consec_successful_polls;   // +27 (R)
    uint16_t ota_status;                // +28 (R)
    uint16_t event_log_count;           // +29 (R)
};

class ModbusServer {
public:
    ModbusServer(uint8_t slaveId = 1);
    void begin();
    
    // Update the internal register map for a specific node (0 = Server, 1-19 = Clients)
    void updateTelemetryBlock(uint8_t nodeIndex, const ModbusDeviceBlock& data);
    // Allow for setting of different speeds for individual Client ACs
    void setTargetSpeedByIndex(uint8_t index, uint16_t speed);
    
    // Retrieve the currently set target speeds (written by the upstream Attune Gateway)
    uint16_t getTargetSpeed(uint8_t nodeIndex);

private:
    static void modbusTask(void* pvParameters);
    void processModbusFrame();
    void sendException(uint8_t functionCode, uint8_t exceptionCode);
    void transmitFrame(uint8_t* frame, size_t length);

    HardwareSerial _rs485;
    uint8_t _slaveId;
    
    // Memory map containing Block 0 (Server) and Blocks 1-19 (Clients)
    ModbusDeviceBlock _registerMap[MAX_CLIENTS + 1]; 
};
