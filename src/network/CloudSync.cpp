#include "network/CloudSync.h"
#include <ArduinoJson.h>

// Updated Constructor mapping
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
}

bool CloudSync::performApiPoll() {
    if (WiFi.status() != WL_CONNECTED) {
        log_w("WiFi disconnected during API poll.");
        return false;
    }

    HTTPClient http;
    http.begin(_apiEndpoint);
    http.setTimeout(5000); 

    int httpResponseCode = http.GET();
    bool success = false;

    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        // Adjust JSON capacity size based on up to 20 block index entries
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.containsKey("targets")) {
            JsonArray targetsArray = doc["targets"].as<JsonArray>();
            
            for (JsonObject target : targetsArray) {
                if (target.containsKey("index") && target.containsKey("speed")) {
                    uint8_t index = target["index"].as<uint8_t>();
                    uint16_t speed = target["speed"].as<uint16_t>();
                    
                    // Route directly to our decoupled Modbus index block
                    if (index <= MAX_CLIENTS) {
                        _modbus->setTargetSpeedByIndex(index, speed);
                    }
                }
            }
            success = true;
        } else {
            log_e("JSON Parsing Error or missing targets array.");
        }
    } else {
        log_w("API Poll Failed. HTTP Code: %d", httpResponseCode);
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
                log_i("Cloud connection restored. Exiting Fail-Safe Mode.");
                sync->_failSafeActive = false;
            }
        } else {
            sync->_consecutiveFailures++;
            log_w("API Poll Failure %d/10", sync->_consecutiveFailures);
        }

        // Evaluate 10-strike connection failures or 10-minute flatline
        uint32_t msSinceLastSync = now - sync->_lastSuccessfulSyncTime;
        if (!sync->_failSafeActive && 
           (sync->_consecutiveFailures >= 10 || msSinceLastSync >= (10 * 60 * 1000))) {
            
            log_e("EMERGENCY: Cloud connectivity lost. Entering Fail-Safe Mode.");
            sync->_failSafeActive = true;
        }
   vTaskDelay(pdMS_TO_TICKS(pollInterval * 1000));
    }
}
