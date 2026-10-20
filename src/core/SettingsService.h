#pragma once

#include <Arduino.h>
#include <optional>
#include "LatheProfile.h"
#include "OperatorIntent.h"

#ifndef HAVE_AXIS_ID
#define HAVE_AXIS_ID
enum AxisId : uint8_t {
  AXIS_Z = 0,
  AXIS_X = 1
};
#endif

enum LimitDirection : uint8_t {
  LIMIT_LEFT_OR_UP    = 0, // Z left (chuck) or X infeed (up)
  LIMIT_RIGHT_OR_DOWN = 1  // Z right (tailstock) or X retract (down)
};

/**
 * @file SettingsService.h
 * @brief Authoritative configuration manager and parameter broker for whatELS.
 * @details Implements the 4-tier properties taxonomy, active lathe machine profile,
 *          sparse NVS overrides, and universal reset architecture.
 */
class SettingsService {
public:
  // Boot Initialization: Loads active preset into RAM, scans NVS for sparse overrides,
  // and synchronizes physical parameters to legacy globals for Core 1 compatibility.
  static void init();

  // --- Tier 1: Active Lathe Machine Profile & Presets ---
  static const LatheConfiguration& getActiveProfile();
  static bool hasCapability(LatheCapabilityFlags flag);
  static const char* getActivePresetSlug();
  static bool setPreset(const char* slug, String* reply = nullptr);
  static bool listPresets(String* reply);
  static bool getPresetJson(const char* slug, String* reply);
  static bool applyPreset(const char* slug, String* reply = nullptr);
  static bool saveCurrentAsPreset(const char* slug, String* reply = nullptr);
  static bool deletePreset(const char* slug, String* reply = nullptr);
  static bool setDefaultPreset(const char* slug, String* reply = nullptr);
  static bool resetToDefaultPreset(String* reply = nullptr);
  static void loadMachineProfile();
  static void saveMachineProfile();
  static void applyDerivedMachineConfig();
  static void printMachineProfileBanner();

  // --- Universal Wire Protocol Handlers ---
  // Handles SETTINGS.get or SETTINGS.get=<key>
  static bool get(const char* key, String* reply);
  static bool getGroup(const char* groupName, String* reply);

  // Handles SETTINGS.set=<key>,<val> or SETTINGS.set=<key>=<val>
  static bool set(const char* keyVal, String* reply);

  // Universal Reset Engine: SETTINGS.reset=<key> (reverts key to preset default)
  static bool reset(const char* key, String* reply = nullptr);
  static bool resetGroup(const char* group, String* reply = nullptr);

  // Schema Introspection: SETTINGS.schema or SETTINGS.schema=<name>
  static bool getSchema(const char* schemaName, String* reply);

  // --- Tier 2 / Category 1: Travel Stops & Boundaries ---
  static bool setAxisLimitDu(AxisId axis, LimitDirection dir, long limitDu);
  static bool clearAxisLimit(AxisId axis, LimitDirection dir);
  static bool hasAxisLimit(AxisId axis, LimitDirection dir);
  static long getAxisLimitDu(AxisId axis, LimitDirection dir);

  // --- Tier 2 / Category 2: Pitch, Threading & Feed Intent ---
  static bool setFeedrateDupr(long dupr, bool preserveDirection = true);
  static bool setFeedrateMm(float pitchMm, bool preserveDirection = true);
  static bool setFeedrateTpi(float tpi, bool preserveDirection = true);
  static bool setFeedrateInch(float pitchInch, bool preserveDirection = true);
  static bool reverseFeedDirection();
  static ThreadSetting getThreadSetting();
  static long getJogStepDu();
  static bool cycleJogStep();

  // --- Tier 2 / Category 3: Cycle & Geometry Parameters ---
  static bool setStarts(int starts);
  static int getStarts();
  static bool setTurnPasses(int passes);
  static int getTurnPasses();
  static bool setTaperRatio(float ratioK);
  static bool setTaperAngle(float angleDeg);
  static bool setTaperPreset(int presetIdx, float angleDeg, float ratioVal);
  static TaperSetting getTaperSetting();
  static float getTaperRatio();
  static bool setAuxForward(bool fwd);
  static bool getAuxForward();

  // --- Tier 2 / Category 4: Display & Unit Modes ---
  static bool setMeasure(MeasureUnit m);
  static bool setMeasure(int m);
  static MeasureUnit getMeasure();
  static bool setDiameterMode(bool dia);
  static bool isDiameterMode();
  static bool setAbsoluteMode(bool abs);
  static bool isAbsoluteMode();
  static bool setZDroMode(bool zdro);
  static bool isZDroMode();
  static bool setXDroMode(bool xdro);
  static bool isXDroMode();
  static bool setSpindleDisplayMode(int mode);
  static int getSpindleDisplayMode();

  // --- Tier 2 / Category 5: Operational Mode & Motion State ---
  static bool requestMode(int targetMode);
  static int getMode();
  static bool requestIsOn(bool on);
  static bool isOn();
  static void triggerEstop(int estopCode);
  static void clearEstop();
  static int getEstopCode();

  // Core 1 Synchronization Hook (Called from main.cpp / motion loop)
  static void applyCore1PendingLatches();

  // --- Tier 3: Workshop Operator Preferences ---
  static const OperatorPreferences& getPreferences() { return operatorPrefs; }
  static float getDeadbandAngle();
  static bool isDeadbandAngleAuto();
  static float getDeadbandTurns();
  static bool isDeadbandTurnsAuto();

  // --- Typed Mutators (Updates RAM & writes sparse override to NVS) ---
  static bool setJogStepDu(long du);
  static bool setDeadbandAngle(const char* val);
  static bool setDeadbandTurns(const char* val);

  // Tier 1 direct mutators
  static bool setScrewZDu(long du);
  static bool setScrewXDu(long du);
  static bool setMotorStepsZ(long steps);
  static bool setMotorStepsX(long steps);
  static bool setEncoderPpr(int ppr);
  static bool setSpindleRatio(float ratio);
};
