#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// Extern total water within weekly
extern float total_water_weekly; 

// Lưu/đọc/xoá WiFi credentials
void saveWiFiCredentials(const String& ssid, const String& password);
bool loadWiFiCredentials(String& ssid, String& password);
void clearWiFiCredentials();

// Trạng thái KET_NOI_WIFI
void handleStateKET_NOI_WIFI();

// Bật/Tắt Access Point, LAN
void enableAccessPoint();
void disableAccessPoint();
void enableLAN();

// Lưu/gửi data SPIFFS, publish flowRate
void saveDataToSPIFFS(const String& data);
bool sendSavedDataToMQTT();
void publishFlowRate(float flowRate);

#endif
