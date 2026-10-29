#include "Esp32SystemInfoHAL.h"
#include "Config.h"

#if defined(ARDUINO) || defined(ESP32)
#include <Arduino.h>
#include "WifiService.h"

namespace whatels::core {

uint32_t Esp32SystemInfoHAL::getUptimeSeconds() const {
  return millis() / 1000;
}

uint32_t Esp32SystemInfoHAL::getFreeHeapBytes() const {
  return ESP.getFreeHeap();
}

String Esp32SystemInfoHAL::getWifiStatus() const {
  return WifiService::instance().getModeString();
}

const char* Esp32SystemInfoHAL::getVersion() const {
  return CONTROLLER_VERSION_TEXT;
}

const char* Esp32SystemInfoHAL::getChipModel() const {
  return ESP.getChipModel();
}

uint32_t Esp32SystemInfoHAL::getChipRevision() const {
  return ESP.getChipRevision();
}

uint32_t Esp32SystemInfoHAL::getFlashSizeBytes() const {
  return ESP.getFlashChipSize();
}

const char* Esp32SystemInfoHAL::getFeatures() const {
#if defined(ENABLE_SPINDLE_SIM) && ENABLE_SPINDLE_SIM
  return "spindleSim,ota";
#else
  return "ota";
#endif
}

} // namespace whatels::core

#else

namespace whatels::core {

uint32_t Esp32SystemInfoHAL::getUptimeSeconds() const { return 0; }
uint32_t Esp32SystemInfoHAL::getFreeHeapBytes() const { return 0; }
String Esp32SystemInfoHAL::getWifiStatus() const { return "OFF"; }
const char* Esp32SystemInfoHAL::getVersion() const { return CONTROLLER_VERSION_TEXT; }
const char* Esp32SystemInfoHAL::getChipModel() const { return "ESP32-S3"; }
uint32_t Esp32SystemInfoHAL::getChipRevision() const { return 0; }
uint32_t Esp32SystemInfoHAL::getFlashSizeBytes() const { return 0; }
const char* Esp32SystemInfoHAL::getFeatures() const { return "ota"; }

} // namespace whatels::core

#endif

