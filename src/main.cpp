#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <esp_sleep.h>
#include <time.h>  // để lấy thời gian NTP

#include "states.h"      // enum State
#include "ble_utils.h"
#include "wifi_utils.h"
#include "mqtt_utils.h"

// Extern variables for weekly and monthly water usage
float total_water_weekly = 0;
float total_water_monthly = 0;

//-----------------------------------
// Global variables
//-----------------------------------
bool bleEnabled         = false;
bool isWifiConnected    = false;
bool apEnabled          = false;

// WiFi credentials
String ssid_new         = "thong";
String password_new     = "123457890";

// Initialize state
State currentState = KET_NOI_WIFI;

// MQTT configuration
const char* mqtt_server   = "app.coreiot.io";
int         mqtt_port     = 1883;
const char* mqtt_username = "";
const char* mqtt_password = "App123456@";

// Create WiFiClientSecure, PubSubClient, and AsyncWebServer
WiFiClientSecure wifiClient;
PubSubClient     client(wifiClient);
AsyncWebServer   server(80);

//-----------------------------------
// Flow sensor parameters
//-----------------------------------
#define FLOW_SENSOR_PIN GPIO_NUM_2
#define WAKE_GPIO_PIN   GPIO_NUM_5

volatile int pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
static const float calibrationFactor = 450.0;

// For no water detection
bool noWaterDetected = false;
unsigned long noWaterStartTime = 0;
const unsigned long noWaterDuration = 10 * 60 * 1000; // 10 minutes

// Accumulative meterReading over 10-minute cycles
volatile unsigned long meterReading = 0;

//-----------------------------------
// ISR for flow sensor pulse counting
//-----------------------------------
void IRAM_ATTR pulseCounter() {
    unsigned long currentTime = millis();
    if (currentTime - lastPulseTime > 1) {
        pulseCount++;
        lastPulseTime = currentTime;
    }
}

//-----------------------------------
// Read flow sensor, update meterReading, and reset pulseCount
//-----------------------------------
float readFlowRate() {
    noInterrupts();
    int pulses = pulseCount;
    pulseCount = 0;
    interrupts();
    
    // Compute instantaneous flow rate (liters/min) from pulses.
    float flowRate = (pulses / calibrationFactor) * 60.0;
    total_water_weekly += flowRate;
    meterReading += pulses;  // accumulate pulses for 10 minutes cycle
    return flowRate;
}

//-----------------------------------
// Update monthly total every 10 minutes from meterReading
//-----------------------------------
void updateMonthlyTotal() {
    // Convert meterReading to liters for the 10-minute period.
    float liters = meterReading / calibrationFactor;
    total_water_monthly += liters;
    Serial.print("Cycle added: ");
    Serial.print(liters);
    Serial.println(" liters to monthly total.");
    meterReading = 0;
}

//-----------------------------------
// Check time (via NTP) and reset total_water_monthly at 0:00 on the 1st day of month
//-----------------------------------
void checkMonthlyReset() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
         Serial.println("Failed to obtain time");
         return;
    }
    static int lastResetMonth = -1;
    // Kiểm tra nếu đang là ngày 1, giờ 0, phút dưới 1 và chưa reset tháng này
    if (timeinfo.tm_mday == 1 && timeinfo.tm_hour == 0 && timeinfo.tm_min < 1) {
         if (lastResetMonth != timeinfo.tm_mon) {
             total_water_monthly = 0;
             lastResetMonth = timeinfo.tm_mon;
             Serial.println("Monthly total reset at 0:00 on day 1.");
         }
    }
}

//-----------------------------------
// Check if no water (flowRate < 1) and then eventually deep sleep
//-----------------------------------
void handleNoWaterSleepMode(float flowRate) {
    if (flowRate < 1) {
        if (!noWaterDetected) {
            noWaterStartTime = millis();
            noWaterDetected  = true;
            Serial.println("No water detected. Starting countdown to Sleep Mode...");
        } else if (millis() - noWaterStartTime > noWaterDuration) {
            Serial.println("No water for 10 minutes. Entering Sleep Mode...");
            esp_deep_sleep_start();
        }
    } else {
        noWaterDetected = false;
    }
}

//-----------------------------------
// Print wakeup reason from deep sleep
//-----------------------------------
void printWakeUpReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0: 
            Serial.println("Wakeup caused by external signal using RTC_IO"); break;
        case ESP_SLEEP_WAKEUP_EXT1: 
            Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
        default: 
            Serial.println("Wakeup not caused by external signal."); break;
    }
}

//-----------------------------------
// Setup()
//-----------------------------------
void setup() {
    Serial.begin(115200);
    printWakeUpReason();
    
    // Optionally configure NTP: đặt lại múi giờ, server NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
    
    if (!SPIFFS.begin(true)) {
        Serial.println("Failed to mount SPIFFS");
    }
    
    // Configure MQTT
    wifiClient.setInsecure();
    client.setCallback(mqttCallback);
    
    // Set initial state to try connecting WiFi
    currentState = KET_NOI_WIFI;
}

//-----------------------------------
// Loop()
//-----------------------------------
void loop() {
    static unsigned long lastReadTime   = 0;
    static unsigned long lastUpdateTime = 0;
    static unsigned long lastMeterTime  = 0;  // for updating monthly total every 10 minutes

    // Every 1 second: read sensor and update meterReading
    if (millis() - lastReadTime > 1000) {
        lastReadTime = millis();
        float flowRate = readFlowRate();
        
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
    
    // Every 10 minutes, update monthly total
    if (millis() - lastMeterTime >= 600000) { // 600,000 ms = 10 minutes
        updateMonthlyTotal();
        lastMeterTime = millis();
    }
    
    // If WiFi is connected, check for monthly reset
    if (WiFi.status() == WL_CONNECTED) {
        checkMonthlyReset();
    }
    
    // Process state machine every 100ms
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
                if (!client.connected()) {
                    connectToMqtt();
                }
                if (WiFi.status() != WL_CONNECTED) {
                    Serial.println("WiFi lost. Returning to BLE mode.");
                    currentState = CHUA_CO_KET_NOI;
                    isWifiConnected = false;
                }
                else if (SPIFFS.exists("/data.log")) {
                    Serial.println("Data found in flash. Switching to SEND_FLASH_DATA state.");
                    currentState = SEND_FLASH_DATA;
                }
                else {
                    Serial.println("No data in flash. Switching to GUI_DU_LIEU_MQTT state.");
                    currentState = GUI_DU_LIEU_MQTT;
                }
                break;
            case SEND_FLASH_DATA: {
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
                currentState = CHUA_CO_KET_NOI;
                break;
        }
        lastUpdateTime = millis();
    }
}
