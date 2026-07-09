#include "network/CloudSync.h"
#include <ArduinoJson.h>

CloudSync::CloudSync(StorageManager* storage, ModbusServer* modbus) 
    : _storage(storage), _modbus(modbus), 
      _consecutiveFailures(0), _lastSuccessfulSyncTime(0), _failSafeActive(false) {}

void CloudSync::begin(const char* ssid, const char* password, const char* apiEndpoint) {
    if (_storage->getDeviceRole() != DeviceRole::SERVER_MASTER) return;

    _apiEndpoint = apiEndpoint;
    WiFi.begin(ssid, password);

    xTaskCreate(syncTask, "Cloud_Sync_Task", 8192, this, 2, NULL);
}

bool CloudSync::isFailSafeActive() {
    return _failSafeActive;
}}

bool CloudSync::performApiPoll() {
    if (WiFi.status() != WL_CONNECTED) {
        log_w("WiFi link disconnected during API network poll sequence.");
        return false;
    }

    HTTPClient http;
    http.begin(_apiEndpoint);
    http.setTimeout(5000); 

    int httpResponseCode = http.GET();
    bool success = false;

    if (httpResponseCode == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.containsKey("targets")) {
            JsonArray targetsArray = doc["targets"].as<JsonArray>();
            success = true;

            for (size_t i = 0; i < targetsArray.size(); i++) {
                if (targetsArray[i].containsKey("index") && targetsArray[i].containsKey("speed")) {
                    uint8_t idx = targetsArray[i]["index"];
                    uint16_t speed = targetsArray[i]["speed"];
                    uint16_t ramp = targetsArray[i].getMember("ramp").as<uint16_t>();
                    if (ramp == 0) ramp = 10; // Default fallback safety bounds

                    if (idx == 0) {
                        // Strategy 1 Node Decoupling: Index 0 belongs to Master's local profile
                        _storage->setLocalTargetFanSpeed(speed);
                        _storage->setRampTimeSeconds(ramp);
                    }
                    
                    // Always mirror targets down to the central Modbus shared register cache structure
                    if (_modbus != nullptr && idx < (MAX_CLIENTS + 1)) {
                        _modbus->setTargetSpeedByIndex(idx, speed);
                    }
                }
            }
        }
    } else {
        log_e("Cloud API Sync Network Fault. HTTP Status Return Code: %d", httpResponseCode);
    }

    http.end();
    return success;
}

void CloudSync::syncTask(void* pvParameters) {
    CloudSync* sync = static_cast<CloudSync*>(pvParameters);
    
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    sync->_lastSuccessfulSyncTime = millis();

    for (;;) {
        uint16_t pollInterval = sync->_storage->getApiPollInterval();
        if (pollInterval < 10) pollInterval = 10; 

        bool pollResult = sync->performApiPoll();
        uint32_t now = millis();

        if (pollResult) {
            sync->_consecutiveFailures = 0;
            sync->_lastSuccessfulSyncTime = now;
            
            if (sync->_failSafeActive) {
                log_i("Cloud syncing pipelines restored. Cleared active system Fail-Safes.");
                sync->_failSafeActive = false;
            }
        } else {
            sync->_consecutiveFailures++;
            log_w("API Sync Failure Counter Event Flag: %d/10", sync->_consecutiveFailures);
        }

        // Handle structural fail-safe declarations if network connection drops
        uint32_t msSinceLastSync = now - sync->_lastSuccessfulSyncTime;
        if (!sync->_failSafeActive && (sync->_consecutiveFailures >= 10 || msSinceLastSync >= (10 * 60 * 1000))) {
            log_e("CRITICAL: Cloud Pipeline Loss Detected. Injecting 75%% Flow Fail-Safe Command State Vectors.");
            sync->_failSafeActive = true;
        }

        vTaskDelay(pdMS_TO_TICKS(pollInterval * 1000));
    }
}
