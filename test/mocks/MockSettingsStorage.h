#pragma once

#include "core/ISettingsStorage.h"
#include <map>
#include <string>
#include <cstring>
#include <cstdlib>

namespace whatels::core {

/**
 * @file MockSettingsStorage.h
 * @brief In-memory test double for ISettingsStorage.
 * @details Stores key-values in RAM per namespace, enabling deterministic,
 *          isolated unit testing of settings and migrations on host (g++) without disk I/O.
 */
class MockSettingsStorage : public ISettingsStorage {
public:
  std::map<std::string, std::map<std::string, std::string>> namespaces;
  std::string activeNs = "prefs";
  bool readOnly = false;
  bool opened = false;

  bool begin(const char* name, bool ro = false) override {
    activeNs = name ? name : "prefs";
    readOnly = ro;
    opened = true;
    return true;
  }

  void end() override {
    opened = false;
  }

  bool clear() override {
    if (!opened || readOnly) return false;
    namespaces[activeNs].clear();
    return true;
  }

  bool remove(const char* key) override {
    if (!opened || readOnly || !key) return false;
    return namespaces[activeNs].erase(key) > 0;
  }

  bool isKey(const char* key) override {
    if (!opened || !key) return false;
    return namespaces[activeNs].find(key) != namespaces[activeNs].end();
  }

  // --- Getters ---
  bool getBool(const char* key, bool& outVal) override {
    if (!isKey(key)) return false;
    outVal = (namespaces[activeNs][key] == "1" || namespaces[activeNs][key] == "true");
    return true;
  }

  bool getU8(const char* key, uint8_t& outVal) override {
    if (!isKey(key)) return false;
    outVal = static_cast<uint8_t>(std::stoul(namespaces[activeNs][key]));
    return true;
  }

  bool getU16(const char* key, uint16_t& outVal) override {
    if (!isKey(key)) return false;
    outVal = static_cast<uint16_t>(std::stoul(namespaces[activeNs][key]));
    return true;
  }

  bool getI32(const char* key, int32_t& outVal) override {
    if (!isKey(key)) return false;
    outVal = static_cast<int32_t>(std::stol(namespaces[activeNs][key]));
    return true;
  }

  bool getFloat(const char* key, float& outVal) override {
    if (!isKey(key)) return false;
    outVal = std::stof(namespaces[activeNs][key]);
    return true;
  }

  bool getString(const char* key, char* outBuf, size_t maxLen) override {
    if (!isKey(key) || !outBuf || maxLen == 0) return false;
    const std::string& val = namespaces[activeNs][key];
    std::strncpy(outBuf, val.c_str(), maxLen - 1);
    outBuf[maxLen - 1] = '\0';
    return true;
  }

  // --- Putters ---
  bool putBool(const char* key, bool val) override {
    if (!opened || readOnly || !key) return false;
    namespaces[activeNs][key] = val ? "1" : "0";
    return true;
  }

  bool putU8(const char* key, uint8_t val) override {
    if (!opened || readOnly || !key) return false;
    namespaces[activeNs][key] = std::to_string(val);
    return true;
  }

  bool putU16(const char* key, uint16_t val) override {
    if (!opened || readOnly || !key) return false;
    namespaces[activeNs][key] = std::to_string(val);
    return true;
  }

  bool putI32(const char* key, int32_t val) override {
    if (!opened || readOnly || !key) return false;
    namespaces[activeNs][key] = std::to_string(val);
    return true;
  }

  bool putFloat(const char* key, float val) override {
    if (!opened || readOnly || !key) return false;
    namespaces[activeNs][key] = std::to_string(val);
    return true;
  }

  bool putString(const char* key, const char* val) override {
    if (!opened || readOnly || !key || !val) return false;
    namespaces[activeNs][key] = std::string(val);
    return true;
  }

  // --- Test Pre-Seeding Convenience Helpers ---
  void seedI32(const char* ns, const char* key, int32_t val) {
    namespaces[ns ? ns : "prefs"][key] = std::to_string(val);
  }

  void seedBool(const char* ns, const char* key, bool val) {
    namespaces[ns ? ns : "prefs"][key] = val ? "1" : "0";
  }

  void seedFloat(const char* ns, const char* key, float val) {
    namespaces[ns ? ns : "prefs"][key] = std::to_string(val);
  }

  void seedString(const char* ns, const char* key, const char* val) {
    namespaces[ns ? ns : "prefs"][key] = std::string(val ? val : "");
  }
};

} // namespace whatels::core

