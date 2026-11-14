#pragma once

/**
 * @file Esp32SystemInfoHAL.h
 * @brief Production ESP32-S3 implementation of ISystemInfoHAL.
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 * @see docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 */

#include "ISystemInfoHAL.h"

namespace whatels::core {

/**
 * @brief Production ESP32-S3 implementation of ISystemInfoHAL.
 * @details Reads live hardware registers via ESP-IDF / Arduino APIs:
 *          millis(), ESP.getFreeHeap(), ESP.getChipModel(), ESP.getFlashChipSize(),
 *          and WifiService.
 */
class Esp32SystemInfoHAL : public ISystemInfoHAL {
public:
  Esp32SystemInfoHAL() = default;
  virtual ~Esp32SystemInfoHAL() = default;

  uint32_t getUptimeSeconds() const override;
  uint32_t getFreeHeapBytes() const override;
  String getWifiStatus() const override;

  const char* getVersion() const override;
  const char* getChipModel() const override;
  uint32_t getChipRevision() const override;
  uint32_t getFlashSizeBytes() const override;
  const char* getFeatures() const override;
};

} // namespace whatels::core

