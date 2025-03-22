#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include <Arduino.h>

// Callback MQTT, kết nối MQTT
void mqttCallback(char* topic, byte* message, unsigned int length);
void connectToMqtt();

#endif
