//Implements NVS writes, reads, validation schemas, 
//and array indexing tracking inside the ESP32-S3's logical flash partitions.
#include "storage/StorageManager.h"

#define NVS_NAMESPACE "ac_sys_cfg"
#define KEY_ROLE      "dev_role"
#define KEY_TARGET    "tgt_speed"
#define KEY_RAMP      "ramp_time"
#define KEY_POLL      "poll_int"
#define KEY_MAC_COUNT "mac_cnt"
#define KEY_MAC_ARRAY "mac_arr"

StorageManager::StorageManager() : _initialized(false), _activeClientCount(0) {
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
    _activeClientCount = 0;
    memset(_clientMacs, 0, sizeof(_clientMacs));
}

DeviceRole StorageManager::getDeviceRole() {
    // Default to CLIENT_SLAVE unless explicitly provisioned
    return static_cast<DeviceRole>(_prefs.getUChar(KEY_ROLE, static_cast<uint8_t>(DeviceRole::CLIENT_SLAVE)));
}

void StorageManager::setDeviceRole(DeviceRole role) {
    _prefs.putUChar(KEY_ROLE, static_cast<uint8_t>(role));
}

uint16_t StorageManager::getLocalTargetFanSpeed() {
    return _prefs.getUShort(KEY_TARGET, 0); // Default to 0% (Off)
}

void StorageManager::setLocalTargetFanSpeed(uint16_t speed) {
    _prefs.putUShort(KEY_TARGET, speed);
}

uint16_t StorageManager::getRampTimeSeconds() {
    return _prefs.getUShort(KEY_RAMP, 10); // Default linear ramp is 10s
}

void StorageManager::setRampTimeSeconds(uint16_t seconds) {
    _prefs.putUShort(KEY_RAMP, seconds);
}

uint16_t StorageManager::getApiPollInterval() {
    return _prefs.getUShort(KEY_POLL, 60); // Default poll time is 60s
}

void StorageManager::setApiPollInterval(uint16_t interval) {
    _prefs.putUShort(KEY_POLL, interval);
}

// =============================================================================
// NVS MAC Provisioning Whitelist Operations
// =============================================================================

void StorageManager::loadMacList() {
    _activeClientCount = _prefs.getUShort(KEY_MAC_COUNT, 0);
    if (_activeClientCount > MAX_CLIENTS) {
        _activeClientCount = 0;
    }
    
    if (_activeClientCount > 0) {
        _prefs.getBytes(KEY_MAC_ARRAY, _clientMacs, _activeClientCount * 6);
    }
}

void StorageManager::saveMacList() {
    _prefs.putUShort(KEY_MAC_COUNT, _activeClientCount);
    if (_activeClientCount > 0) {
        _prefs.putBytes(KEY_MAC_ARRAY, _clientMacs, _activeClientCount * 6);
    } else {
        _prefs.remove(KEY_MAC_ARRAY);
    }
}

bool StorageManager::addClientMac(const uint8_t* mac) {
    if (isMacProvisioned(mac)) {
        return true; // Already registered
    }
    
    if (_activeClientCount >= MAX_CLIENTS) {
        log_e("Cannot provision client: Maximum limit reached (%d)", MAX_CLIENTS);
        return false;
    }
    
    memcpy(_clientMacs[_activeClientCount], mac, 6);
    _activeClientCount++;
    saveMacList();
    log_i("Mac provisioned successfully.");
    return true;
}

bool StorageManager::removeClientMac(const uint8_t* mac) {
    for (size_t i = 0; i < _activeClientCount; i++) {
        if (memcmp(_clientMacs[i], mac, 6) == 0) {
            // Shift array components left to maintain packing
            for (size_t j = i; j < _activeClientCount - 1; j++) {
                memcpy(_clientMacs[j], _clientMacs[j + 1], 6);
            }
            _activeClientCount--;
            memset(_clientMacs[_activeClientCount], 0, 6);
            saveMacList();
            log_i("Mac removed successfully.");
            return true;
        }
    }
    return false; // MAC address not found
}

size_t StorageManager::getProvisionedClients(uint8_t macBuffer[][6], size_t maxClients) {
    size_t countToCopy = min(_activeClientCount, maxClients);
    for (size_t i = 0; i < countToCopy; i++) {
        memcpy(macBuffer[i], _clientMacs[i], 6);
    }
    return countToCopy;
}

bool StorageManager::isMacProvisioned(const uint8_t* mac) {
    for (size_t i = 0; i < _activeClientCount; i++) {
        if (memcmp(_clientMacs[i], mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

size_t StorageManager::getClientCount() {
    return _activeClientCount;
}
