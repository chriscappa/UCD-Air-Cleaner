//This instantiates the HAL drivers and network interfaces, arbitrates control priorities 
//(Manual Override vs. Cloud Target vs. Fail-Safe), evaluates the 5-second Fan Stall hardware alarm, 
//and exposes the USB-C Plaintext CLI for client provisioning.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "CommonDefs.h"
#include "DataModels.h"
#include "storage/StorageManager.h"
#include "drivers/FanPWMController.h"
#include "drivers/TachMultiplexer.h"
#include "drivers/SDP810_Pressure.h"
#include "drivers/PMS5003_Particle.h"
#include "network/MeshEngine.h"
#include "network/ModbusServer.h"
#include "network/CloudSync.h"
#include "ui/ButtonInterface.h"
#include "network/BleManager.h"

// =============================================================================
// Global Instance Declarations
// =============================================================================
StorageManager storage;
ButtonInterface buttonUI;
FanPWMController fanController;
TachMultiplexer tachScanner(3000); // 3000 RPM maximum boundary normalization
SDP810_Pressure pressureSensor;
PMS5003_Particle particleSensor;

MeshEngine* meshEngine = nullptr;
ModbusServer* modbus = nullptr;
CloudSync* cloudSync = nullptr;
BleManager* bleManager = nullptr;

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
// Mesh Message Ingestion Handler (Replaces onTelemetryReceived)
// =============================================================================
void onMeshMessageReceived(uint32_t fromNode, String &msg) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
        log_e("Mesh JSON parsing failed: %s", error.c_str());
        return;
    }

    String msgType = doc["type"] | "";

    if (currentRole == DeviceRole::SERVER_MASTER && msgType == "telemetry") {
        // Find which node index slot corresponds to the sending Chip ID
        uint32_t senderId = doc["nodeId"];
        uint8_t nodeIndex = 0;
        
        // Use whitelists or map directly based on Chip ID mappings stored in storage
        // (Simplified placeholder mapping: using a hash or directly matching slot)
        nodeIndex = (senderId % MAX_CLIENTS) + 1; 

        if (modbus != nullptr) {
            ModbusDeviceBlock block = {0};
            block.differential_pressure   = doc["pressure"] | 0;
            block.pm1_0                   = doc["pm1_0"] | 0;
            block.pm2_5                   = doc["pm2_5"] | 0;
            block.pm10                    = doc["pm10"] | 0;
            block.particle_count          = doc["p_count"] | 0;
            block.actual_fan_speed        = doc["fan_spd"] | 0;
            block.active_local_target     = doc["tgt_spd"] | 0;
            block.status_bitfield         = doc["status"] | 0;
            block.seconds_since_telemetry = 0;
            block.remaining_manual_min    = doc["man_min"] | 0;

            modbus->updateTelemetryBlock(nodeIndex, block);
        }
    } else if (currentRole == DeviceRole::CLIENT_SLAVE && msgType == "sync") {
        // Master issued a downstream target update to the entire mesh
        bool forceFailSafe = doc["fail_safe"] | false;
        
        // Find if our specific nodeId has a targeted speed in the payload array
        uint32_t myId = meshEngine->getNodeId();
        JsonArray targetArray = doc["targets"].as<JsonArray>();
        
        uint16_t foundTarget = 0;
        bool targetMatched = false;

        for (JsonObject targetObj : targetArray) {
            if (targetObj["id"] == myId) {
                foundTarget = targetObj["speed"] | 0;
                targetMatched = true;
                break;
            }
        }

        if (forceFailSafe) {
            storage.setLocalTargetFanSpeed(75);
        } else if (targetMatched) {
            storage.setLocalTargetFanSpeed(foundTarget);
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
                activeTargetSpeed = modbus->getTargetSpeed(0); 
            } else {
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

            // Downstream JSON Broadcast to the mesh every 10 seconds
            if (now % 10000 < 100) {
                JsonDocument doc;
                doc["type"] = "sync";
                doc["fail_safe"] = failSafe;
                
                JsonArray targets = doc["targets"].to<JsonArray>();
                
                // Fetch each provisioned client and associate their speeds
                uint8_t macs[MAX_CLIENTS][6];
                size_t count = storage.getProvisionedClients(macs, MAX_CLIENTS);
                for (size_t i = 0; i < count; i++) {
                    uint16_t individualClientTarget = modbus->getTargetSpeed(i + 1);
                    
                    // Simple example of mapping: Using standard MAC derived indices
                    JsonObject targetNode = targets.add<JsonObject>();
                    // Convert last 4 bytes of MAC to form a simulated numeric Node ID
                    uint32_t nodeMeshId = (macs[i][2] << 24) | (macs[i][3] << 16) | (macs[i][4] << 8) | macs[i][5];
                    targetNode["id"] = nodeMeshId;
                    targetNode["speed"] = individualClientTarget;
                }
                
                String broadcastStr;
                serializeJson(doc, broadcastStr);
                meshEngine->broadcast(broadcastStr);
            }
        } else {
            // Client Node Upstream Telemetry JSON Package (Executed every 5 seconds)
            if (now % 5000 < 100) {
                JsonDocument doc;
                doc["type"] = "telemetry";
                doc["nodeId"] = meshEngine->getNodeId();
                doc["fan_spd"] = tachScanner.getNormalizedSpeed();
                doc["tgt_spd"] = activeTargetSpeed;
                doc["status"] = status.raw;
                doc["man_min"] = buttonUI.getRemainingManualMinutes();
                
                int16_t pressureVal = 0;
                if (pressureSensor.readPressure(pressureVal)) {
                    doc["pressure"] = pressureVal;
                }
                
                uint16_t p1 = 0, p25 = 0, p10 = 0, rawCount = 0;
                if (particleSensor.readData(p1, p25, p10, rawCount)) {
                    doc["pm1_0"] = p1;
                    doc["pm2_5"] = p25;
                    doc["pm10"] = p10;
                    doc["p_count"] = rawCount;
                }

                String telemetryStr;
                serializeJson(doc, telemetryStr);
                meshEngine->broadcast(telemetryStr);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, loopFreq);
    }
}

// =============================================================================
// Operational BLE Command Processing Function
// =============================================================================
void handleBleCommand(const String& jsonCmd) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonCmd);
    if (err) {
        log_e("Invalid BLE JSON command received");
        return;
    }

    String cmd = doc["cmd"] | "";

    if (cmd == "set_speed") {
        uint16_t speed = doc["value"] | 0;
        storage.setLocalTargetFanSpeed(speed);
        buttonUI.setManualOverride(true);
        log_i("BLE Command: Manual Speed set to %d%%", speed);
    } 
    else if (cmd == "add_client") {
        String macStr = doc["mac"] | "";
        uint8_t mac[6];
        if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
            if (storage.addClientMac(mac)) {
                log_i("BLE Command: Client MAC added successfully");
            }
        }
    } 
    else if (cmd == "remove_client") {
        String macStr = doc["mac"] | "";
        uint8_t mac[6];
        if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6) {
            storage.removeClientMac(mac);
            log_i("BLE Command: Client MAC removed");
        }
    } 
    else if (cmd == "reboot") {
        log_w("BLE Command: Rebooting node...");
        ESP.restart();
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

    // Initialize BLE Server
    bleManager = new BleManager();
    String deviceName = (currentRole == DeviceRole::SERVER_MASTER) ? "AC-Server-Node" : "AC-Client-Node";
    bleManager->begin(deviceName, handleBleCommand);

    // 2. Fire Up Native Local Hardware Drivers
    buttonUI.begin(); 
    pressureSensor.begin();
    particleSensor.begin();
    fanController.begin();
    tachScanner.begin();

    // 3. Bind and Spin Mesh Transport Infrastructure
    meshEngine = new MeshEngine();
    meshEngine->begin();
    meshEngine->setMessageCallback(onMeshMessageReceived);

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
    meshEngine->update(); // update mesh engine
    
    // The underlying FreeRTOS task handles system processing loops. 
    // Delete the background Arduino setup task wrapper to release stack space.
    vTaskDelete(NULL);
}
