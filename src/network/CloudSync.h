//This class runs exclusively on the Server AC, 
//polling a remote REST endpoint over the 2.4 GHz Wi-Fi link.
//It tracks consecutive failures and manages the declaration of the 75% fail-safe state.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "CommonDefs.h"
#include "storage/StorageManager.h"
#include "network/ModbusServer.h" // Added dependency

class CloudSync {
public:
    // Updated Constructor signature to take ModbusServer pointer
    CloudSync(StorageManager* storage, ModbusServer* modbus);
    void begin(const char* ssid, const char* password, const char* apiEndpoint);
    
    bool isFailSafeActive();

private:
    static void syncTask(void* pvParameters);
    bool performApiPoll();

    StorageManager* _storage;
    ModbusServer* _modbus; // Pointer to shared register map cache
    String _apiEndpoint;
    
    uint16_t _consecutiveFailures;
    uint32_t _lastSuccessfulSyncTime;
    bool _failSafeActive;
};
