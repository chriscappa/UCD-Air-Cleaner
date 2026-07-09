#include "network/EspNowEngine.h"

EspNowEngine* EspNowEngine::_instance = nullptr;

EspNowEngine::EspNowEngine(StorageManager* storage) : _storage(storage), _telemetryCb(nullptr) {
    _instance = this;
}

bool EspNowEngine::alignWiFiChannel(const char* ssid) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    log_i("Scanning for upstream router: %s", ssid);
    int n = WiFi.scanNetworks();
    uint8_t targetChannel = 0;
    
    for (int i = 0; i < n; ++i) {
        if (strcmp(WiFi.SSID(i).c_str(), ssid) == 0) {
            targetChannel = WiFi.channel(i);
            break;
        }
    }
    
    WiFi.scanDelete();

    if (targetChannel == 0) {
        log_w("Router SSID not found. Defaulting to channel 1.");
        targetChannel = 1;
    } else {
        log_i("Router found. Locking radio to channel %d.", targetChannel);
    }

    // Force the baseband radio to the specific channel to eliminate contention
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    return true;
}

bool EspNowEngine::begin(const char* routerSSID, DeviceRole role) {
    _role = role;
    
    if (!alignWiFiChannel(routerSSID)) {
        return false;
    }

    if (esp_now_init() != ESP_OK) {
        log_e("Error initializing ESP-NOW");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    if (_role == DeviceRole::SERVER_MASTER) {
        registerPeers();
    }

    return true;
}

void EspNowEngine::registerPeers() {
    uint8_t macs[MAX_CLIENTS][6];
    size_t count = _storage->getProvisionedClients(macs, MAX_CLIENTS);
    
    for (size_t i = 0; i < count; i++) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, macs[i], 6);
        peerInfo.channel = 0; // Use current baseband channel
        peerInfo.encrypt = false; // Add PMK/LMK encryption here for production security
        
        if (esp_now_add_peer(&peerInfo) != ESP_OK) {
            log_e("Failed to add peer during initialization.");
        }
    }
}

bool EspNowEngine::sendDownstreamTarget(const uint8_t* clientMac, uint16_t targetSpeed, uint16_t rampTime, bool forceFailSafe) {
    DownstreamSyncPayload payload;
    payload.header.protocol_magic[0] = 'A';
    payload.header.protocol_magic[1] = 'C';
    payload.header.protocol_magic[2] = 'S';
    payload.header.type = EspNowMessageType::DOWNSTREAM_SYNC;
    payload.header.transaction_id = millis(); // Simple TX ID
    
    payload.target_fan_speed = targetSpeed;
    payload.ramp_time_seconds = rampTime;
    payload.force_fail_safe = forceFailSafe ? 1 : 0;

    esp_err_t result = esp_now_send(clientMac, (uint8_t*)&payload, sizeof(payload));
    return (result == ESP_OK);
}

bool EspNowEngine::sendUpstreamTelemetry(const uint8_t* serverMac, const UpstreamTelemetryPayload& telemetry) {
    esp_err_t result = esp_now_send(serverMac, (uint8_t*)&telemetry, sizeof(telemetry));
    return (result == ESP_OK);
}

void EspNowEngine::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Log TX failures for diagnostic registers
    if (status != ESP_NOW_SEND_SUCCESS) {
        log_v("ESP-NOW TX Failure to MAC %02X:%02X:%02X:%02X:%02X:%02X", 
              mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    }
}

void EspNowEngine::onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (len < sizeof(EspNowHeader)) return;

    const EspNowHeader* header = reinterpret_cast<const EspNowHeader*>(data);
    
    if (header->protocol_magic[0] != 'A' || header->protocol_magic[1] != 'C' || header->protocol_magic[2] != 'S') {
        return; // Invalid magic bytes
    }

    if (header->type == EspNowMessageType::UPSTREAM_TELEMETRY && _instance->_role == DeviceRole::SERVER_MASTER) {
        if (len == sizeof(UpstreamTelemetryPayload) && _instance->_telemetryCb) {
            const UpstreamTelemetryPayload* payload = reinterpret_cast<const UpstreamTelemetryPayload*>(data);
            _instance->_telemetryCb(mac_addr, payload);
        }
    }
    // Downstream targets and pairing handling are routed to the main task queue
}
