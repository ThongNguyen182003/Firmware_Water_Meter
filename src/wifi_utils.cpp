#include "wifi_utils.h"
#include "states.h"
#include <PubSubClient.h>
#include <vector>

// Biến extern từ main
extern float total_water_monthly;
extern AsyncWebServer server;
extern bool apEnabled;
extern bool isWifiConnected;
extern String ssid_new, password_new;
extern State currentState;
extern String id_new;
// MQTT extern
extern PubSubClient client;

// --------------------------------
// Lưu WiFi credentials (atomic write)
// --------------------------------
void saveWiFiCredentials(const String &ssid, const String &password)
{
    // 1) Ghi ra file tạm
    File tmp = SPIFFS.open("/wifi.tmp", FILE_WRITE);
    if (!tmp)
    {
        Serial.println("[ SaveWiFiCredentials ]: Failed to open /wifi.tmp for writing");
        return;
    }
    tmp.println(ssid);
    tmp.println(password);
    tmp.close();

    // 2) Xoá file cũ và rename
    if (SPIFFS.remove("/wifi.txt"))
    {
        if (!SPIFFS.rename("/wifi.tmp", "/wifi.txt"))
        {
            Serial.println("[ SaveWiFiCredentials ] : Failed to rename /wifi.tmp → /wifi.txt");
            // Nếu rename lỗi, có thể giữ lại /wifi.tmp để debug
        }
        else
        {
            Serial.println(" [ SaveWiFiCredentials ] WiFi credentials saved to SPIFFS.");
        }
    }
    else
    {
        Serial.println("[ SaveWiFiCredentials ]: Failed to remove old /wifi.txt");
    }
}

// --------------------------------
// Đọc WiFi credentials với debug info
// --------------------------------
bool loadWiFiCredentials(String &ssid, String &password)
{
    File file = SPIFFS.open("/wifi.txt", FILE_READ);
    if (!file)
    {
        Serial.println("[ LoadWiFiCredentials ]: Failed to open /wifi.txt for reading");
        return false;
    }

    ssid = file.readStringUntil('\n');
    password = file.readStringUntil('\n');
    ssid.trim();
    password.trim();
    file.close();

    Serial.printf("[ LoadWiFiCredentials ] : SSID=\"%s\", Password=\"%s\"\n",
                  ssid.c_str(), password.c_str());
    // Chỉ trả về true khi cả hai đều không rỗng
    return !ssid.isEmpty() && !password.isEmpty();
}

void clearWiFiCredentials()
{
    if (SPIFFS.remove("/wifi.txt"))
    {
        Serial.println("WiFi credentials cleared from SPIFFS.");
    }
    else
    {
        Serial.println("Failed to clear WiFi credentials.");
    }
}

// --------------------------------
// Xử lý state KET_NOI_WIFI
// --------------------------------
void handleStateKET_NOI_WIFI()
{
    static unsigned long startAttemptTime = 0;
    static bool connectingNew = false;
    static bool tryingSaved = false;

    if (!connectingNew && !tryingSaved)
    {
        Serial.println("[WiFi] Connecting with BLE-provided credentials...");
        WiFi.disconnect(true);
        delay(100);
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(8,8,8,8)); // Đặt lại DNS
        WiFi.begin(ssid_new.c_str(), password_new.c_str());
        startAttemptTime = millis();
        connectingNew = true;
    }

    if (connectingNew)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("[WiFi] Connected with new credentials.");
            Serial.print("Local IP: "); Serial.println(WiFi.localIP());
            Serial.print("DNS IP: "); Serial.println(WiFi.dnsIP());

            saveWiFiCredentials(ssid_new, password_new);
            currentState = KET_NOI_WIFI_THANH_CONG;
            connectingNew = false;
        }
        else if (millis() - startAttemptTime > 15000)
        {
            Serial.println("[WiFi] New credentials failed. Trying saved...");
            String savedSSID, savedPassword;
            if (loadWiFiCredentials(savedSSID, savedPassword))
            {
                ssid_new = savedSSID;
                password_new = savedPassword;
                Serial.printf("[WiFi] Trying saved SSID=\"%s\", Password=\"%s\"\n",
                              ssid_new.c_str(), password_new.c_str());

                WiFi.disconnect(true);
                delay(100);
                WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, IPAddress(8,8,8,8)); 
                WiFi.begin(ssid_new.c_str(), password_new.c_str());
                startAttemptTime = millis();
                connectingNew = false;
                tryingSaved = true;
            }
            else
            {
                Serial.println("[WiFi] No saved credentials. Switching to BLE mode.");
                currentState = CHUA_CO_KET_NOI;
                connectingNew = false;
            }
        }
    }
    else if (tryingSaved)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("[WiFi] Connected with saved credentials.");
            Serial.print("Local IP: "); Serial.println(WiFi.localIP());
            Serial.print("DNS IP: "); Serial.println(WiFi.dnsIP());

            currentState = KET_NOI_WIFI_THANH_CONG;
            tryingSaved = false;
        }
        else if (millis() - startAttemptTime > 10000)
        {
            Serial.println("[WiFi] Failed with saved credentials. Switching to BLE mode.");
            currentState = CHUA_CO_KET_NOI;
            tryingSaved = false;
        }
    }
}


// --------------------------------
// Bật/Tắt Access Point
// --------------------------------
void enableAccessPoint()
{
    if (!apEnabled)
    {
        WiFi.softAP("ESP32-AP", "12345678");
        IPAddress apIP = WiFi.softAPIP();
        Serial.println("Access Point enabled. AP IP: " + apIP.toString());
        apEnabled = true;

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  {
            String htmlPage = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
            htmlPage += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; } ";
            htmlPage += "form { display: inline-block; background-color: #f9f9f9; padding: 20px; border: 1px solid #ccc; border-radius: 10px; }";
            htmlPage += "input[type='text'], input[type='password'] { width: 90%; padding: 10px; margin: 10px 0; border: 1px solid #ccc; border-radius: 5px; }";
            htmlPage += "input[type='submit'] { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; }";
            htmlPage += "input[type='submit']:hover { background-color: #45a049; }";
            htmlPage += "</style></head><body>";
            htmlPage += "<h2>Connect to WiFi</h2>";
            // Phần hiển thị tổng lượng nước tháng:
            htmlPage += "<p><b>Total water usage this month:</b> " + String(total_water_monthly) + " liters</p>";
            // Form kết nối WiFi:
            htmlPage += "<form action=\"/connect\" method=\"post\">";
            htmlPage += "SSID: <input type=\"text\" name=\"ssid\"><br>";
            htmlPage += "Password: <input type=\"password\" name=\"password\"><br>";
            htmlPage += "<input type=\"submit\" value=\"Connect\">";
            htmlPage += "</form>";
            htmlPage += "</body></html>";

            request->send(200, "text/html", htmlPage); });

        server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            String ssid     = "";
            String password = "";
            if (request->hasParam("ssid", true) && request->hasParam("password", true))
            {
                ssid     = request->getParam("ssid", true)->value();
                password = request->getParam("password", true)->value();
                 Serial.printf("Đã kết nối Wifi mới: SSID = %s, Password: %s\n", ssid.c_str(), password.c_str());

            }

            if (!ssid.isEmpty() && !password.isEmpty())
            {
                 Serial.printf("Đã kết nối Wifi mới: SSID = %s, Password: %s\n", ssid.c_str(), password.c_str());
                ssid_new     = ssid;
                password_new = password;
                disableAccessPoint();
                currentState = KET_NOI_WIFI;
            }
            else
            {
                request->send(200, "text/plain", "Invalid credentials format.");
            } });

        server.begin();
    }
}

void disableAccessPoint()
{
    if (apEnabled && WiFi.softAPgetStationNum() == 0)
    {
        WiFi.softAPdisconnect(true);
        Serial.println("Access Point disabled.");
        apEnabled = false;
    }
}

// --------------------------------
// LAN mode
// --------------------------------
void enableLAN()
{
    // Serial.println("LAN Mode");
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(200, "text/html",
                              "<html><body><h2>Connect to WiFi - LAN Mode</h2>"
                              "<form action=\"/connect\" method=\"post\">"
                              "SSID: <input type=\"text\" name=\"ssid\"><br>"
                              "Password: <input type=\"password\" name=\"password\"><br>"
                              "<input type=\"submit\" value=\"Connect\">"
                              "</form></body></html>"); });

    server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        String ssid     = "";
        String password = "";
        if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
            ssid     = request->getParam("ssid", true)->value();
            password = request->getParam("password", true)->value();
             Serial.printf("Đã kết nối Wifi mới: SSID = %s, Password: %s\n", ssid.c_str(), password.c_str());
        }
        if (!ssid.isEmpty() && !password.isEmpty()) {
            ssid_new     = ssid;
            password_new = password;
            currentState = KET_NOI_WIFI;
        } else {
            request->send(200, "text/plain", "Invalid credentials format.");
        } });

    server.begin();
}

// --------------------------------
// Lưu/Gửi Data qua SPIFFS
// --------------------------------
void saveDataToSPIFFS(const String &data)
{
    std::vector<String> lines;

    // Đọc nếu file tồn tại
    if (SPIFFS.exists("/data.log"))
    {
        File file = SPIFFS.open("/data.log", FILE_READ);
        if (file)
        {
            while (file.available())
            {
                String line = file.readStringUntil('\n');
                line.trim();
                if (!line.isEmpty())
                    lines.push_back(line);
            }
            file.close();
        }
    }

    // Giới hạn dòng
    if (lines.size() >= 20)
    {
        lines.erase(lines.begin()); // xóa dòng đầu tiên
    }

    // Thêm dòng mới
    lines.push_back(data);

    // Ghi lại toàn bộ file
    File file = SPIFFS.open("/data.log", FILE_WRITE);
    if (!file)
    {
        Serial.println("[SPIFFS] Failed to open /data.log for writing");
        return;
    }
    for (const auto &l : lines)
    {
        file.println(l);
    }
    file.close();
    Serial.println("[SPIFFS] Data saved to /data.log");
}

bool sendSavedDataToMQTT()
{
    if (!SPIFFS.exists("/data.log"))
    {
        Serial.println("[MQTT] No /data.log to send");
        return false;
    }

    File file = SPIFFS.open("/data.log", FILE_READ);
    if (!file)
    {
        Serial.println("[MQTT] Failed to open /data.log for reading");
        return false;
    }

    std::vector<String> lines;
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.isEmpty())
            lines.push_back(line);
    }
    file.close();

    if (lines.empty())
    {
        SPIFFS.remove("/data.log");
        Serial.println("[MQTT] /data.log empty -> deleted");
        return false;
    }

    String lineToSend = lines.front();
    lines.erase(lines.begin());

    if (client.connected())
    {
        String topic = "datawater/" + id_new;

        client.publish(topic.c_str(), lineToSend.c_str());
        // if (client.publish(topic.c_str(), lineToSend.c_str())) {
        //     Serial.println("[MQTT] Sent saved data: " + lineToSend);
        // } else {
        //     Serial.println("[MQTT] Failed to send saved data, will retry");
        //     return true;  // giữ lại file, thử lại sau
        // }
    }
    else
    {
        Serial.println("[MQTT] Client not connected, will retry");
        return false;
    }

    if (lines.empty())
    {
        SPIFFS.remove("/data.log");
        Serial.println("[MQTT] All saved data sent. File deleted.");
    }
    else
    {
        file = SPIFFS.open("/data.log", FILE_WRITE);
        for (const auto &l : lines)
        {
            file.println(l);
        }
        file.close();
    }

    return !lines.empty(); // true nếu còn dữ liệu để gửi tiếp
}
