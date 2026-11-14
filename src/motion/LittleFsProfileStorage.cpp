#include "motion/LittleFsProfileStorage.h"

#if defined(ARDUINO) || defined(ESP32)
#include <LittleFS.h>
#include <cstdint>

namespace whatels::motion {

bool LittleFsProfileStorage::exists(const char* path) const {
  return path && LittleFS.exists(path);
}

bool LittleFsProfileStorage::readFile(const char* path, std::string& outContent) const {
  if (!path || !LittleFS.exists(path)) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  size_t sz = file.size();
  outContent.resize(sz);
  size_t bytesRead = (sz > 0) ? file.readBytes(&outContent[0], sz) : 0;
  file.close();
  outContent.resize(bytesRead);
  return true;
}

bool LittleFsProfileStorage::writeFile(const char* path, const std::string& content) {
  if (!path) return false;
  File file = LittleFS.open(path, "w");
  if (!file) return false;
  size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  file.close();
  return written == content.size();
}

bool LittleFsProfileStorage::removeFile(const char* path) {
  if (!path) return false;
  return LittleFS.remove(path);
}

} // namespace whatels::motion

#else

namespace whatels::motion {

bool LittleFsProfileStorage::exists(const char* /*path*/) const { return false; }
bool LittleFsProfileStorage::readFile(const char* /*path*/, std::string& /*outContent*/) const { return false; }
bool LittleFsProfileStorage::writeFile(const char* /*path*/, const std::string& /*content*/) { return false; }
bool LittleFsProfileStorage::removeFile(const char* /*path*/) { return false; }

} // namespace whatels::motion

#endif
