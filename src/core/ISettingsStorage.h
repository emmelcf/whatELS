#pragma once

#include <cstddef>
#include <cstdint>

namespace whatels::core {

/**
 * @file ISettingsStorage.h
 * @brief Abstract Non-Volatile Storage (NVS) boundary interface.
 * @details Conforms to ADR-0005 and AGENTS.md Tier 1 invariant: Zero fake framework mocks.
 *          Decouples persistent configuration storage from ESP32-specific Preferences.h
 *          and flash memory SDKs, enabling native host testing without vendor headers.
 */
class ISettingsStorage {
public:
  virtual ~ISettingsStorage() = default;

  // --- Namespace Partition Lifecycle ---
  virtual bool begin(const char* name, bool readOnly = false) = 0;
  virtual void end() = 0;
  virtual bool clear() = 0;
  virtual bool remove(const char* key) = 0;
  virtual bool isKey(const char* key) = 0;

  // --- Typed Primitive Getters ---
  // Returns true if key exists and populates outVal; returns false if key not found.
  virtual bool getBool(const char* key, bool& outVal) = 0;
  virtual bool getU8(const char* key, uint8_t& outVal) = 0;
  virtual bool getU16(const char* key, uint16_t& outVal) = 0;
  virtual bool getI32(const char* key, int32_t& outVal) = 0;
  virtual bool getFloat(const char* key, float& outVal) = 0;
  virtual bool getString(const char* key, char* outBuf, size_t maxLen) = 0;

  // --- Typed Primitive Putters ---
  // Returns true on successful write; returns false on error.
  virtual bool putBool(const char* key, bool val) = 0;
  virtual bool putU8(const char* key, uint8_t val) = 0;
  virtual bool putU16(const char* key, uint16_t val) = 0;
  virtual bool putI32(const char* key, int32_t val) = 0;
  virtual bool putFloat(const char* key, float val) = 0;
  virtual bool putString(const char* key, const char* val) = 0;
};

} // namespace whatels::core

