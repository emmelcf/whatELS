#pragma once
#include <string>
#include <cstdint>

class File {
public:
  operator bool() const { return false; }
  size_t write(const uint8_t*, size_t len) { return len; }
  void close() {}
  std::string readString() { return ""; }
};

class LittleFSFS {
public:
  bool begin(bool = false) { return true; }
  File open(const char*, const char*) { return File(); }
  bool exists(const char*) { return false; }
};

inline LittleFSFS LittleFS;

