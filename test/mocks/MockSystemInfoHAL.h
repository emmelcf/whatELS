#pragma once

#include "core/ISystemInfoHAL.h"

namespace whatels::core {

/**
 * @brief Test double for ISystemInfoHAL.
 * @details Simulates controller hardware vitals, uptime, heap, and firmware features.
 */
class MockSystemInfoHAL : public ISystemInfoHAL {
public:
  uint32_t uptimeSeconds = 120;
  uint32_t freeHeapBytes = 145408; // 142 KB
  String wifiStatus = "STA_CONNECTED";
  const char* version = "H5V38";
  const char* chipModel = "ESP32-S3";
  uint32_t chipRevision = 2;
  uint32_t flashSizeBytes = 16 * 1024 * 1024; // 16 MB
  const char* features = "spindleSim,ota";

  MockSystemInfoHAL() = default;
  virtual ~MockSystemInfoHAL() = default;

  uint32_t getUptimeSeconds() const override { return uptimeSeconds; }
  uint32_t getFreeHeapBytes() const override { return freeHeapBytes; }
  String getWifiStatus() const override { return wifiStatus; }

  const char* getVersion() const override { return version; }
  const char* getChipModel() const override { return chipModel; }
  uint32_t getChipRevision() const override { return chipRevision; }
  uint32_t getFlashSizeBytes() const override { return flashSizeBytes; }
  const char* getFeatures() const override { return features; }
};

} // namespace whatels::core

