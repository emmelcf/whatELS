#pragma once

#include <Arduino.h>

class SettingsService {
public:
  // Handles SETTINGS.get or SETTINGS.get=<key>
  // Returns true if handled, populating reply with SETTING.<key>=<val>\n
  static bool get(const char* key, String* reply);

  // Handles SETTINGS.set=<key>,<val> or SETTINGS.set=<key>=<val>
  // Returns true if handled, populating reply with OK:SETTINGS.<key>=<val>\n or ERR:SETTINGS.<key>=<err>\n
  static bool set(const char* keyVal, String* reply);
};
