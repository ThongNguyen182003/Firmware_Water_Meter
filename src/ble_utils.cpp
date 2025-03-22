#include "ble_utils.h"
#include "states.h"

// Biến, hàm dùng chung, ta "extern" từ main.cpp
extern bool bleEnabled;
extern String ssid_new;
extern String password_new;
extern State currentState;

// Callback BLE để nhận WiFi credentials
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();
      Serial.println("Value received: " + value);

      if (value.startsWith("ssid:") && value.indexOf(",psw:") != -1) {
          int ssidStart = value.indexOf("ssid:") + 5;
          int pswStart  = value.indexOf(",psw:") + 5;

          if (ssidStart < pswStart - 5 && pswStart < (int)value.length()) {
              String ssid     = value.substring(ssidStart, pswStart - 5);
              String password = value.substring(pswStart);

              Serial.println("Received WiFi credentials:");
              Serial.println("SSID: " + ssid);
              Serial.println("Password: " + password);

              ssid_new    = ssid;
              password_new= password;
              currentState= KET_NOI_WIFI; // chuyển qua kết nối WiFi
          } else {
              Serial.println("Invalid WiFi credentials format");
          }
      }
  }
};

void initBLE() {
    if (!bleEnabled) {
        BLEDevice::init("XIAO_ESP32S3");
        BLEServer *pServer   = BLEDevice::createServer();
        BLEService *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");

        BLECharacteristic *pCharacteristic = pService->createCharacteristic(
            "beb5483e-36e1-4688-b7f5-ea07361b26a8",
            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
        );
        pCharacteristic->setCallbacks(new MyCallbacks());
        pService->start();

        BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
        pAdvertising->setScanResponse(true);
        BLEDevice::startAdvertising();

        bleEnabled = true;
        Serial.println("BLE enabled and advertising");
    }
}

void stopBLE() {
    if (bleEnabled) {
        BLEDevice::deinit();
        bleEnabled = false;
        Serial.println("BLE disabled");
    }
}

// Khi state == CHUA_CO_KET_NOI => chuyển qua BLE
void handleStateCHUA_CO_KET_NOI() {
    static unsigned long lastAttemptTime = 0;
    if (millis() - lastAttemptTime > 500) {
        Serial.println("State: No have connection => init BLE");
        initBLE();
        currentState = KET_NOI_BLE;
        lastAttemptTime = millis();
    }
}
