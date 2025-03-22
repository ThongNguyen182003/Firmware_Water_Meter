#ifndef BLE_UTILS_H
#define BLE_UTILS_H

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Hàm khởi tạo BLE, dừng BLE, xử lý state CHUA_CO_KET_NOI
void initBLE();
void stopBLE();
void handleStateCHUA_CO_KET_NOI();

#endif
