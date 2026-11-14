#pragma once

/**
 * @file PersistentSetting.h
 * @brief Template wrapper for NVS-backed persistent settings with dirty tracking.
 * @see docs/state/ELS_PROPERTIES_AND_SETTINGS.md
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 */

#include <cstdint>
#include <cmath>
#include "core/PlatformCompat.h"
#include "ISettingsStorage.h"

#if defined(ARDUINO) || defined(ESP32)
#include <esp_log.h>
#include "NvsSettingsStorage.h"
#else
#ifndef ESP_LOGD
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#endif
#endif

/**
 * @brief Non-template base holding the shared default ISettingsStorage pointer.
 */
class PersistentSettingBase {
 public:
  static void setDefaultStorage(whatels::core::ISettingsStorage* storage) {
    defaultStorageRef() = storage;
  }

  static whatels::core::ISettingsStorage* getDefaultStorage() {
#if defined(ARDUINO) || defined(ESP32)
    if (!defaultStorageRef()) {
      static whatels::core::NvsSettingsStorage s_nvsStorage;
      defaultStorageRef() = &s_nvsStorage;
    }
#endif
    return defaultStorageRef();
  }

 private:
  static whatels::core::ISettingsStorage*& defaultStorageRef() {
    static whatels::core::ISettingsStorage* s_defaultStorage = nullptr;
    return s_defaultStorage;
  }
};

namespace detail {
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, bool& out) {
  return s.getBool(key, out);
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, uint8_t& out) {
  return s.getU8(key, out);
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, uint16_t& out) {
  return s.getU16(key, out);
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, int& out) {
  int32_t tmp = 0;
  if (!s.getI32(key, tmp)) return false;
  out = static_cast<int>(tmp);
  return true;
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, long& out) {
  int32_t tmp = 0;
  if (!s.getI32(key, tmp)) return false;
  out = static_cast<long>(tmp);
  return true;
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, float& out) {
  return s.getFloat(key, out);
}
inline bool readStorage(whatels::core::ISettingsStorage& s, const char* key, String& out) {
  char buf[256];
  if (!s.getString(key, buf, sizeof(buf))) return false;
  out = String(buf);
  return true;
}

inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, bool val) {
  return s.putBool(key, val);
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, uint8_t val) {
  return s.putU8(key, val);
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, uint16_t val) {
  return s.putU16(key, val);
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, int val) {
  return s.putI32(key, static_cast<int32_t>(val));
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, long val) {
  return s.putI32(key, static_cast<int32_t>(val));
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, float val) {
  return s.putFloat(key, val);
}
inline bool writeStorage(whatels::core::ISettingsStorage& s, const char* key, const String& val) {
  return s.putString(key, val.c_str());
}

template <typename T>
inline bool equals(const T& a, const T& b) { return a == b; }

template <>
inline bool equals<float>(const float& a, const float& b) { return fabsf(a - b) < 0.00001f; }
} // namespace detail

/**
 * @brief Smart Observable Property for persistent NVS configuration.
 *
 * Guarantees:
 * 1. Single read from NVS upon boot/init; instant in-memory RAM reads thereafter.
 * 2. Deterministic dirty-checking on assignment: redundant writes are suppressed with zero flash wear.
 * 3. Automatic type-safe write-through persistence to NVS on value changes.
 * 4. Lightweight optional onChanged callback hook for telemetry / reactivity.
 */
template <typename T>
class PersistentSetting : public PersistentSettingBase {
 public:
  using CallbackFn = void (*)(const T& newValue);

  PersistentSetting()
      : _ns(""),
        _key(""),
        _defaultValue(T{}),
        _value(T{}),
        _onChange(nullptr),
        _storage(nullptr),
        _loaded(false) {}

  PersistentSetting(const char* nvsNamespace, const char* nvsKey, T defaultValue, CallbackFn onChange = nullptr)
      : _ns(nvsNamespace),
        _key(nvsKey),
        _defaultValue(defaultValue),
        _value(defaultValue),
        _onChange(onChange),
        _storage(nullptr),
        _loaded(false) {}

  void setup(const char* nvsNamespace, const char* nvsKey, T defaultValue, CallbackFn onChange = nullptr) {
    _ns = nvsNamespace;
    _key = nvsKey;
    _defaultValue = defaultValue;
    _value = defaultValue;
    _onChange = onChange;
    _loaded = false;
  }

  void setStorage(whatels::core::ISettingsStorage* storage) {
    _storage = storage;
  }

  void init() {
    load();
  }

  void load() {
    whatels::core::ISettingsStorage* storage = resolveStorage();
    _value = _defaultValue;
    if (storage && storage->begin(_ns, true)) {
      T stored = _defaultValue;
      if (detail::readStorage(*storage, _key, stored)) {
        _value = stored;
      }
      storage->end();
    }
    _loaded = true;
  }

  // Transparent read from in-memory RAM cache
  operator T() const { return _value; }
  const T& get() const { return _value; }

  // Assignment / mutation with dirty check
  bool set(const T& newVal) {
    if (detail::equals(_value, newVal)) {
      ESP_LOGD("NVS", "Suppressed redundant write for key '%s'", _key);
      return false;
    }

    _value = newVal;

    whatels::core::ISettingsStorage* storage = resolveStorage();
    if (storage && storage->begin(_ns, false)) {
      detail::writeStorage(*storage, _key, _value);
      storage->end();
      ESP_LOGD("NVS", "Persisted '%s'", _key);
    }

    if (_onChange) {
      _onChange(_value);
    }
    return true;
  }

  PersistentSetting& operator=(const T& newVal) {
    set(newVal);
    return *this;
  }

  const char* key() const { return _key; }
  const char* ns() const { return _ns; }

 private:
  whatels::core::ISettingsStorage* resolveStorage() const {
    return _storage ? _storage : getDefaultStorage();
  }

  const char* _ns;
  const char* _key;
  T _defaultValue;
  T _value;
  CallbackFn _onChange;
  whatels::core::ISettingsStorage* _storage;
  bool _loaded;
};
