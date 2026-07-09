//The persistent storage engine manages non-volatile configuration parameters 
//(Ramp Times, Client Whitelists, API parameters) across ESP32-S3 deep power cycles and OTA updates.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "CommonDefs.h"

class StorageManager {
public:
    StorageManager();
    ~StorageManager();

    bool begin();
    void format();

    // System Settings
    DeviceRole getDeviceRole();
    void setDeviceRole(DeviceRole role);
    
    uint16_t getLocalTargetFanSpeed();
    void setLocalTargetFanSpeed(uint16_t speed);

    uint16_t getRampTimeSeconds();
    void setRampTimeSeconds(uint16_t seconds);

    uint16_t getApiPollInterval();
    void setApiPollInterval(uint16_t interval);

    // Dynamic Provisioning List (Client MAC Database)
    bool addClientMac(const uint8_t* mac);
    bool removeClientMac(const uint8_t* mac);
    size_t getProvisionedClients(uint8_t macBuffer[][6], size_t maxClients);
    bool isMacProvisioned(const uint8_t* mac);
    size_t getClientCount();

private:
    Preferences _prefs;
    bool _initialized;
    
    // Internal manifest structures to keep track of whitelisted ESP-NOW MAC addresses
    void loadMacList();
    void saveMacList();
    
    uint8_t _clientMacs[MAX_CLIENTS][6];
    size_t _activeClientCount;
};
