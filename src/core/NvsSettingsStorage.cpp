#include "NvsSettingsStorage.h"

#if defined(ARDUINO) || defined(ESP32)

namespace whatels::core {

bool NvsSettingsStorage::begin(const char* name, bool readOnly) {
  if (!name) return false;
  return _prefs.begin(name, readOnly);
}

void NvsSettingsStorage::end() {
  _prefs.end();
}

bool NvsSettingsStorage::clear() {
  return _prefs.clear();
}

bool NvsSettingsStorage::remove(const char* key) {
  if (!key) return false;
  return _prefs.remove(key);
}

bool NvsSettingsStorage::isKey(const char* key) {
  if (!key) return false;
  return _prefs.isKey(key);
}

bool NvsSettingsStorage::getBool(const char* key, bool& outVal) {
  if (!key || !_prefs.isKey(key)) return false;
  outVal = _prefs.getBool(key, false);
  return true;
}

bool NvsSettingsStorage::getU8(const char* key, uint8_t& outVal) {
  if (!key || !_prefs.isKey(key)) return false;
  outVal = _prefs.getUChar(key, 0);
  return true;
}

bool NvsSettingsStorage::getU16(const char* key, uint16_t& outVal) {
  if (!key || !_prefs.isKey(key)) return false;
  outVal = _prefs.getUShort(key, 0);
  return true;
}

bool NvsSettingsStorage::getI32(const char* key, int32_t& outVal) {
  if (!key || !_prefs.isKey(key)) return false;
  outVal = _prefs.getInt(key, 0);
  return true;
}

bool NvsSettingsStorage::getFloat(const char* key, float& outVal) {
  if (!key || !_prefs.isKey(key)) return false;
  outVal = _prefs.getFloat(key, 0.0f);
  return true;
}

bool NvsSettingsStorage::getString(const char* key, char* outBuf, size_t maxLen) {
  if (!key || !outBuf || maxLen == 0 || !_prefs.isKey(key)) return false;
  outBuf[0] = '\0';
  _prefs.getString(key, outBuf, maxLen);
  outBuf[maxLen - 1] = '\0';
  return true;
}

bool NvsSettingsStorage::putBool(const char* key, bool val) {
  if (!key) return false;
  return _prefs.putBool(key, val) > 0;
}

bool NvsSettingsStorage::putU8(const char* key, uint8_t val) {
  if (!key) return false;
  return _prefs.putUChar(key, val) > 0;
}

bool NvsSettingsStorage::putU16(const char* key, uint16_t val) {
  if (!key) return false;
  return _prefs.putUShort(key, val) > 0;
}

bool NvsSettingsStorage::putI32(const char* key, int32_t val) {
  if (!key) return false;
  return _prefs.putInt(key, val) > 0;
}

bool NvsSettingsStorage::putFloat(const char* key, float val) {
  if (!key) return false;
  return _prefs.putFloat(key, val) > 0;
}

bool NvsSettingsStorage::putString(const char* key, const char* val) {
  if (!key || !val) return false;
  size_t written = _prefs.putString(key, val);
  return written > 0 || (val[0] == '\0' && _prefs.isKey(key));
}

} // namespace whatels::core
#endif // ARDUINO || ESP32
