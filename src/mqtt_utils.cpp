#include "mqtt_utils.h"
#include <PubSubClient.h>
#include "states.h"

// Lấy biến, enum từ main
extern State currentState;
extern PubSubClient client;
extern const char* mqtt_server;
extern int         mqtt_port;
extern const char* mqtt_username;
extern const char* mqtt_password;

// Callback MQTT khi nhận gói tin
void mqttCallback(char* topic, byte* message, unsigned int length) {
    String receivedTopic = String(topic);
    String messageValue;
    for (unsigned int i = 0; i < length; i++) {
        messageValue += (char)message[i];
    }
    Serial.print("Message Received: ");
    Serial.println(messageValue);

    if (receivedTopic == "command") {
        if (messageValue == "AC") {
            currentState = CHE_DO_AP;
        } else if (messageValue == "BLE") {
            currentState = CHE_DO_BLE;
        } else if (messageValue == "LAN") {
            currentState = CHE_DO_LAN;
        }
    }
}

// Kết nối MQTT
void connectToMqtt() {
    static unsigned long lastAttemptTime = 0;

    if (client.connected()) {
        client.subscribe("command");
        return;
    }
    if (millis() - lastAttemptTime >= 2000) {
        Serial.println("Attempting to connect to MQTT...");
        if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
            Serial.println("MQTT connected.");
            client.subscribe("command");
        } else {
            Serial.print("Failed to connect to MQTT, state: ");
            Serial.println(client.state());
        }
        lastAttemptTime = millis();
    }
}
