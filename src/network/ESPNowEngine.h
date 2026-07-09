//This class manages the MAC-layer peer-to-peer network. 
//It handles dynamic Wi-Fi channel alignment to prevent 2.4 GHz 
//interference with the upstream cellular router.
#pragma once

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "CommonDefs.h"
#include "DataModels.h"
#include "storage/StorageManager.h"

class EspNowEngine {
public:
    EspNowEngine(StorageManager* storage);
    
    // Aligns the ESP32 radio channel to the target Wi-Fi network before initializing ESP-NOW
    bool begin(const char* routerSSID, DeviceRole role);

    // Server-side: Broadcast targets to a specific provisioned client
    bool sendDownstreamTarget(const uint8_t* clientMac, uint16_t targetSpeed, uint16_t rampTime, bool forceFailSafe);

    // Client-side: Report local state back to the Server
    bool sendUpstreamTelemetry(const uint8_t* serverMac, const UpstreamTelemetryPayload& telemetry);

    // Callback attachment points for the main application layer
    typedef void (*TelemetryCallback)(const uint8_t* mac, const UpstreamTelemetryPayload* payload);
    void setTelemetryCallback(TelemetryCallback cb);

private:
    StorageManager* _storage;
    DeviceRole _role;
    TelemetryCallback _telemetryCb;

    bool alignWiFiChannel(const char* ssid);
    void registerPeers();

    // ESP-NOW internal callbacks
    static void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len);
    
    static EspNowEngine* _instance;
};
