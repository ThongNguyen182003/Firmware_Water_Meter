#ifndef OTA_UTILS_H
#define OTA_UTILS_H

/**
 * @brief Tải và cài đặt OTA từ URL cho trước.
 * @param otaUrl http://10.28.128.17:3000/firmware.bin
 */
void performHttpOtaUpdate(const char* otaUrl);

#endif
