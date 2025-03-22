#include "ota_utils.h"
#include <Update.h>  

/*
* OTA Update Function
*/
void initOTA(AsyncWebServer &server) {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", 
            "<html>"
            "<body>"
            "<h2>OTA Update</h2>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "  <input type='file' name='firmware' accept='.bin' required>"
            "  <br><br>"
            "  <input type='submit' value='Update Firmware'>"
            "</form>"
            "</body>"
            "</html>"
        );
    });

    // Route POST to handle uploaded file 
    server.on(
        "/update", 
        HTTP_POST,
        // Update Completed 
        [](AsyncWebServerRequest *request) {
            if (Update.hasError()) {
                request->send(200, "text/plain", "Firmware Update Failed!");
            } else {
                request->redirect("/"); // Về trang chủ (hoặc hiển thị thành công)
            }
        },
        // Xử lý dữ liệu được upload
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("OTA Update Start: %s\n", filename.c_str());
                // Bắt đầu quá trình Update
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            // Ghi dữ liệu firmware
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("OTA Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );
}
