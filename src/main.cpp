#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <esp_sleep.h>
#include <time.h> // để lấy thời gian NTP

#include "states.h" // enum State
#include "ble_utils.h"
#include "wifi_utils.h"
#include "mqtt_utils.h"
#include "ota_utils.h"

// ——————————————————
//  Thời gian gửi data: 15 giây (cho test)
// ——————————————————
static const unsigned long DATA_PUBLISH_INTERVAL = 15UL * 1000UL; // 15000 ms = 15s

// Biến static giữa các lần loop
static unsigned long lastReadTime = 0;
static unsigned long lastMeterTime = 0;
static unsigned long lastPublishTime = 0;
static unsigned long lastCheckTime = 0;
static State lastState = KET_NOI_WIFI;

// Extern biến lưu lượng
float total_water_weekly = 0;
float total_water_monthly = 0;
float lastIntervalVolume = 0;

// ——————————————————
//  Global
// ——————————————————
bool bleEnabled = false;
bool isWifiConnected = false;
bool apEnabled = false;
String id_new;

String ssid_new = "";
String password_new = "";

State currentState = KET_NOI_WIFI;

const char *mqtt_server = "89ffa4ed74ef4736ba72d21bf3de00ab.s1.eu.hivemq.cloud";
int mqtt_port = 8883;
const char *mqtt_username = "testapp";
const char *mqtt_password = "Test123456@";

WiFiClientSecure wifiClient;
PubSubClient client(wifiClient);
AsyncWebServer server(80);

#define FLOW_SENSOR_PIN GPIO_NUM_2
volatile int pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
static const float calibrationFactor = 450;
String topic_test = "";
bool noWaterDetected = false;
unsigned long noWaterStartTime = 0;
const unsigned long noWaterDuration = 10UL * 60UL * 1000UL; // 10 phút

volatile unsigned long meterReading = 0;
float lastMeasuredFlowRate = 0;

// Define State Name

const char *stateNames[] = {
    "CHUA_CO_KET_NOI",
    "KET_NOI_BLE",
    "KET_NOI_WIFI",
    "KET_NOI_WIFI_THANH_CONG",
    "GUI_DU_LIEU_MQTT",
    "CHE_DO_BLE",
    "CHE_DO_LAN",
    "CHE_DO_AP",
    "SEND_FLASH_DATA",
};

// Get State Name
const char *getStateName(State s)
{
  size_t count = sizeof(stateNames) / sizeof(stateNames[0]);
  if ((int)s >= 0 && (size_t)s < count)
  {
    return stateNames[s];
  }
  return "UNKNOWN_STATE";
}

// ——————————————————
//  ISR đếm xung
// ——————————————————
void IRAM_ATTR pulseCounter()
{
  unsigned long now = millis();
  if (now - lastPulseTime > 1)
  {
    pulseCount++;
    lastPulseTime = now;
  }
}

// ——————————————————
//  Đọc flowRate, tích tụ meterReading
// ——————————————————
float readFlowRate()
{
  noInterrupts();
  int pulses = pulseCount;
  pulseCount = 0;
  interrupts();
  float flowRate = (pulses / calibrationFactor) * 60;
  // float flowRate = (pulses/5.5);
  total_water_weekly += flowRate;
  meterReading += pulses;
  //   Serial.printf("[Read] flowRate=%.2f L/min, pulses=%d\n", flowRate, pulses);
  return flowRate;
}

// ——————————————————
//  Cập nhật tổng tháng mỗi 10 phút
// ——————————————————
void updateMonthlyTotal()
{
  float liters = meterReading / calibrationFactor;
  lastIntervalVolume = liters;
  total_water_monthly += liters;
  Serial.printf("[10m] Added %.2f L -> monthly total %.2f L\n", liters, total_water_monthly);
  meterReading = 0;
  File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
  if (f)
  {
    f.printf("%.2f\n", total_water_monthly);
    f.close();
    Serial.println("[SPIFFS] monthly.txt saved");
  }
}

// ——————————————————
//  Reset tổng tháng vào 0h00 ngày 1
// ——————————————————
void checkMonthlyReset()
{
  struct tm t;
  if (!getLocalTime(&t))
    return;
  static int lastMonth = -1;
  if (t.tm_mday == 1 && t.tm_hour == 0 && t.tm_min < 1 && t.tm_mon != lastMonth)
  {
    total_water_monthly = 0;
    lastMonth = t.tm_mon;
    Serial.println("[Reset] Monthly total reset");
    File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
    if (f)
    {
      f.printf("%.2f\n", total_water_monthly);
      f.close();
      Serial.println("[SPIFFS] monthly.txt reset saved");
    }
  }
}

// ——————————————————
//  No water -> deep sleep
// ——————————————————
void handleNoWaterSleepMode(float flowRate)
{
  if (flowRate < 1.0)
  {
    if (!noWaterDetected)
    {
      noWaterStartTime = millis();
      noWaterDetected = true;
      Serial.println("[NoWater] start 10m countdown");
    }
    else if (millis() - noWaterStartTime > noWaterDuration)
    {
      Serial.println("[NoWater] 10m no water -> sleep");
      File f = SPIFFS.open("/monthly.txt", FILE_WRITE);
      if (f)
      {
        f.printf("%.2f\n", total_water_monthly);
        f.close();
      }
      esp_sleep_enable_ext1_wakeup(1ULL << FLOW_SENSOR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
      esp_deep_sleep_start();
    }
  }
  else
  {
    if (noWaterDetected)
      Serial.println("[NoWater] flow resumed");
    noWaterDetected = false;
  }
}

// ——————————————————
//  Print lý do wakeup
// ——————————————————
void printWakeUpReason()
{
  auto cause = esp_sleep_get_wakeup_cause();
  Serial.printf("[Wakeup] cause=%d\n", cause);
}

void setup()
{
  Serial.begin(115200);
  printWakeUpReason();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  if (!SPIFFS.begin(true))
    Serial.println("[SPIFFS] mount failed");
  else
  {
    if (SPIFFS.exists("/device_id.txt"))
    {
      File f = SPIFFS.open("/device_id.txt", FILE_READ);
      id_new = f.readStringUntil('\n');
      f.close();
      Serial.println("[SPIFFS] Loaded id_new: " + id_new);
    }
    if (SPIFFS.exists("/monthly.txt"))
    {
      File f = SPIFFS.open("/monthly.txt", FILE_READ);
      total_water_monthly = f.parseFloat();
      f.close();
      Serial.printf("[SPIFFS] Loaded monthly=%.2f L\n", total_water_monthly);
    }
  }
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, RISING);
  wifiClient.setInsecure();
  client.setCallback(mqttCallback);
  Serial.println("[Setup] BLE mode");
  // clearWiFiCredentials();
  currentState = KET_NOI_WIFI;
}

void loop()
{
  unsigned long now = millis();

  // 1s đọc flow
  if (now - lastReadTime >= 1000)
  {
    lastReadTime = now;
    lastMeasuredFlowRate = readFlowRate();
    handleNoWaterSleepMode(lastMeasuredFlowRate);
  }

  // 10m update monthly total (giữ nguyên lịch trình cũ)
  if (now - lastMeterTime >= 100000)
  {
    lastMeterTime = now;
    updateMonthlyTotal();
  }

  // 15s publish or save offline when flowRate >= 1 L/min
  if (now - lastPublishTime >= DATA_PUBLISH_INTERVAL)
  {
    lastPublishTime = now;

    // Chỉ xử lý khi có nước chảy (flowRate >= 1 L/min)
    if (lastMeasuredFlowRate >= 0)
    {
      id_new.trim();
      String topic = "datawater/" + id_new;

      String json = String("{\"flowRate\":") + String(lastMeasuredFlowRate, 2) + ",\"volume\":" + String(lastIntervalVolume, 2) + ",\"total_monthly\":" + String(total_water_monthly, 2) + "}";
      // Serial.println("Topic public " + topic);
      // Serial.println("Json public " + json);
      // Serial.println("Public wwith BLE wwifi");
      if (client.connected())
      {
        client.publish(topic.c_str(), json.c_str());
        Serial.println("[Pubblish] JSON: " + json);
      }
      else
      {
        saveDataToSPIFFS(json);
        Serial.println("[Offline] Saved JSON: " + json);
      }
    }
  }

  // Nếu Wi-Fi đang kết nối, kiểm tra xem có cần reset tổng tháng
  if (WiFi.status() == WL_CONNECTED)
  {
    checkMonthlyReset();
  }

  // State machine every 100ms
  if (now - lastCheckTime >= 100)
  {
    lastCheckTime = now;

    // In log khi chuyển state
    if (currentState != lastState)
    {
      Serial.printf("[State] %s -> %s\n",
                    getStateName(lastState),
                    getStateName(currentState));
      lastState = currentState;
    }
    switch (currentState)
    {
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
      // if (!isWifiConnected)
      // {
      //   isWifiConnected = true;
      //   Serial.println("[WiFi] connected");
      // }
      // if (!client.connected())
      // {
      //   connectToMqtt();
      // }
      // if (WiFi.status() != WL_CONNECTED)
      // {
      //   currentState = CHUA_CO_KET_NOI;
      //   isWifiConnected = false;
      // }
      // else if (SPIFFS.exists("/data.log"))
      // {
      //   currentState = SEND_FLASH_DATA;
      // }
      // else
      // {
      //   currentState = GUI_DU_LIEU_MQTT;
      // }
      // break;
      if (!isWifiConnected)
        isWifiConnected = true;
      if (!client.connected())
      {
        connectToMqtt();
      }
      if (WiFi.status() != WL_CONNECTED)
      {
        Serial.println("WiFi lost. Returning to BLE mode.");
        currentState = CHUA_CO_KET_NOI;
      }
      else if (SPIFFS.exists("/data.log"))
      {
        Serial.println("Data found in flash. Switching to SEND_FLASH_DATA state.");
        currentState = SEND_FLASH_DATA;
      }
      else
      {
        Serial.println("No have data in flash. Switching to GUI_DU_LIEU_MQTT state.");
        currentState = GUI_DU_LIEU_MQTT;
      }
      break;

    case SEND_FLASH_DATA:

      static unsigned long lastFlashSendTime = 0;
      if (millis() - lastFlashSendTime > 1000)
      { // Gửi dữ liệu từ SPIFFS mỗi 500ms
        lastFlashSendTime = millis();
        if (!sendSavedDataToMQTT())
        {
          currentState = GUI_DU_LIEU_MQTT;
        }
      }
      break;

    case GUI_DU_LIEU_MQTT:
      if (WiFi.status() != WL_CONNECTED)
      {
        currentState = CHUA_CO_KET_NOI;
        isWifiConnected = false;
        break;
      }

      if (!client.connected())
      {
        Serial.println("[MQTT] Lost connection. Reconnecting...");
        connectToMqtt();
      }
      else
      {
        client.loop();
      }
      break;

    case CHE_DO_BLE:
      initBLE();
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
  }
}
