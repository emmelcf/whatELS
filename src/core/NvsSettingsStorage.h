#pragma once

/**
 * @file NvsSettingsStorage.h
 * @brief Production ESP32 NVS (Preferences.h) adapter implementing ISettingsStorage.
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 * @see docs/state/ELS_PROPERTIES_AND_SETTINGS.md
 * @see docs/testing/README.md
 */

#include "ISettingsStorage.h"

#if defined(ARDUINO) || defined(ESP32)
#include <Preferences.h>

namespace whatels::core {

/**
 * @brief Concrete ESP32 NVS storage implementation backed by Arduino Preferences.
 */
class NvsSettingsStorage : public ISettingsStorage {
public:
  NvsSettingsStorage() = default;
  ~NvsSettingsStorage() override = default;

  bool begin(const char* name, bool readOnly = false) override;
  void end() override;
  bool clear() override;
  bool remove(const char* key) override;
  bool isKey(const char* key) override;

  bool getBool(const char* key, bool& outVal) override;
  bool getU8(const char* key, uint8_t& outVal) override;
  bool getU16(const char* key, uint16_t& outVal) override;
  bool getI32(const char* key, int32_t& outVal) override;
  bool getFloat(const char* key, float& outVal) override;
  bool getString(const char* key, char* outBuf, size_t maxLen) override;

  bool putBool(const char* key, bool val) override;
  bool putU8(const char* key, uint8_t val) override;
  bool putU16(const char* key, uint16_t val) override;
  bool putI32(const char* key, int32_t val) override;
  bool putFloat(const char* key, float val) override;
  bool putString(const char* key, const char* val) override;

private:
  Preferences _prefs;
};

} // namespace whatels::core
#endif // ARDUINO || ESP32
