#pragma once

/**
 * @file PlatformCompat.h
 * @brief Host/ESP32 portability shim for String and platform-guarded types.
 * @see docs/testing/README.md
 * @see docs/WHATELS_MASTER_ARCHITECTURE.md
 */

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_log.h>
#else
#include <cstddef>
#include <cstdint>
#include <string>
using String = std::string;
#ifndef ESP_LOGE
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
#endif
#endif
