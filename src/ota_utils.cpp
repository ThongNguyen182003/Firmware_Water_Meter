#include "ota_utils.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "mqtt_utils.h"
/**
 * @brief Thực thi OTA HTTP: tải firmware từ otaUrl và flash vào ESP.
 * @param otaUrl Đường dẫn HTTP đến firmware.bin
 */
void performHttpOtaUpdate(const char* otaUrl) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OTA: WiFi chưa kết nối");
        return;
    }

    Serial.printf("OTA: Tải từ %s\n", otaUrl);
    HTTPClient http;
    http.begin(otaUrl);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("OTA: HTTP GET lỗi, code=%d\n", httpCode);
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("OTA: Content-Length không hợp lệ");
        http.end();
        return;
    }

    // Bắt đầu update với đúng kích thước
    if (!Update.begin(contentLength)) {
        Serial.println("OTA: Không đủ vùng nhớ");
        http.end();
        return;
    }

    // Ghi từng byte từ stream
    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    Serial.printf("OTA: Đã ghi %u/%u bytes\n", written, (unsigned)contentLength);

    // Hoàn tất và kiểm tra
    if (Update.end() && Update.isFinished()) {
        Serial.println("OTA: Thành công, khởi động lại...");
        ESP.restart();
    } else {
        Serial.printf("OTA: Lỗi #%u: %s\n",
                      Update.getError(),
                      Update.errorString());
    }

    http.end();
}