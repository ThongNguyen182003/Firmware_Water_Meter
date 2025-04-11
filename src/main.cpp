#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

#include "states.h"      // enum State
#include "ble_utils.h"
#include "wifi_utils.h"
#include "mqtt_utils.h"

// Extern variable

float total_water_weekly = 0;
//-----------------------------------
// Biến toàn cục
//-----------------------------------
bool bleEnabled         = false;
bool isWifiConnected    = false;
bool apEnabled          = false;

// WiFi
String ssid_new         = "thong";
String password_new     = "123457890";

// Enum State (khởi tạo)
State currentState = KET_NOI_WIFI;

// MQTT
const char* mqtt_server   = "app.coreiot.io";
int         mqtt_port     = 1883;
const char* mqtt_username = "";
// const char* mqtt_password = "App123456@";

// Tạo wifiClient + MQTT client + WebServer
WiFiClientSecure wifiClient;
PubSubClient     client(wifiClient);
AsyncWebServer   server(80);

//-----------------------------------
// Cảm biến lưu lượng nước
//-----------------------------------
#define FLOW_SENSOR_PIN GPIO_NUM_2
#define WAKE_GPIO_PIN   GPIO_NUM_5

volatile int   pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
static const float calibrationFactor = 450.0;

bool noWaterDetected      = false;
unsigned long noWaterStartTime = 0;
const unsigned long noWaterDuration = 10 * 60 * 1000; // 2 phút

//-----------------------------------
// Đo và tính lưu lượng
//-----------------------------------
void IRAM_ATTR pulseCounter() {
    unsigned long currentTime = millis();
    if (currentTime - lastPulseTime > 1) {
        pulseCount++;
        lastPulseTime = currentTime;
    }
}

float readFlowRate() {
    float flowRate = (pulseCount / calibrationFactor) * 60.0;
    total_water_weekly += flowRate;
    pulseCount = 0;
    return flowRate;
}

//-----------------------------------
// If within 2 min, sensor haven't detected pwm => Sleep
//-----------------------------------
void handleNoWaterSleepMode(float flowRate) {
    if (flowRate < 1) {
        if (!noWaterDetected) {
            noWaterStartTime = millis();
            noWaterDetected  = true;
            Serial.println("No water detected. Starting countdown to Sleep Mode...");
        } else if (millis() - noWaterStartTime > noWaterDuration) {
            Serial.println("No water for 2 minutes. Entering Sleep Mode...");
            // esp_sleep_enable_ext1_wakeup(1ULL << FLOW_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
            esp_deep_sleep_start();
        }
    } else {
        noWaterDetected = false;
    }
}

//-----------------------------------
void printWakeUpReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
        case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
        default: Serial.println("Wakeup not caused by external signal."); break;
    }
}

//-----------------------------------
// setup()
//-----------------------------------
void setup() {
    Serial.begin(115200);
    printWakeUpReason();

    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);

    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to format and mount SPIFFS");
    }

    // Cấu hình MQTT
    wifiClient.setInsecure();  
    client.setCallback(mqttCallback);

    // Ban đầu => thử KET_NOI_WIFI
    currentState = KET_NOI_WIFI;
}

//-----------------------------------
// loop()
//-----------------------------------
void loop() {
    static unsigned long lastReadTime   = 0;
    static unsigned long lastUpdateTime = 0;

    // Đọc flow sensor mỗi 1s
    if (millis() - lastReadTime > 1000) {
        lastReadTime = millis();
        float flowRate = readFlowRate();

        // Nếu có lưu lượng đủ lớn => gửi
        if (flowRate > 1.5) {
            if (!isWifiConnected) {
                saveDataToSPIFFS(String(flowRate));
            } else if (client.connected()) {
                publishFlowRate(flowRate);
            } else {
                Serial.println("Client not connected. Saving to SPIFFS.");
                saveDataToSPIFFS(String(flowRate));
            }
        }
        handleNoWaterSleepMode(flowRate);
    }

    // Kiểm tra state machine ~ mỗi 100ms
    if (millis() - lastUpdateTime >= 100) {
        switch (currentState) {
            case CHUA_CO_KET_NOI:
                handleStateCHUA_CO_KET_NOI();
                break;

            case KET_NOI_BLE:
                initBLE();
                currentState = KET_NOI_BLE;
                break;

            case KET_NOI_WIFI:
                handleStateKET_NOI_WIFI();
                break;

            case KET_NOI_WIFI_THANH_CONG:
                if (!isWifiConnected) isWifiConnected = true;
                // Thử kết nối MQTT
                if (!client.connected()) {
                    connectToMqtt();
                }
                // Nếu WiFi mất => về BLE
                if (WiFi.status() != WL_CONNECTED) {
                    Serial.println("WiFi lost. Returning to BLE mode.");
                    currentState    = CHUA_CO_KET_NOI;
                    isWifiConnected = false;
                }
                // Nếu có data cũ => gửi
                else if (SPIFFS.exists("/data.log")) {
                    Serial.println("Data found in flash. Switching to SEND_FLASH_DATA state.");
                    currentState = SEND_FLASH_DATA;
                } else {
                    Serial.println("No data in flash. Switching to GUI_DU_LIEU_MQTT state.");
                    currentState = GUI_DU_LIEU_MQTT;
                }
                break;

            case SEND_FLASH_DATA:
            {
                static unsigned long lastFlashSendTime = 0;
                if (millis() - lastFlashSendTime > 1000) {
                    lastFlashSendTime = millis();
                    if (!sendSavedDataToMQTT()) {
                        currentState = GUI_DU_LIEU_MQTT;
                    }
                }
            }
            break;

            case GUI_DU_LIEU_MQTT:
                if (isWifiConnected) {
                    client.loop();
                }
                break;

            case CHE_DO_BLE:
                if (isWifiConnected) {
                    WiFi.disconnect();
                    isWifiConnected = false;
                }
                initBLE();
                currentState = KET_NOI_BLE;
                break;

            case CHE_DO_AP:
                stopBLE();
                enableAccessPoint();
                break;

            case CHE_DO_LAN:
                stopBLE();
                enableLAN();
                break;

            default:
                // Nếu rơi vào case lạ => quay về BLE
                currentState = CHUA_CO_KET_NOI;
                break;
        }
        lastUpdateTime = millis();
    }
}
