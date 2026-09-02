#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

#define SERVICE_UUID        "4fa8691a-1360-4c7f-8e3b-6e45a0d70000"
#define TELEMETRY_CHAR_UUID "4fa8691a-1360-4c7f-8e3b-6e45a0d70001"
#define COMMAND_CHAR_UUID   "4fa8691a-1360-4c7f-8e3b-6e45a0d70002"

typedef void (*CommandCallback)(const String& jsonCmd);

class BleManager : public BLEServerCallbacks, public BLECharacteristicCallbacks {
private:
    BLEServer* pServer = nullptr;
    BLECharacteristic* pTelemetryChar = nullptr;
    BLECharacteristic* pCommandChar = nullptr;
    bool deviceConnected = false;
    CommandCallback onCommandReceived = nullptr;

public:
    void begin(const String& deviceName, CommandCallback cmdCb) {
        onCommandReceived = cmdCb;
        BLEDevice::init(deviceName.c_str());

        pServer = BLEDevice::createServer();
        pServer->setCallbacks(this);

        BLEService* pService = pServer->createService(SERVICE_UUID);

        // Telemetry Characteristic (Read/Notify)
        pTelemetryChar = pService->createCharacteristic(
            TELEMETRY_CHAR_UUID,
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
        );
        pTelemetryChar->addDescriptor(new BLE2902());

        // Command Characteristic (Write)
        pCommandChar = pService->createCharacteristic(
            COMMAND_CHAR_UUID,
            BLECharacteristic::PROPERTY_WRITE
        );
        pCommandChar->setCallbacks(this);

        pService->start();

        BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->setScanResponse(true);
        pAdvertising->setMinPreferred(0x06);
        pAdvertising->setMinPreferred(0x12);
        BLEDevice::startAdvertising();
    }

    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        BLEDevice::startAdvertising(); // Restart advertising for new connections
    }

    void onWrite(BLECharacteristic* pCharacteristic) override {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0 && onCommandReceived != nullptr) {
            onCommandReceived(rxValue);
        }
    }

    void notifyTelemetry(const String& jsonPayload) {
        if (deviceConnected && pTelemetryChar != nullptr) {
            pTelemetryChar->setValue(jsonPayload.c_str());
            pTelemetryChar->notify();
        }
    }

    bool isConnected() { return deviceConnected; }
};

#endif
