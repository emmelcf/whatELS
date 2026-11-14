#pragma once

/**
 * @file IProfileStorage.h
 * @brief Abstract file storage interface for Lathe Machine Profile catalogs.
 * @details Decouples LatheProfile from ESP32 LittleFS.h so host unit tests can run
 *          against a pure C++ in-memory test double with zero Arduino.h/LittleFS.h mocks.
 * @see docs/machine/LATHE_MACHINE_PROFILE.md
 * @see docs/testing/README.md
 */

#include <string>

namespace whatels::motion {

class IProfileStorage {
public:
  virtual ~IProfileStorage() = default;

  virtual bool exists(const char* path) const = 0;
  virtual bool readFile(const char* path, std::string& outContent) const = 0;
  virtual bool writeFile(const char* path, const std::string& content) = 0;
  virtual bool removeFile(const char* path) = 0;
};

} // namespace whatels::motion
