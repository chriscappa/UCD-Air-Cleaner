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
TachMultiplexer tachScanner(3000); // 3000 RPM maximum boundary normalization
SDP810_Pressure pressureSensor;
PMS5003_Particle particleSensor;

EspNowEngine* espNow = nullptr;
ModbusServer* modbus = nullptr;
CloudSync* cloudSync = nullptr;

// System Orchestration State Trackers
DeviceRole currentRole;
uint16_t activeTargetSpeed = 0;
uint32_t lastStallCheckTime = 0;
bool fanStallAlarmActive = false;
uint32_t stallViolationStartTime = 0;

// Hardcoded Deployment Connection Configurations
const char* ROUTER_SSID  = "CRADLEPOINT_ROUTER_SSID";
const char* ROUTER_PASS  = "ROUTER_WPA2_PASSWORD";
const char* API_ENDPOINT = "https://api.attune-iot.com/v1/ac/targets";

// =============================================================================
// ESP-NOW Telemetry Callback (Server-Side Ingestion)
// =============================================================================
void onTelemetryReceived(const uint8_t* mac, const UpstreamTelemetryPayload* payload) {
    if (currentRole != DeviceRole::SERVER_MASTER || modbus == nullptr) return;

    // Map incoming hardware MAC address to local whitelisted Node Index (1-19)
    uint8_t provisionedMacs[MAX_CLIENTS][6];
    size_t count = storage.getProvisionedClients(provisionedMacs, MAX_CLIENTS);
    
    uint8_t nodeIndex = 0;
    for (size_t i = 0; i < count; i++) {
        if (memcmp(provisionedMacs[i], mac, 6) == 0) {
            nodeIndex = i + 1; // Slide forward: Node Index matches Modbus Block location
            break;
        }
    }

    // If matching node slot is found, pack telemetry fields directly into Modbus memory
    if (nodeIndex > 0) {
        ModbusDeviceBlock block = {0};
        block.differential_pressure   = payload->differential_pressure;
        block.pm1_0                   = payload->pm1_0;
        block.pm2_5                   = payload->pm2_5;
        block.pm10                  = payload->pm10;
        block.particle_count          = payload->particle_count;
        block.actual_fan_speed        = payload->actual_fan_speed;
        block.active_local_target     = payload->active_local_target;
        block.status_bitfield         = payload->status_bitfield;
        block.seconds_since_telemetry = 0; // Reset network timeout counter
        block.remaining_manual_min    = payload->remaining_manual_min;

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
            }
        } 
        else if (input.startsWith("add ")) {
            String macStr = input.substring(4);
            uint8_t mac[6];
            if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
                if (storage.addClientMac(mac)) {
                    Serial.println("OK: Client Added to lowest available Slot Index. Reboot required.");
                } else {
                    Serial.println("ERR: Whitelist Full or Storage Error.");
                }
            } else {
                Serial.println("ERR: Invalid MAC format. Use XX:XX:XX:XX:XX:XX");
            }
        }
        else if (input.startsWith("remove ")) {
            String macStr = input.substring(7);
            uint8_t mac[6];
            if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
                &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
                if (storage.removeClientMac(mac)) {
                    Serial.println("OK: Client Removed. Target Slot is now vacant.");
                } else {
                    Serial.println("ERR: MAC Address Not Found.");
                }
            }
        }
        else {
            Serial.println("ERR: Unknown Command. Valid variants: 'add [MAC]', 'remove [MAC]', or 'list'.");
        }
    }
}

// =============================================================================
// Main System Orchestration Task (FreeRTOS 10Hz Execution Loop)
// =============================================================================
void systemOrchestrationTask(void* pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t loopFreq = pdMS_TO_TICKS(100); // 100ms cycle rate
    uint32_t minuteCounter = millis();

    for (;;) {
        uint32_t now = millis();

        // 1. Process USB CLI Stream (Server-Specific Only)
        if (currentRole == DeviceRole::SERVER_MASTER) {
            processCLI();
        }

        // 2. Evaluate User UI Interface / Local Manual Controls
        ButtonEvent btnEvent = buttonUI.getEvent();
        if (btnEvent == ButtonEvent::SHORT_PRESS) {
            uint16_t localSpeed = storage.getLocalTargetFanSpeed();
            localSpeed = (localSpeed + 25) > 100 ? 0 : localSpeed + 25; // 25% incremental loop steps
            storage.setLocalTargetFanSpeed(localSpeed);
            buttonUI.setManualOverride(true);
            log_i("Local Manual Override Asserted. Speed Shifted to: %d%%", localSpeed);
        } else if (btnEvent == ButtonEvent::QUINTUPLE_PRESS) {
            log_w("Factory Reset Signaled! Clearing NVS Whitelists and Device Configurations...");
            storage.format();
            ESP.restart();
        }

        // 3. Incrementally Tick Manual Timeout Counter (Minute Intervals)
        if (now - minuteCounter >= 60000) {
            buttonUI.decrementManualTimer();
            minuteCounter = now;
        }

        // 4. Target Control Priority Arbitration Logic (Strategy 1 Alignment)
        bool failSafe = (cloudSync && cloudSync->isFailSafeActive());
        
        if (failSafe) {
            activeTargetSpeed = 75;
            fanController.forceSpeedNow(75); // Immediate safety step override (bypasses linear ramping)
        } else if (buttonUI.isManualOverrideActive()) {
            activeTargetSpeed = storage.getLocalTargetFanSpeed();
            fanController.setTargetSpeed(activeTargetSpeed, storage.getRampTimeSeconds());
        } else {
            // Automatic Mode
            if (currentRole == DeviceRole::SERVER_MASTER) {
                // Read Cloud Target speed for Block 0 (the local server itself)
                activeTargetSpeed = modbus->getTargetSpeed(0); 
            } else {
                // Client Nodes read target assignments pushed downstream via network frames
                activeTargetSpeed = storage.getLocalTargetFanSpeed(); 
            }
            fanController.setTargetSpeed(activeTargetSpeed, storage.getRampTimeSeconds());
        }

        // 5. Hardware Safety: Continuous Fan Stall Alarm Evaluation
        if (now - lastStallCheckTime >= 1000) {
            uint8_t actualSpeed = tachScanner.getNormalizedSpeed();
            int16_t deviation = (int16_t)activeTargetSpeed - (int16_t)actualSpeed;
            
            // Fault condition: Actual feedback tracks > 10% lower than target execution state
            if (activeTargetSpeed > 0 && deviation > 10) {
                if (stallViolationStartTime == 0) stallViolationStartTime = now;
                if (now - stallViolationStartTime >= 5000) { // Continuous 5-second window rule
                    fanStallAlarmActive = true;
                    log_e("HARDWARE FAULT: Fan Stall Loop Violation detected!");
                }
            } else {
                stallViolationStartTime = 0;
                fanStallAlarmActive = false; // Self-recovery condition
            }
            lastStallCheckTime = now;
        }

        // 6. Assemble Status Bitfield Packages
        StatusBitfield status;
        status.raw = 0;
        status.bits.fail_safe_active = failSafe;
        status.bits.fan_stall_alarm   = fanStallAlarmActive;
        status.bits.manual_control    = buttonUI.isManualOverrideActive();

        // 7. Telemetry Ingestion & Downstream Target Inter-Node Distribution
        if (currentRole == DeviceRole::SERVER_MASTER) {
            // Package Block 0 (Server Local Cache Metrics)
            ModbusDeviceBlock block0 = {0};
            block0.active_local_target = activeTargetSpeed;
            block0.actual_fan_speed   = tachScanner.getNormalizedSpeed();
            block0.status_bitfield     = status.raw;
            block0.remaining_manual_min = buttonUI.getRemainingManualMinutes();
            modbus->updateTelemetryBlock(0, block0);

            // Downstream Distribution: Broadcast targets to linked clients every 10 seconds
            if (now % 10000 < 100) {
                uint8_t macs[MAX_CLIENTS][6];
                size_t count = storage.getProvisionedClients(macs, MAX_CLIENTS);
                for (size_t i = 0; i < count; i++) {
                    // Extract corresponding decoupled target speed block from Modbus Map
                    uint16_t individualClientTarget = modbus->getTargetSpeed(i + 1);
                    espNow->sendDownstreamTarget(macs[i], individualClientTarget, storage.getRampTimeSeconds(), failSafe);
                }
            }
        } else {
            // Client Node Upstream Telemetry Burst (Executed every 5 seconds)
            if (now % 5000 < 100) {
                UpstreamTelemetryPayload payload = {0};
                payload.header.protocol_magic[0] = 'A';
                payload.header.protocol_magic[1] = 'C';
                payload.header.protocol_magic[2] = 'S';
                payload.header.type = EspNowMessageType::UPSTREAM_TELEMETRY;
                
                payload.actual_fan_speed    = tachScanner.getNormalizedSpeed();
                payload.active_local_target = activeTargetSpeed;
                payload.status_bitfield     = status.raw;
                payload.remaining_manual_min = buttonUI.getRemainingManualMinutes();
                
                // Read local sensor peripherals and map values to structural fields
                payload.differential_pressure = pressureSensor.readPressurePascal() * 10;
                payload.pm1_0                 = particleSensor.getPM1_0() * 10;
                payload.pm2_5                 = particleSensor.getPM2_5() * 10;
                payload.pm10                  = particleSensor.getPM10() * 10;
                payload.particle_count        = particleSensor.getRawParticleCount();

                // Unicast packet directly to Master address node
                espNow->sendUpstreamTelemetry(espNow->getServerMacAddress(), &payload);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, loopFreq);
    }
}

// =============================================================================
// Operational Boot Hardware & Network Initializations
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(2000); // Guard window to stabilize serial diagnostics
    log_i("Initializing Firmware Framework v%d.%d", FW_VERSION_MAJOR, FW_VERSION_MINOR);

    // 1. Initialize NVS Framework Component Layouts
    if (!storage.begin()) {
        log_e("HALT DETECTED: Non-Volatile Storage Failure.");
        while(1) delay(100);
    }
    currentRole = storage.getDeviceRole();

    // 2. Fire Up Native Local Hardware Drivers
    buttonUI.begin();
    pressureSensor.begin();
    particleSensor.begin();
    fanController.begin();
    tachScanner.begin();

    // 3. Bind and Spin Network Transport Infrastructure
    espNow = new EspNowEngine(&storage);
    if (!espNow->begin(ROUTER_SSID, currentRole)) {
        log_e("HALT DETECTED: RF Cohabitation Channel Alignment Failure.");
    }
    espNow->setTelemetryCallback(onTelemetryReceived);

    if (currentRole == DeviceRole::SERVER_MASTER) {
        log_i("Booting Master Node: Initializing Upstream Interfaces...");
        
        // Fire up Modbus Engine (Slave ID 1 default)
        modbus = new ModbusServer(1);
        modbus->begin();
        
        // Pass Modbus instance pointer reference into CloudSync for Strategy 1 target mapping
        cloudSync = new CloudSync(&storage, modbus);
        cloudSync->begin(ROUTER_SSID, ROUTER_PASS, API_ENDPOINT);
        
        Serial.println("Server Initialization Sequence Finalized. Console CLI Online.");
    } else {
        log_i("Booting Slave Node: Passive Client Target Mode Activated.");
    }

    // 4. Register and Launch Main System Orchestrator Task thread
    xTaskCreate(systemOrchestrationTask, "Sys_Orch_Task", 8192, NULL, 5, NULL);
}

void loop() {
    // The underlying FreeRTOS task handles system processing loops. 
    // Delete the background Arduino setup task wrapper to release stack space.
    vTaskDelete(NULL);
}
