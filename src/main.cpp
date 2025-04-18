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
#include "ota_utils.h"

static unsigned long lastRead   = 0;
static unsigned long lastMeter  = 0;
static unsigned long lastFlow   = 0;
static unsigned long lastVolume = 0;
static unsigned long lastCheck  = 0;
static State lastState = CHUA_CO_KET_NOI;

// Extern variables for weekly and monthly water usage
float total_water_weekly   = 0;
float total_water_monthly  = 0;
float lastIntervalVolume   = 0;

//-----------------------------------
// Global variables
//-----------------------------------
bool bleEnabled      = false;
bool isWifiConnected = false;
bool apEnabled       = false;
String id_new;

String ssid_new     = "thong";
String password_new = "123457890";

State currentState = CHUA_CO_KET_NOI;

const char* mqtt_server   = "89ffa4ed74ef4736ba72d21bf3de00ab.s1.eu.hivemq.cloud";
int         mqtt_port     = 8883;
const char* mqtt_username = "testapp";
const char* mqtt_password = "Test123456@";

WiFiClientSecure wifiClient;
PubSubClient     client(wifiClient);
AsyncWebServer   server(80);

#define FLOW_SENSOR_PIN GPIO_NUM_2
volatile int           pulseCount    = 0;
volatile unsigned long lastPulseTime = 0;
static const float     calibrationFactor = 450.0;

bool noWaterDetected      = false;
unsigned long noWaterStartTime = 0;
const unsigned long noWaterDuration = 10 * 60 * 1000UL;

volatile unsigned long meterReading = 0;
float lastMeasuredFlowRate          = 0;

const unsigned long flowPublishInterval   = 5  * 60 * 1000UL;
const unsigned long volumePublishInterval = 20 * 60 * 1000UL;

// ISR
void IRAM_ATTR pulseCounter() {
  unsigned long now = millis();
  if (now - lastPulseTime > 1) {
    pulseCount++;
    lastPulseTime = now;
  }
}

// Read flow
float readFlowRate() {
  noInterrupts();
  int pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  float flowRate = (pulses / calibrationFactor) * 60.0;  // L/min
  total_water_weekly += flowRate;
  meterReading += pulses;
  Serial.printf("[Read] flowRate=%.2f L/min, pulses=%d\n", flowRate, pulses);
  return flowRate;
}

// Monthly update every 10m
void updateMonthlyTotal() {
  float liters = meterReading / calibrationFactor;
  lastIntervalVolume = liters;
  total_water_monthly += liters;
  Serial.printf("[10m Cycle] Added %.2f L to monthly total. New = %.2f L\n",
                liters, total_water_monthly);
  meterReading = 0;
  File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
  if (f) {
    f.printf("%.2f\n", total_water_monthly);
    f.close();
    Serial.println("[SPIFFS] monthly.txt saved");
  }
}

// Reset monthly at 0:00 day 1
void checkMonthlyReset() {
  struct tm t;
  if (!getLocalTime(&t)) {
    Serial.println("[Time] NTP sync failed");
    return;
  }
  static int lastMonth = -1;
  if (t.tm_mday == 1 && t.tm_hour == 0 && t.tm_min < 1) {
    if (lastMonth != t.tm_mon) {
      total_water_monthly = 0;
      lastMonth = t.tm_mon;
      Serial.println("[Reset] Monthly total reset at 00:00, day 1");
      File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
      if (f) {
        f.printf("%.2f\n", total_water_monthly);
        f.close();
        Serial.println("[SPIFFS] monthly.txt reset saved");
      }
    }
  }
}

// No water -> deep sleep
void handleNoWaterSleepMode(float flowRate) {
  if (flowRate < 1.0) {
    if (!noWaterDetected) {
      noWaterStartTime = millis();
      noWaterDetected = true;
      Serial.println("[NoWater] Low flow detected, starting 10min countdown");
    } else if (millis() - noWaterStartTime > noWaterDuration) {
      Serial.println("[NoWater] 10min no water, preparing deep sleep");
      File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
      if (f) {
        f.printf("%.2f\n", total_water_monthly);
        f.close();
        Serial.println("[SPIFFS] monthly.txt saved before sleep");
      }
      Serial.println("[Sleep] Configuring wakeup and entering deep sleep");
      esp_sleep_enable_ext1_wakeup(1ULL << FLOW_SENSOR_PIN,
                                  ESP_EXT1_WAKEUP_ANY_HIGH);
      esp_deep_sleep_start();
    }
  } else {
    if (noWaterDetected) Serial.println("[NoWater] Flow resumed, cancel countdown");
    noWaterDetected = false;
  }
}

// Print wakeup reason
void printWakeUpReason() {
  auto cause = esp_sleep_get_wakeup_cause();
  Serial.printf("[Wakeup] cause=%d\n", cause);
}

void setup() {
  Serial.begin(115200);
  printWakeUpReason();
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] mount failed");
  } else {
    if (SPIFFS.exists("/device_id.txt")) {
      File f = SPIFFS.open("/device_id.txt", FILE_READ);
      id_new = f.readStringUntil('\n'); f.close();
      Serial.println("[SPIFFS] Loaded id_new: " + id_new);
    }
    if (SPIFFS.exists("/monthly.txt")) {
      File f = SPIFFS.open("/monthly.txt", FILE_READ);
      total_water_monthly = f.parseFloat(); f.close();
      Serial.printf("[SPIFFS] Loaded total_water_monthly=%.2f L\n",
                    total_water_monthly);
    }
  }
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN),
                  pulseCounter, RISING);
  wifiClient.setInsecure();
  client.setCallback(mqttCallback);
  clearWiFiCredentials();
  currentState = KET_NOI_WIFI;
  Serial.println("Start BLE");
}

void loop() {
  static unsigned long lastUpdateTime = 0;
  unsigned long now = millis();

  if (now - lastRead > 60000) {
    lastRead = now;
    lastMeasuredFlowRate = readFlowRate();
    handleNoWaterSleepMode(lastMeasuredFlowRate);
  }
  if (now - lastMeter >= 600000) { lastMeter = now; updateMonthlyTotal(); }
  if (now - lastFlow >= flowPublishInterval) {
    lastFlow = now;
    if (client.connected()) {
      String tp = "/data/" + id_new;
      String js = "{\"flowRate\":" + String(lastMeasuredFlowRate,2) + "}";
      client.publish(tp.c_str(), js.c_str());
      Serial.println("[Pub] flow JSON: " + js);
    }
  }
  if (now - lastVolume >= volumePublishInterval) {
    lastVolume = now;
    if (client.connected()) {
      String tp = "/data/" + id_new;
      String js = "{\"volume\":" + String(lastIntervalVolume,2)
                + ",\"total_monthly\":" + String(total_water_monthly,2)
                + "}";
      client.publish(tp.c_str(), js.c_str());
      Serial.println("[Pub] vol/month JSON: " + js);
    }
  }
  if (WiFi.status()==WL_CONNECTED) checkMonthlyReset();

  if (WiFi.status() == WL_CONNECTED) {
    checkMonthlyReset();
}

// Process state machine every 100ms
if (millis() - lastUpdateTime >= 100) {
    switch (currentState) {
        case CHUA_CO_KET_NOI:
            Serial.println("Chuaw ket noi");
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
