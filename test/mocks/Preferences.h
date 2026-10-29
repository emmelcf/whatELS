#pragma once
#include <string>
#include <cstdint>

class Preferences {
public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool clear() { return true; }
  int32_t getInt(const char*, int32_t def = 0) { return def; }
  size_t putInt(const char*, int32_t) { return 4; }
  bool getBool(const char*, bool def = false) { return def; }
  size_t putBool(const char*, bool) { return 1; }
  std::string getString(const char*, const char* def = "") { return def; }
  size_t putString(const char*, const char*) { return 1; }
  bool isKey(const char*) { return false; }
};

