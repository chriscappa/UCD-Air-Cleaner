//Implements NVS writes, reads, validation schemas, 
//and array indexing tracking inside the ESP32-S3's logical flash partitions.
#include "storage/StorageManager.h"

#define NVS_NAMESPACE "ac_sys_cfg"
#define KEY_ROLE      "dev_role"
#define KEY_TARGET    "tgt_speed"
#define KEY_RAMP      "ramp_time"
#define KEY_POLL      "poll_int"
#define KEY_MAC_ARRAY "mac_arr"

StorageManager::StorageManager() : _initialized(false) {
    memset(_clientMacs, 0, sizeof(_clientMacs));
}

StorageManager::~StorageManager() {
    if (_initialized) {
        _prefs.end();
    }
}

bool StorageManager::begin() {
    if (_initialized) return true;
    
    if (!_prefs.begin(NVS_NAMESPACE, false)) {
        log_e("NVS Initialization Failed!");
        return false;
    }
    
    _initialized = true;
    loadMacList();
    return true;
}

void StorageManager::format() {
    if (!_initialized) return;
    _prefs.clear();
    memset(_clientMacs, 0, sizeof(_clientMacs));
}

DeviceRole StorageManager::getDeviceRole() {
    return static_cast<DeviceRole>(_prefs.getUChar(KEY_ROLE, static_cast<uint8_t>(DeviceRole::CLIENT_SLAVE)));
}

void StorageManager::setDeviceRole(DeviceRole role) {
    _prefs.putUChar(KEY_ROLE, static_cast<uint8_t>(role));
}

uint16_t StorageManager::getLocalTargetFanSpeed() {
    return _prefs.getUShort(KEY_TARGET, 0);
}

void StorageManager::setLocalTargetFanSpeed(uint16_t speed) {
    _prefs.putUShort(KEY_TARGET, speed);
}

uint16_t StorageManager::getRampTimeSeconds() {
    return _prefs.getUShort(KEY_RAMP, 10);
}

void StorageManager::setRampTimeSeconds(uint16_t seconds) {
    _prefs.putUShort(KEY_RAMP, seconds);
}

uint16_t StorageManager::getApiPollInterval() {
    return _prefs.getUShort(KEY_POLL, 60);
}

void StorageManager::setApiPollInterval(uint16_t interval) {
    _prefs.putUShort(KEY_POLL, interval);
}

// =============================================================================
// Fixed-Slot MAC Array Operations
// =============================================================================

void StorageManager::loadMacList() {
    // Always load the full block sizing representing the entire map slot array
    _prefs.getBytes(KEY_MAC_ARRAY, _clientMacs, sizeof(_clientMacs));
}

void StorageManager::saveMacList() {
    _prefs.putBytes(KEY_MAC_ARRAY, _clientMacs, sizeof(_clientMacs));
}

bool StorageManager::addClientMac(const uint8_t* mac) {
    if (isMacProvisioned(mac)) {
        return true; 
    }
    
    // Scan array for the lowest vacant slot (where MAC is all zeros)
    uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};
    int targetSlot = -1;

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (memcmp(_clientMacs[i], zeroMac, 6) == 0) {
            targetSlot = i;
            break; // Grab lowest vacancy open
        }
    }

    if (targetSlot == -1) {
        log_e("Cannot provision client: All slots are occupied.");
        return false;
    }
    
    memcpy(_clientMacs[targetSlot], mac, 6);
    saveMacList();
    log_i("Mac provisioned successfully in Slot Index: %d", targetSlot + 1);
    return true;
}

bool StorageManager::removeClientMac(const uint8_t* mac) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (memcmp(_clientMacs[i], mac, 6) == 0) {
            // Nullify the address space to break the link, but DO NOT shift components
            memset(_clientMacs[i], 0, 6);
            saveMacList();
            log_i("Mac removed successfully from Slot Index: %d", i + 1);
            return true;
        }
    }
    return false; 
}

size_t StorageManager::getProvisionedClients(uint8_t macBuffer[][6], size_t maxClients) {
    size_t countToCopy = min((size_t)MAX_CLIENTS, maxClients);
    for (size_t i = 0; i < countToCopy; i++) {
        memcpy(macBuffer[i], _clientMacs[i], 6);
    }
    return countToCopy; // Returns the full block size (empty slots are zeroed out)
}

bool StorageManager::isMacProvisioned(const uint8_t* mac) {
    uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};
    if (memcmp(mac, zeroMac, 6) == 0) return false;

    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (memcmp(_clientMacs[i], mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

size_t StorageManager::getClientCount() {
    size_t activeCount = 0;
    uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};
    
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (memcmp(_clientMacs[i], zeroMac, 6) != 0) {
            activeCount++;
        }
    }
    return activeCount;
}
