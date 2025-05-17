#include "ble_utils.h"
#include "states.h"
#include <FS.h>
#include <SPIFFS.h>
#include <string>

// extern từ main.cpp
extern bool   bleEnabled;
extern String ssid_new;
extern String password_new;
extern String id_new;
extern State  currentState;

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        // Đọc raw payload
        std::string raw = pCharacteristic->getValue();
        Serial.printf("BLE RX raw (len=%u): ", raw.length());
        Serial.println(raw.c_str());

        // Chuyển sang String và chuẩn hóa dấu phân cách (dấu phẩy -> dấu chấm phẩy)
        String value = String(raw.c_str());
        value.replace(',', ';');
        Serial.println("[BLE] normalized: " + value);

        // Parse khi có đủ 3 trường: ssid, psw, id
        if (value.startsWith("ssid:") &&
            value.indexOf(";psw:") != -1 &&
            value.indexOf(";id:")  != -1) {

            int ssidStart = value.indexOf("ssid:") + 5;
            int pswStart  = value.indexOf(";psw:")  + 5;
            int idStart   = value.indexOf(";id:")   + 4;
            int ssidEnd   = value.indexOf(";", ssidStart);
            int pswEnd    = value.indexOf(";", pswStart);

            String ssid     = value.substring(ssidStart, ssidEnd);
            String password = value.substring(pswStart, pswEnd);
            String devId    = value.substring(idStart);

            ssid_new     = ssid;
            password_new = password;
            id_new       = devId;

            // Lưu ID vào SPIFFS
            File f = SPIFFS.open("/device_id.txt", FILE_WRITE);
            if (f) {
                f.println(id_new);
                f.close();
                Serial.println("Saved device ID to /device_id.txt");
            } else {
                Serial.println("Failed to open /device_id.txt");
            }

            currentState = KET_NOI_WIFI;
        } else {
            Serial.println("Invalid BLE payload");
        }
    }
};

void initBLE() {
    if (!bleEnabled) {
        SPIFFS.begin(true);
        BLEDevice::init("XIAO_ESP32S3");
        auto *pServer  = BLEDevice::createServer();
        auto *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
        auto *pChar    = pService->createCharacteristic(
            "beb5483e-36e1-4688-b7f5-ea07361b26a8",
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
        );
        pChar->setCallbacks(new MyCallbacks());
        pService->start();
        BLEDevice::getAdvertising()->addServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
        BLEDevice::startAdvertising();
        bleEnabled = true;
        Serial.println("BLE enabled");
    }
}

void stopBLE() {
    if (bleEnabled) {
        BLEDevice::deinit();
        bleEnabled = false;
        Serial.println("BLE disabled");
    }
}

void handleStateCHUA_CO_KET_NOI() {
    static unsigned long lastAttemptTime = 0;
    if (millis() - lastAttemptTime > 500) {
        initBLE();
        currentState = KET_NOI_BLE;
        lastAttemptTime = millis();
    }
}