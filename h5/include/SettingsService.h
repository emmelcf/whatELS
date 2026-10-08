#pragma once

#include <Arduino.h>
#include <optional>
#include "LatheProfile.h"

/**
 * @file SettingsService.h
 * @brief Authoritative configuration manager and parameter broker for NanoELS H5.
 * @details Implements the 4-tier properties taxonomy, active lathe machine profile,
 *          sparse NVS overrides, and universal reset architecture.
 */
class SettingsService {
public:
  // Boot Initialization: Loads active preset into RAM, scans NVS for sparse overrides,
  // and synchronizes physical parameters to legacy globals for Core 1 compatibility.
  static void init();

  // --- Tier 1: Active Lathe Machine Profile ---
  static const LatheConfiguration& getActiveProfile();
  static bool hasCapability(LatheCapabilityFlags flag);
  static const char* getActivePresetSlug();
  static bool setPreset(const char* slug, String* reply = nullptr);

  // --- Universal Wire Protocol Handlers ---
  // Handles SETTINGS.get or SETTINGS.get=<key>
  static bool get(const char* key, String* reply);

  // Handles SETTINGS.set=<key>,<val> or SETTINGS.set=<key>=<val>
  static bool set(const char* keyVal, String* reply);

  // Universal Reset Engine: SETTINGS.reset=<key> (reverts key to preset default)
  static bool reset(const char* key, String* reply = nullptr);
  static bool resetGroup(const char* group, String* reply = nullptr);

  // --- Tier 3: Telemetry Deadband Getters ---
  static float getDeadbandAngle();
  static bool isDeadbandAngleAuto();
  static float getDeadbandTurns();
  static bool isDeadbandTurnsAuto();

  // --- Typed Mutators (Updates RAM & writes sparse override to NVS) ---
  static bool setDiameterMode(bool dia);
  static bool setAbsoluteMode(bool abs);
  static bool setZDroMode(bool zdro);
  static bool setXDroMode(bool xdro);
  static bool setMeasure(int m);
  static bool setJogStepDu(long du);
  static bool setDeadbandAngle(const char* val);
  static bool setDeadbandTurns(const char* val);

  // Tier 1 direct mutators
  static bool setScrewZDu(long du);
  static bool setScrewXDu(long du);
  static bool setMotorStepsZ(long steps);
  static bool setMotorStepsX(long steps);
  static bool setEncoderPpr(int ppr);

  // Synchronizes physical parameters into legacy globals for Core 1 compatibility
  static void syncLegacyGlobals();
};
