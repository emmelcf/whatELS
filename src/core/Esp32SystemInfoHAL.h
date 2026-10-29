#pragma once

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

