#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <esp_log.h>

namespace detail {
inline bool readPref(Preferences& p, const char* key, bool def) { return p.getBool(key, def); }
inline int readPref(Preferences& p, const char* key, int def) { return p.getInt(key, def); }
inline long readPref(Preferences& p, const char* key, long def) { return p.getLong(key, def); }
inline float readPref(Preferences& p, const char* key, float def) { return p.getFloat(key, def); }
inline uint8_t readPref(Preferences& p, const char* key, uint8_t def) { return p.getUChar(key, def); }
inline String readPref(Preferences& p, const char* key, const String& def) { return p.getString(key, def); }

inline size_t writePref(Preferences& p, const char* key, bool val) { return p.putBool(key, val); }
inline size_t writePref(Preferences& p, const char* key, int val) { return p.putInt(key, val); }
inline size_t writePref(Preferences& p, const char* key, long val) { return p.putLong(key, val); }
inline size_t writePref(Preferences& p, const char* key, float val) { return p.putFloat(key, val); }
inline size_t writePref(Preferences& p, const char* key, uint8_t val) { return p.putUChar(key, val); }
inline size_t writePref(Preferences& p, const char* key, const String& val) { return p.putString(key, val); }

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
class PersistentSetting {
 public:
  using CallbackFn = void (*)(const T& newValue);

  PersistentSetting()
      : _ns(""),
        _key(""),
        _defaultValue(T{}),
        _value(T{}),
        _onChange(nullptr),
        _loaded(false) {}

  PersistentSetting(const char* nvsNamespace, const char* nvsKey, T defaultValue, CallbackFn onChange = nullptr)
      : _ns(nvsNamespace),
        _key(nvsKey),
        _defaultValue(defaultValue),
        _value(defaultValue),
        _onChange(onChange),
        _loaded(false) {}

  void setup(const char* nvsNamespace, const char* nvsKey, T defaultValue, CallbackFn onChange = nullptr) {
    _ns = nvsNamespace;
    _key = nvsKey;
    _defaultValue = defaultValue;
    _value = defaultValue;
    _onChange = onChange;
    _loaded = false;
  }

  void init() {
    load();
  }

  void load() {
    Preferences pref;
    if (pref.begin(_ns, true)) { // Read-only mode
      _value = detail::readPref(pref, _key, _defaultValue);
      pref.end();
    } else {
      _value = _defaultValue;
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

    Preferences pref;
    if (pref.begin(_ns, false)) { // Read-write mode
      detail::writePref(pref, _key, _value);
      pref.end();
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
  const char* _ns;
  const char* _key;
  T _defaultValue;
  T _value;
  CallbackFn _onChange;
  bool _loaded;
};
