#pragma once

/**
 * @file MockProfileStorage.h
 * @brief Pure C++ in-memory test double for whatels::motion::IProfileStorage.
 * @details Stores profile catalog files in RAM, enabling deterministic host unit
 *          testing of LatheProfile without LittleFS or disk I/O.
 */

#include "motion/IProfileStorage.h"
#include <map>
#include <string>

namespace whatels::motion {

class MockProfileStorage : public IProfileStorage {
public:
  std::map<std::string, std::string> files;
  bool readOnly = false;

  bool exists(const char* path) const override {
    if (!path) return false;
    return files.find(path) != files.end();
  }

  bool readFile(const char* path, std::string& outContent) const override {
    if (!path) return false;
    auto it = files.find(path);
    if (it == files.end()) return false;
    outContent = it->second;
    return true;
  }

  bool writeFile(const char* path, const std::string& content) override {
    if (readOnly || !path) return false;
    files[path] = content;
    return true;
  }

  bool removeFile(const char* path) override {
    if (readOnly || !path) return false;
    return files.erase(path) > 0;
  }

  void clear() { files.clear(); }
};

} // namespace whatels::motion
