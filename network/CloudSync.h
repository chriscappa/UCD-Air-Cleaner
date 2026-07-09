//This class runs exclusively on the Server AC, 
//polling a remote REST endpoint over the 2.4 GHz Wi-Fi link.
//It tracks consecutive failures and manages the declaration of the 75% fail-safe state.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "CommonDefs.h"
#include "storage/StorageManager.h"

class CloudSync {
public:
    CloudSync(StorageManager* storage);
    void begin(const char* ssid, const char* password, const char* apiEndpoint);
    
    // Returns true if the system is currently in a 10-strike fail-safe state
    bool isFailSafeActive();
    
    // Retrieve the latest fetched target speed from the cloud
    uint16_t getGlobalTargetSpeed();

private:
    static void syncTask(void* pvParameters);
    bool performApiPoll();

    StorageManager* _storage;
    String _apiEndpoint;
    
    uint16_t _globalTargetSpeed;
    
    uint16_t _consecutiveFailures;
    uint32_t _lastSuccessfulSyncTime;
    bool _failSafeActive;
};
