#include "network/EspNowEngine.h"

EspNowEngine* EspNowEngine::_instance = nullptr;

EspNowEngine::EspNowEngine(StorageManager* storage) : _storage(storage), _telemetryCb(nullptr) {
    _instance = this;
}

bool EspNowEngine::alignWiFiChannel(const char* ssid) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

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
        log_w("Upstream Router AP footprint not detected. Forcing channel 1 fallback.");
        targetChannel = 1;
    }

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
        log_e("Hardware Layer: Failed to initialize network ESP-NOW engine stack.");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    
    registerPeers();
    return true;
}

void EspNowEngine::registerPeers() {
    // Dynamically register broadcast or designated listening endpoints based on role definitions
    if (_role == DeviceRole::SERVER_MASTER) {
        uint8_t clientMacs[MAX_CLIENTS][6];
        size_t count = _storage->getProvisionedClients(clientMacs, MAX_CLIENTS);
        
        for (size_t i = 0; i < count; i++) {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, clientMacs[i], 6);
            peerInfo.channel = 0; // Lock dynamically to active channel
            peerInfo.encrypt = false;
            esp_now_add_peer(&peerInfo);
        }
    }
}

bool EspNowEngine::sendDownstreamTarget(const uint8_t* clientMac, uint16_t targetSpeed, uint16_t rampTime, bool forceFailSafe) {
    DownstreamSyncPayload payload;
    payload.header.protocol_magic[0] = 'A';
    payload.header.protocol_magic[1] = 'C';
    payload.header.protocol_magic[2] = 'S';
    payload.header.type = EspNowMessageType::DOWNSTREAM_SYNC;
    payload.header.transaction_id = millis();
    
    payload.target_fan_speed = targetSpeed;
    payload.ramp_time_seconds = rampTime;
    payload.force_fail_safe = forceFailSafe ? 1 : 0;

    // Direct registration check guard before issuing low-level transmission calls
    if (!esp_now_is_peer_exist(clientMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, clientMac, 6);
        esp_now_add_peer(&peerInfo);
    }

    esp_err_t result = esp_now_send(clientMac, (uint8_t*)&payload, sizeof(payload));
    return (result == ESP_OK);
}

bool EspNowEngine::sendUpstreamTelemetry(const uint8_t* serverMac, const UpstreamTelemetryPayload& telemetry) {
    if (!esp_now_is_peer_exist(serverMac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, serverMac, 6);
        esp_now_add_peer(&peerInfo);
    }
    esp_err_t result = esp_now_send(serverMac, (uint8_t*)&telemetry, sizeof(telemetry));
    return (result == ESP_OK);
}

void EspNowEngine::setTelemetryCallback(TelemetryCallback cb) {
    _telemetryCb = cb;
}

void EspNowEngine::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        log_v("ESP-NOW Packet dropped to destination MAC address.");
    }
}

void EspNowEngine::onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (_instance == nullptr || len < sizeof(EspNowHeader)) return;

    const EspNowHeader* header = reinterpret_cast<const EspNowHeader*>(data);
    if (header->protocol_magic[0] != 'A' || header->protocol_magic[1] != 'C' || header->protocol_magic[2] != 'S') return;

    // Security Verification: Server drops payloads arriving from non-provisioned nodes
    if (_instance->_role == DeviceRole::SERVER_MASTER) {
        if (!_instance->_storage->isMacProvisioned(mac_addr)) {
            log_w("Dropped unauthorized telemetry packet injection attempts from non-whitelisted MAC.");
            return;
        }
    }

    if (header->type == EspNowMessageType::UPSTREAM_TELEMETRY && _instance->_role == DeviceRole::SERVER_MASTER) {
        if (_instance->_telemetryCb != nullptr) {
            const UpstreamTelemetryPayload* telemetry = reinterpret_cast<const UpstreamTelemetryPayload*>(data);
            _instance->_telemetryCb(mac_addr, telemetry);
        }
    }
}
