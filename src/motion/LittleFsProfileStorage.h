#pragma once

/**
 * @file LittleFsProfileStorage.h
 * @brief Production ESP32 LittleFS implementation of IProfileStorage.
 * @see docs/machine/LATHE_MACHINE_PROFILE.md
 * @see docs/testing/README.md
 */

#include "motion/IProfileStorage.h"

namespace whatels::motion {

class LittleFsProfileStorage : public IProfileStorage {
public:
  bool exists(const char* path) const override;
  bool readFile(const char* path, std::string& outContent) const override;
  bool writeFile(const char* path, const std::string& content) override;
  bool removeFile(const char* path) override;
};

} // namespace whatels::motion
