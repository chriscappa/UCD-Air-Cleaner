#include "network/CloudSync.h"
#include <ArduinoJson.h>

CloudSync::CloudSync(StorageManager* storage) 
    : _storage(storage), _globalTargetSpeed(0), 
      _consecutiveFailures(0), _lastSuccessfulSyncTime(0), _failSafeActive(false) {}

void CloudSync::begin(const char* ssid, const char* password, const char* apiEndpoint) {
    if (_storage->getDeviceRole() != DeviceRole::SERVER_MASTER) return;

    _apiEndpoint = apiEndpoint;

    WiFi.begin(ssid, password);
    // Note: EspNowEngine handles the channel locking prior to this point. 
    // We just provide credentials here to associate with the Cradlepoint router.

    xTaskCreate(syncTask, "Cloud_Sync_Task", 8192, this, 2, NULL);
}

bool CloudSync::isFailSafeActive() {
    return _failSafeActive;
}

uint16_t CloudSync::getGlobalTargetSpeed() {
    return _failSafeActive ? 75 : _globalTargetSpeed;
}

bool CloudSync::performApiPoll() {
    if (WiFi.status() != WL_CONNECTED) {
        log_w("WiFi disconnected during API poll.");
        return false;
    }

    HTTPClient http;
    http.begin(_apiEndpoint);
    http.setTimeout(5000); // 5-second timeout

    int httpResponseCode = http.GET();
    bool success = false;

    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error && doc.containsKey("target_fan_speed")) {
            _globalTargetSpeed = doc["target_fan_speed"].as<uint16_t>();
            success = true;
        }
    } else {
        log_w("API Poll Failed. HTTP Code: %d", httpResponseCode);
    }

    http.end();
    return success;
}

void CloudSync::syncTask(void* pvParameters) {
    CloudSync* sync = static_cast<CloudSync*>(pvParameters);
    
    // Wait for initial Wi-Fi connection
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    sync->_lastSuccessfulSyncTime = millis();

    for (;;) {
        uint16_t pollInterval = sync->_storage->getApiPollInterval();
        if (pollInterval < 10) pollInterval = 10; // Enforce minimum 10s polling

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

        // Evaluate Fail-Safe Triggers
        // 1. 10 consecutive connection attempts fail
        // 2. 10 minutes pass with no response
        uint32_t msSinceLastSync = now - sync->_lastSuccessfulSyncTime;
        if (!sync->_failSafeActive && 
           (sync->_consecutiveFailures >= 10 || msSinceLastSync >= (10 * 60 * 1000))) {
            
            log_e("EMERGENCY: Cloud connectivity lost. Entering Fail-Safe Mode (75%% overriding).");
            sync->_failSafeActive = true;
        }

        vTaskDelay(pdMS_TO_TICKS(pollInterval * 1000));
    }
}
