#include "mqtt_utils.h"
#include <PubSubClient.h>
#include "ota_utils.h"    
#include "states.h"

// Biến, enum từ main.cpp
extern State        currentState;
extern PubSubClient client;
extern const char*  mqtt_server;
extern int          mqtt_port;
extern const char*  mqtt_username;
extern const char*  mqtt_password;
extern String       id_new;

// URL firmware OTA
static const char* OTA_URL = "http://10.28.128.17:3000/firmware.bin";

// Đăng ký subscribe cho các sub‑topic của /data/<id_new>/
static void subscribeDeviceTopics() {
    if (id_new.length() == 0) return;
    String base = "/data/" + id_new + "/";

    String cmdTopic = base + "command";
    client.subscribe(cmdTopic.c_str());
    Serial.printf("Subscribed to %s\n", cmdTopic.c_str());

    String otaTopic = base + "ota";
    client.subscribe(otaTopic.c_str());
    Serial.printf("Subscribed to %s\n", otaTopic.c_str());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String receivedTopic = String(topic);
    String messageValue;
    for (unsigned int i = 0; i < length; i++) {
        messageValue += (char)payload[i];
    }
    Serial.printf("MQTT >> topic: %s, payload: %s\n",
                  receivedTopic.c_str(), messageValue.c_str());

    String prefix = "/data/" + id_new + "/";
    if (receivedTopic.startsWith(prefix)) {
        String sub = receivedTopic.substring(prefix.length());

        if (sub == "command") {
            if (messageValue == "AC") {
                currentState = CHE_DO_AP;
            } else if (messageValue == "BLE") {
                currentState = CHE_DO_BLE;
            } else if (messageValue == "LAN") {
                currentState = CHE_DO_LAN;
            }
        }
        else if (sub == "ota") {
            Serial.println(">>> OTA trigger received, starting HTTP OTA update");
            // Gọi đúng prototype với URL
            performHttpOtaUpdate(OTA_URL);
        }
    }
}

void connectToMqtt() {
    static unsigned long lastAttemptTime = 0;

    // Đặt máy chủ MQTT
    client.setServer(mqtt_server, mqtt_port);

    // Kiểm tra nếu đã kết nối
    if (client.connected()) {
        Serial.println("MQTT connected.");
        client.subscribe("command"); // Đăng ký topic
        return;
    }

    // Nếu chưa kết nối, kiểm tra thời gian thử lại
    if (millis() - lastAttemptTime >= 2000) { // Mỗi 2 giây thử lại
        Serial.println("Attempting to connect to MQTT...");
        if (client.connect("ESP32Client", mqtt_username, mqtt_password)) {
            Serial.println("MQTT connected.");
            client.subscribe("command"); // Đăng ký topic sau khi kết nối thành công
        } else {
            Serial.print("Failed to connect to MQTT, state: ");
            Serial.println(client.state()); // Ghi lại trạng thái lỗi nếu có
        }
        lastAttemptTime = millis(); // Cập nhật thời gian lần thử tiếp theo
    }
}