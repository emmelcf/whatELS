#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "core_client/StandardCommandParser.h"
#include "core_client/ClientContext.h"
#include "core/CoreCommands.h"
#include "core/CoreCommandService.h"
#include "core/SettingsService.h"
#include "core/TelemetrySerializer.h"
#include "modes/ModeRegistry.h"
#include "motion/kernel/MotionEngine.h"
#include "motion/hal/MockStepperHAL.h"
#include "motion/hal/MockSpindleEncoderHAL.h"

using namespace whatels::core;
using namespace whatels::core_client;

// =============================================================================
// TEST DOUBLES FOR SERVICES
// =============================================================================

static LatheConfiguration s_mockConfig;
const LatheConfiguration& SettingsService::getActiveProfile() {
  return s_mockConfig;
}

bool SettingsService::get(const char* key, String* out) {
  if (out) *out = key ? String("val_") + key : "val_all";
  return true;
}

bool SettingsService::set(const char* kv, String* out) {
  if (out) *out = kv ? kv : "";
  return true;
}

bool SettingsService::reset(const char* key, String* out) {
  if (out) *out = key ? key : "all";
  return true;
}

bool SettingsService::getSchema(const char* name, String* out) {
  if (out) *out = name ? String("{\"schema\":\"") + name + "\"}" : "{}";
  return true;
}

bool SettingsService::listPresets(String* out) {
  if (out) *out = "[\"preset1\",\"preset2\"]";
  return true;
}

String ModeRegistry::serializeDiscoveryJson(uint32_t /*caps*/, bool /*all*/) {
  return "{\"modes\":[{\"id\":50,\"name\":\"GEARBOX\"}]}";
}

String TelemetrySerializer::buildTelemetry(const MachineModel& /*model*/, uint8_t domainMask) {
  return String("K.zDu=0|K.xDu=0|mask=") + std::to_string(domainMask);
}

MachineModel MachineModel::captureAll() {
  return MachineModel();
}

// =============================================================================
// TEST HARNESS HELPERS
// =============================================================================

static void printHeader(const char* name) {
  std::cout << "\n==================================================\n";
  std::cout << " TEST: " << name << "\n";
  std::cout << "==================================================\n";
}

static ClientContext makeAdminContext() {
  ClientContext ctx;
  ctx.sessionId = 1;
  ctx.effectivePerms = PERM_ALL;
  return ctx;
}

static ClientContext makeReadOnlyContext() {
  ClientContext ctx;
  ctx.sessionId = 2;
  ctx.effectivePerms = PERM_READ_TELEMETRY;
  return ctx;
}

// =============================================================================
// TEST SUITES
// =============================================================================

void testCleanBreakQueries() {
  printHeader("1. Clean Break Rule: Rejection of all legacy '?' queries");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  const char* queries[] = {
    "?sysinfo",
    "?sysinfo\n",
    "?state",
    "?state=ALL",
    "?get=pitch",
    "?",
    "  ?sysinfo  \r\n"
  };

  for (const char* q : queries) {
    CommandResult res = parser.parseAndDispatch(q, std::strlen(q), ctx);
    String wire = StandardCommandParser::formatWireResponse(res);
    std::cout << "Query '" << q << "' -> " << wire;
    assert(!res.isHandled());
    assert(wire == "ERR:UNKNOWN_COMMAND\n");
  }
}

void testOrderIndependentCycleParsing() {
  printHeader("2. Order-Independent Key-Value Cycle Parsing");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // Reset engine to idle via stop command
  parser.parseAndDispatch("CMD.stop", 8, ctx);

  // Scrambled threading command with float mm coercion (depth:1.2 mm -> 12000 du)
  const char* threadCmd = "CMD.startCycle=passes:8,pitch:15000,zEnd:-25000,xClear:20000,depth:1.2,mode:thread,zStart:0,step:1";
  CommandResult res = parser.parseAndDispatch(threadCmd, std::strlen(threadCmd), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Thread result: " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startCycle:mode:thread\n");

  // Turning cycle
  const char* turnCmd = "CMD.startCycle=mode:turn,feed:0.15,doc:0.5,zStart:0,zEnd:-50000,xStart:15000,xClear:20000,passes:4";
  res = parser.parseAndDispatch(turnCmd, std::strlen(turnCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Turn result: " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startCycle:mode:turn\n");

  // Facing cycle
  const char* faceCmd = "CMD.startCycle=mode:face,feed:0.10,doc:0.25,xStart:25000,xEnd:0,zStart:0,zClear:2000";
  res = parser.parseAndDispatch(faceCmd, std::strlen(faceCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Face result: " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startCycle:mode:face\n");

  // Taper cycle (using ratio)
  const char* taperCmd = "CMD.startCycle=mode:taper,feed:1500,ratio:1:5,zStart:0,zEnd:-30000,xStart:10000,xClear:15000";
  res = parser.parseAndDispatch(taperCmd, std::strlen(taperCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Taper result: " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startCycle:mode:taper\n");

  // Cutoff cycle
  const char* cutoffCmd = "CMD.startCycle=mode:cutoff,feed:500,peck:10000,retract:1000,xStart:20000,xEnd:0,zPos:-15000";
  res = parser.parseAndDispatch(cutoffCmd, std::strlen(cutoffCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Cutoff result: " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startCycle:mode:cutoff\n");
}

void testCycleValidationAndErrors() {
  printHeader("3. Parameter Validation and Deterministic Errors");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // Missing mode
  const char* missingMode = "CMD.startCycle=pitch:15000,passes:4";
  CommandResult res = parser.parseAndDispatch(missingMode, std::strlen(missingMode), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Missing mode -> " << wire;
  assert(!res.isOk());
  assert(wire.find("MISSING_PARAM:mode") != std::string::npos);

  // Missing pitch in threading
  const char* missingPitch = "CMD.startCycle=mode:thread,depth:10000,zStart:0,zEnd:-10000,xClear:15000";
  res = parser.parseAndDispatch(missingPitch, std::strlen(missingPitch), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Missing pitch -> " << wire;
  assert(!res.isOk());
  assert(wire.find("MISSING_PARAM:pitch") != std::string::npos);

  // Invalid pitch (negative or non-numeric)
  const char* invalidPitch = "CMD.startCycle=mode:thread,pitch:invalid,depth:10000,zStart:0,zEnd:-10000,xClear:15000";
  res = parser.parseAndDispatch(invalidPitch, std::strlen(invalidPitch), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Invalid pitch -> " << wire;
  assert(!res.isOk());
  assert(wire.find("INVALID_PARAM:pitch") != std::string::npos);

  // Unknown mode
  const char* unknownMode = "CMD.startCycle=mode:banana,feed:1500";
  res = parser.parseAndDispatch(unknownMode, std::strlen(unknownMode), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Unknown mode -> " << wire;
  assert(!res.isOk());
  assert(wire.find("UNKNOWN_MODE:banana") != std::string::npos);
}

void testFeedAndMotionCommands() {
  printHeader("4. Feed & In-Flight Control (hold, resume, step, stop)");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // Start feed (Key-Value)
  const char* feedKv = "CMD.startFeed=axis:Z,feed:0.25,dir:-,sync:1";
  CommandResult res = parser.parseAndDispatch(feedKv, std::strlen(feedKv), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "StartFeed (KV) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startFeed\n");

  // In-motion safety gate [SAFE-01] verification: starting new feed while moving must fail!
  const char* feedConflict = "CMD.startFeed=axis:X,feed:0.10,dir:+";
  res = parser.parseAndDispatch(feedConflict, std::strlen(feedConflict), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "In-motion conflict -> " << wire;
  assert(!res.isOk());
  assert(wire.find("MOTION_LOCKED") != std::string::npos);

  // Feed hold (radial retract)
  const char* holdCmd = "CMD.hold";
  res = parser.parseAndDispatch(holdCmd, std::strlen(holdCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Hold -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.hold\n");

  // Resume
  const char* resumeCmd = "CMD.resume";
  res = parser.parseAndDispatch(resumeCmd, std::strlen(resumeCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Resume -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.resume\n");

  // Step mode advance
  const char* stepCmd = "CMD.step";
  res = parser.parseAndDispatch(stepCmd, std::strlen(stepCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Step -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.step\n");

  // Stop
  const char* stopCmd = "CMD.stop";
  res = parser.parseAndDispatch(stopCmd, std::strlen(stopCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Stop -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.stop\n");
}

void testUniversalNamespaces() {
  printHeader("5. Universal Namespaces (STATE, SETTINGS, PROPERTIES, SYS, CLIENT)");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // STATE.get=ALL
  const char* stateAll = "STATE.get=ALL";
  CommandResult res = parser.parseAndDispatch(stateAll, std::strlen(stateAll), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "STATE.get=ALL -> " << wire;
  assert(res.isOk());
  assert(wire.find("K.zDu=0") != std::string::npos);

  // STATE.get=K,I
  const char* statePartial = "STATE.get=K,I";
  res = parser.parseAndDispatch(statePartial, std::strlen(statePartial), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "STATE.get=K,I -> " << wire;
  assert(res.isOk());
  assert(wire.find("mask=3") != std::string::npos); // K(1) | I(2) = 3

  // SYS.get=info & SYS.get=health (Tracked in Issue #9: decoupled ISystemInfoHAL seam)
  // On native host runner without hardware or mock HAL seam, parser cleanly reports UNAVAILABLE.
  const char* sysInfo = "SYS.get=info";
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=info -> " << wire;
#ifdef ARDUINO
  assert(res.isOk());
  assert(wire.find("SYS.version=") != std::string::npos);
#else
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:UNAVAILABLE\n");
#endif

  const char* sysHealth = "SYS.get=health";
  res = parser.parseAndDispatch(sysHealth, std::strlen(sysHealth), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=health -> " << wire;
#ifdef ARDUINO
  assert(res.isOk());
  assert(wire.find("SYS.uptime=") != std::string::npos);
#else
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:UNAVAILABLE\n");
#endif

  // PROPERTIES.modes
  const char* propModes = "PROPERTIES.modes";
  res = parser.parseAndDispatch(propModes, std::strlen(propModes), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "PROPERTIES.modes -> " << wire;
  assert(res.isOk());
  assert(wire.find("GEARBOX") != std::string::npos);

  // SETTINGS.get
  const char* setGet = "SETTINGS.get=pitch";
  res = parser.parseAndDispatch(setGet, std::strlen(setGet), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.get -> " << wire;
  assert(res.isOk());

  // CLIENT.hello handshake (Strict single '=', colon parameters, quoted label containing spaces)
  const char* clientHello = "CLIENT.hello=type:smarttouch,ver:1.2.0,label:\"Apron Console\"";
  res = parser.parseAndDispatch(clientHello, std::strlen(clientHello), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "CLIENT.hello -> " << wire;
  assert(res.isOk());
  assert(wire.find("session_id=1") != std::string::npos);
  assert(wire.find("type=smarttouch") != std::string::npos);
}

void testStrictGrammarAndSpaceRejection() {
  printHeader("6. Strict Grammar, Single-Equals, and Space Rejection Matrix");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  const char* invalidCommands[] = {
    // 1. Loose spaces around '=' or action
    "CMD.startFeed = axis:Z,feed:1500",
    "CMD. startFeed=axis:Z,feed:1500",
    "CMD .startFeed=axis:Z,feed:1500",
    // 2. Loose spaces inside arguments
    "CMD.startFeed=axis:Z, feed:1500",
    "CMD.startFeed=axis: Z,feed:1500",
    "CMD.startFeed=axis :Z,feed:1500",
    // 3. Space-separated commands (missing '=')
    "CLIENT.hello type:smarttouch",
    "CMD.startFeed axis:Z",
    "CMD.stop now",
    // 4. Trailing spaces
    "CMD.stop ",
    "CMD.hold \t",
    // 5. Multiple '=' signs (using '=' instead of ':')
    "CLIENT.hello=type=smarttouch",
    "CLIENT.hello=type=smarttouch,ver=1.2.0",
    "CMD.startFeed=axis=Z,feed=1500",
    "CMD.startCycle=mode=thread,pitch=15000",
    // 6. Unexpected '=' on no-arg commands
    "CMD.stop=",
    "CMD.stop=1",
    "CMD.hold=true",
    "CMD.resume=1",
    "CMD.step=next",
    // 7. Trailing or double commas
    "CMD.startFeed=axis:Z,feed:1500,",
    "CMD.startFeed=axis:Z,,feed:1500",
    // 8. Missing colon in key-value token
    "CMD.startCycle=thread,pitch:15000"
  };

  for (const char* cmd : invalidCommands) {
    CommandResult res = parser.parseAndDispatch(cmd, std::strlen(cmd), ctx);
    String wire = StandardCommandParser::formatWireResponse(res);
    std::cout << "Asserting rejection of: '" << cmd << "' -> " << wire;
    assert(!res.isHandled());
    assert(wire == "ERR:UNKNOWN_COMMAND\n");
  }

  std::cout << "  [PASS] All malformed, space-contaminated, and multi-'=' commands strictly rejected.\n";
}

void testManualJogging() {
  printHeader("7. Manual Jogging (jogStep, jogHold, jogRelease & Permissions)");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();
  auto readOnlyCtx = makeReadOnlyContext();

  // Reset to idle
  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // jogStep positional
  const char* stepPos = "CMD.jogStep=Z,100";
  CommandResult res = parser.parseAndDispatch(stepPos, std::strlen(stepPos), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (pos) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogStep\n");

  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // jogStep key-value
  const char* stepKv = "CMD.jogStep=axis:Z,dist:-500";
  res = parser.parseAndDispatch(stepKv, std::strlen(stepKv), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (KV) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogStep\n");

  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // jogHold positional
  const char* holdPos = "CMD.jogHold=Z,+";
  res = parser.parseAndDispatch(holdPos, std::strlen(holdPos), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogHold (pos) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogHold\n");

  // jogRelease with axis
  const char* relAxis = "CMD.jogRelease=Z";
  res = parser.parseAndDispatch(relAxis, std::strlen(relAxis), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogRelease (axis) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogRelease\n");

  // jogRelease bare (no axis)
  const char* relBare = "CMD.jogRelease";
  res = parser.parseAndDispatch(relBare, std::strlen(relBare), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogRelease (bare) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogRelease\n");

  // jogHold key-value
  const char* holdKv = "CMD.jogHold=axis:X,dir:-";
  res = parser.parseAndDispatch(holdKv, std::strlen(holdKv), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogHold (KV) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogHold\n");
  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // Permission check: read-only client must be rejected
  res = parser.parseAndDispatch(stepPos, std::strlen(stepPos), readOnlyCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (read-only rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire.find("PERMISSION_DENIED") != std::string::npos);

  // Lathe safety: invalid axis or direction must be rejected deterministically (zero silent fallback)
  const char* invalidAxis = "CMD.jogStep=INVALID,100";
  res = parser.parseAndDispatch(invalidAxis, std::strlen(invalidAxis), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (invalid axis) -> " << wire;
  assert(!res.isOk());
  assert(wire.find("INVALID_PARAM:axis") != std::string::npos);

  const char* invalidDir = "CMD.jogHold=Z,INVALID";
  res = parser.parseAndDispatch(invalidDir, std::strlen(invalidDir), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogHold (invalid dir) -> " << wire;
  assert(!res.isOk());
  assert(wire.find("INVALID_PARAM:dir") != std::string::npos);
}

void testCoordinatesAndLimits() {
  printHeader("8. Coordinates & Travel Limits (axisZero, axisPosDu, axisLimitDu, axisClearLimit)");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();

  // axisZero
  const char* zeroCmd = "CMD.axisZero=Z";
  CommandResult res = parser.parseAndDispatch(zeroCmd, std::strlen(zeroCmd), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "axisZero -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.axisZero\n");

  // axisPosDu
  const char* posCmd = "CMD.axisPosDu=Z,500000";
  res = parser.parseAndDispatch(posCmd, std::strlen(posCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "axisPosDu -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.axisPosDu\n");

  // axisLimitDu (Left limit)
  const char* limitCmd = "CMD.axisLimitDu=Z,LEFT,-100000";
  res = parser.parseAndDispatch(limitCmd, std::strlen(limitCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "axisLimitDu -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.axisLimitDu\n");

  // axisClearLimit
  const char* clearCmd = "CMD.axisClearLimit=Z,LEFT";
  res = parser.parseAndDispatch(clearCmd, std::strlen(clearCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "axisClearLimit -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.axisClearLimit\n");
}

void testPositionalFeedAndPing() {
  printHeader("9. Positional Feed & System Ping");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();

  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // Positional startFeed
  const char* feedPos = "CMD.startFeed=Z,0.25,-,1";
  CommandResult res = parser.parseAndDispatch(feedPos, std::strlen(feedPos), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "startFeed (pos) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startFeed\n");

  parser.parseAndDispatch("CMD.stop", 8, adminCtx);

  // Ping
  const char* pingCmd = "CMD.ping";
  res = parser.parseAndDispatch(pingCmd, std::strlen(pingCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "ping -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.ping\n");
}

void testSettingsAndExtendedIntrospection() {
  printHeader("10. Settings Permissions, Presets/Schema & Bare State Hydration");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();
  auto readOnlyCtx = makeReadOnlyContext();

  // SETTINGS.get (all)
  const char* setGetAll = "SETTINGS.get";
  CommandResult res = parser.parseAndDispatch(setGetAll, std::strlen(setGetAll), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.get (all) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.get:val_all\n");

  // SETTINGS.set with admin permissions
  const char* setWrite = "SETTINGS.set=leadscrew_pitch:4.0";
  res = parser.parseAndDispatch(setWrite, std::strlen(setWrite), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.set (admin) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.set:leadscrew_pitch:4.0\n");

  // SETTINGS.set with read-only permissions (MUST be rejected with 403)
  res = parser.parseAndDispatch(setWrite, std::strlen(setWrite), readOnlyCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.set (read-only rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire.find("PERMISSION_DENIED") != std::string::npos);

  // SETTINGS.reset with admin
  const char* setReset = "SETTINGS.reset=leadscrew_pitch";
  res = parser.parseAndDispatch(setReset, std::strlen(setReset), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.reset (admin) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.reset:leadscrew_pitch\n");

  // SETTINGS.reset with read-only (MUST be rejected)
  res = parser.parseAndDispatch(setReset, std::strlen(setReset), readOnlyCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.reset (read-only rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire.find("PERMISSION_DENIED") != std::string::npos);

  // SETTINGS.schema
  const char* schemaCmd = "SETTINGS.schema=motor_z";
  res = parser.parseAndDispatch(schemaCmd, std::strlen(schemaCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.schema -> " << wire;
  assert(res.isOk());
  assert(wire.find("motor_z") != std::string::npos);

  // SETTINGS.presets
  const char* presetsCmd = "SETTINGS.presets";
  res = parser.parseAndDispatch(presetsCmd, std::strlen(presetsCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SETTINGS.presets -> " << wire;
  assert(res.isOk());
  assert(wire.find("preset1") != std::string::npos);

  // PROPERTIES.profiles
  const char* profilesCmd = "PROPERTIES.profiles";
  res = parser.parseAndDispatch(profilesCmd, std::strlen(profilesCmd), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "PROPERTIES.profiles -> " << wire;
  assert(res.isOk());
  assert(wire.find("preset1") != std::string::npos);

  // PROPERTIES.get
  const char* propGet = "PROPERTIES.get=leadscrew_pitch";
  res = parser.parseAndDispatch(propGet, std::strlen(propGet), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "PROPERTIES.get -> " << wire;
  assert(res.isOk());
  assert(wire.find("leadscrew_pitch") != std::string::npos);

  // Bare STATE.get (no args)
  const char* bareState = "STATE.get";
  res = parser.parseAndDispatch(bareState, std::strlen(bareState), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "STATE.get (bare) -> " << wire;
  assert(res.isOk());
  assert(wire.find("mask=15") != std::string::npos); // DOMAIN_ALL (15)

  // Strict Case Sensitivity & Binary Flag Rejection: wrong-cased and text boolean variants MUST be rejected
  const char* wrongCaseCommands[] = {
    "cmd.STOP",
    "CMD.STOP",
    "cmd.stop",
    "STATE.GET",
    "state.get",
    "SETTINGS.GET",
    "CMD.STARTFEED=axis:Z,feed:1500,dir:-",
    "CMD.startFeed=axis:z,feed:1500,dir:-", // lower-case 'z' rejected
    "CMD.startFeed=AXIS:Z,feed:1500,dir:-", // upper-case 'AXIS' rejected
    "CMD.startFeed=axis:Z,feed:1500,dir:-,sync:true",  // text boolean 'true' rejected
    "CMD.startFeed=axis:Z,feed:1500,dir:-,sync:sync",  // text alias 'sync' rejected
    "CMD.startFeed=axis:Z,feed:1500,dir:-,sync:false", // text boolean 'false' rejected
    "CMD.startFeed=axis:Z,feed:1500,dir:-,sync:async"  // text alias 'async' rejected
  };

  for (const char* badCase : wrongCaseCommands) {
    res = parser.parseAndDispatch(badCase, std::strlen(badCase), adminCtx);
    wire = StandardCommandParser::formatWireResponse(res);
    std::cout << "Asserting case rejection: '" << badCase << "' -> " << wire;
    assert(!res.isOk());
    assert(wire == "ERR:UNKNOWN_COMMAND\n" || wire.find("INVALID_PARAM") != std::string::npos);
  }
}

int main() {
  std::cout << "========================================================\n";
  std::cout << " RUNNING STANDARD COMMAND PARSER NATIVE UNIT TEST SUITE\n";
  std::cout << "========================================================\n";

  static MockStepperHAL mockStepper;
  static MockSpindleEncoderHAL mockEncoder;
  MotionEngine engine(&mockStepper, &mockEncoder);

  testCleanBreakQueries();
  testOrderIndependentCycleParsing();
  testCycleValidationAndErrors();
  testFeedAndMotionCommands();
  testUniversalNamespaces();
  testStrictGrammarAndSpaceRejection();
  testManualJogging();
  testCoordinatesAndLimits();
  testPositionalFeedAndPing();
  testSettingsAndExtendedIntrospection();

  std::cout << "\n========================================================\n";
  std::cout << " ALL 10 TEST SUITES PASSED CLEANLY (100% SUCCESS)\n";
  std::cout << "========================================================\n";
  return 0;
}
