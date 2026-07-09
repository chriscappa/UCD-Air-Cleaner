//This instantiates the HAL drivers and network interfaces, arbitrates control priorities 
//(Manual Override vs. Cloud Target vs. Fail-Safe), evaluates the 5-second Fan Stall hardware alarm, 
//and exposes the USB-C Plaintext CLI for client provisioning.
  #include <Arduino.h>
#include "CommonDefs.h"
#include "DataModels.h"
#include "storage/StorageManager.h"
#include "drivers/FanPWMController.h"
#include "drivers/TachMultiplexer.h"
#include "drivers/SDP810_Pressure.h"
#include "drivers/PMS5003_Particle.h"
#include "network/EspNowEngine.h"
#include "network/ModbusServer.h"
#include "network/CloudSync.h"
#include "ui/ButtonInterface.h"

// =============================================================================
// Global Instance Declarations
// =============================================================================
StorageManager storage;
ButtonInterface buttonUI;
FanPWMController fanController;
TachMultiplexer tachScanner(3000); // Assume 3000 RPM max for normalization
SDP810_Pressure pressureSensor;
PMS5003_Particle particleSensor;

EspNowEngine* espNow = nullptr;
ModbusServer* modbus = nullptr;
CloudSync* cloudSync = nullptr;

// System State
DeviceRole currentRole;
uint16_t activeTargetSpeed = 0;
uint32_t lastStallCheckTime = 0;
bool fanStallAlarmActive = false;
uint32_t stallViolationStartTime = 0;

// Configuration Constants
const char* ROUTER_SSID = "CRADLEPOINT_ROUTER_SSID";
const char* ROUTER_PASS = "ROUTER_WPA2_PASSWORD";
const char* API_ENDPOINT= "https://api.attune-iot.com/v1/ac/targets";

// =============================================================================
// ESP-NOW Telemetry Callback (Server-Side)
// =============================================================================
void onTelemetryReceived(const uint8_t* mac, const UpstreamTelemetryPayload* payload) {
    if (currentRole != DeviceRole::SERVER_MASTER || modbus == nullptr) return;

    // Map MAC address to node index 1-19
    uint8_t provisionedMacs[MAX_CLIENTS][6];
    size_t count = storage.getProvisionedClients(provisionedMacs, MAX_CLIENTS);
    
    uint8_t nodeIndex = 0;
    for (size_t i = 0; i < count; i++) {
        if (memcmp(provisionedMacs[i], mac, 6) == 0) {
            nodeIndex = i + 1;
            break;
        }
    }

    if (nodeIndex > 0) {
        ModbusDeviceBlock block = {0};
        block.differential_pressure = payload->differential_pressure;
        block.pm1_0 = payload->pm1_0;
        block.pm2_5 = payload->pm2_5;
        block.pm10 = payload->pm10;
        block.particle_count = payload->particle_count;
        block.actual_fan_speed = payload->actual_fan_speed;
        block.active_local_target = payload->active_local_target;
        block.status_bitfield = payload->status_bitfield;
        block.seconds_since_telemetry = 0; // Reset timeout counter
        block.remaining_manual_min = payload->remaining_manual_min;

        modbus->updateTelemetryBlock(nodeIndex, block);
    }
}

// =============================================================================
// USB-C Plaintext CLI Provisioning Interface
// =============================================================================
void processCLI() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.length() == 0) return;

        if (input.startsWith("list")) {
            Serial.printf("Provisioned Clients: %d/%d\n", storage.getClientCount(), MAX_CLIENTS);
            uint8_t macs[MAX_CLIENTS][6];
            size_t count = storage.getProvisionedClients(macs, MAX_CLIENTS);
            for (size_t i = 0; i < count; i++) {
                Serial.printf("[%d] %02X:%02X:%02X:%02X:%02X:%02X\n", i + 1, 
                    macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
