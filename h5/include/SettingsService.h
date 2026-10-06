#pragma once

#include <Arduino.h>
#include <optional>

class SettingsService {
public:
  // Initializes SettingsService on boot and loads optional overrides into RAM
  static void init();

  // Handles SETTINGS.get or SETTINGS.get=<key>
  // Returns true if handled, populating reply with SETTING.<key>=<val>\n
  static bool get(const char* key, String* reply);

  // Handles SETTINGS.set=<key>,<val> or SETTINGS.set=<key>=<val>
  // Returns true if handled, populating reply with OK:SETTINGS.<key>=<val>\n or ERR:SETTINGS.<key>=<err>\n
  static bool set(const char* keyVal, String* reply);

  // Telemetry deadband getters (returns user override or derived default)
  static float getDeadbandAngle();
  static bool isDeadbandAngleAuto();
  static float getDeadbandTurns();
  static bool isDeadbandTurnsAuto();

  // Centralized typed mutation methods (updates RAM and persists to flash)
  static bool setDiameterMode(bool dia);
  static bool setAbsoluteMode(bool abs);
  static bool setZDroMode(bool zdro);
  static bool setXDroMode(bool xdro);
  static bool setMeasure(int m);
  static bool setJogStepDu(long du);
  static bool setDeadbandAngle(const char* val);
  static bool setDeadbandTurns(const char* val);
};
