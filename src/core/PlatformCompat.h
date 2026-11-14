#pragma once

/**
 * @file PlatformCompat.h
 * @brief Host/ESP32 portability shim for String and platform-guarded types.
 * @see docs/testing/README.md
 * @see docs/WHATELS_MASTER_ARCHITECTURE.md
 */

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#else
#include <cstddef>
#include <cstdint>
#include <string>
using String = std::string;
#endif
