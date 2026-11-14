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
#include "core/PresetService.h"
#include "core/TelemetrySerializer.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include "motion/kernel/MotionEngine.h"
#include "motion/hal/MockStepperHAL.h"
#include "motion/hal/MockSpindleEncoderHAL.h"
#include "mocks/MockSystemInfoHAL.h"

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

bool SettingsService::setPreset(const char* slug, String* reply) {
  if (!slug || std::strcmp(slug, "unknown") == 0) {
    if (reply) *reply = "unknown preset slug";
    return false;
  }
  if (reply) *reply = String("OK:SETTINGS.preset=") + slug + "\n";
  return true;
}

static std::string s_lastCatalogCall;
bool PresetService::discover(const char* path, String* reply) {
  s_lastCatalogCall = std::string("discover:") + (path ? path : "<root>");
  if (reply) {
    *reply = path ? (String("{\"status\":\"ok\",\"domain\":\"") + path + "\",\"catalogs\":[\"iso_metric\"]}")
                  : "{\"status\":\"ok\",\"domains\":[\"profiles\",\"threads\",\"tapers\"]}";
  }
  return true;
}

bool PresetService::get(const char* query, String* reply) {
  s_lastCatalogCall = std::string("get:") + (query ? query : "");
  if (reply) *reply = "{\"status\":\"ok\",\"items\":[{\"id\":\"m10\"}]}";
  return true;
}

bool PresetService::save(const char* jsonPayload, String* reply) {
  s_lastCatalogCall = std::string("save:") + (jsonPayload ? jsonPayload : "");
  if (!jsonPayload || std::strstr(jsonPayload, "\"id\"") == nullptr) {
    if (reply) *reply = "INVALID_JSON";
    return false;
  }
  if (reply) *reply = "custom_m12";
  return true;
}

bool PresetService::remove(const char* jsonPayload, String* reply) {
  s_lastCatalogCall = std::string("remove:") + (jsonPayload ? jsonPayload : "");
  if (!jsonPayload || std::strstr(jsonPayload, "\"id\"") == nullptr) {
    if (reply) *reply = "INVALID_JSON";
    return false;
  }
  if (reply) *reply = "custom_m12";
  return true;
}

bool SettingsService::setAxisLimitDu(AxisId axis, LimitDirection dir, long limitDu) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  const char* label = (dir == LIMIT_LEFT_OR_UP) ? "LEFT" : "RIGHT";
  return MotionEngine::instance().setSoftLimitDu(axis, slot, limitDu, label, true);
}

bool SettingsService::clearAxisLimit(AxisId axis, LimitDirection dir) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  MotionEngine::instance().getSoftLimits(axis).clearLimit(slot);
  return true;
}

bool SettingsService::requestIsOn(bool /*on*/) { return true; }
bool SettingsService::setFeedrateDupr(long /*dupr*/, bool /*preserveDirection*/) { return true; }
long SettingsService::getJogStepDu() { return 1000; }

static UnifiedGearboxMode s_gearboxMode;
IELSMode* ModeRegistry::getActiveMode() {
  return nullptr;
}

UnifiedGearboxMode* ModeRegistry::getGearboxMode() {
  return &s_gearboxMode;
}

bool ModeRegistry::activateMode(ModeId /*id*/) {
  return true;
}

String ModeRegistry::serializeDiscoveryJson(uint32_t /*caps*/, bool all) {
  return all ? "{\"modes\":[{\"id\":50,\"name\":\"GEARBOX\"},{\"id\":57,\"name\":\"CONE\",\"status\":\"UNAVAILABLE\"}]}"
             : "{\"modes\":[{\"id\":50,\"name\":\"GEARBOX\"}]}";
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

void testCycleContract() {
  printHeader("2. Canned Cycle Ingress Contract (Pending IELSMode migration Refs #10)");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // Reset engine to idle via stop command
  assert(parser.parseAndDispatch("CMD.stop", 8, ctx).isOk());

  // Canned cycles are not yet implemented as IELSMode classes (Refs Issue #10);
  // they deterministically report UNAVAILABLE instead of faking execution.
  const char* threadCmd = "CMD.startCycle=mode:thread,pitch:15000,passes:8,depth:12000";
  CommandResult res = parser.parseAndDispatch(threadCmd, std::strlen(threadCmd), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Thread cycle -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startCycle:UNAVAILABLE\n");

  const char* turnCmd = "CMD.startCycle=mode:turn,feed:1500,doc:5000";
  res = parser.parseAndDispatch(turnCmd, std::strlen(turnCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Turn cycle -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startCycle:UNAVAILABLE\n");

  // Missing mode parameter fails with MISSING_PARAM:mode
  const char* missingMode = "CMD.startCycle=pitch:15000,passes:4";
  res = parser.parseAndDispatch(missingMode, std::strlen(missingMode), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Missing mode -> " << wire;
  assert(!res.isOk());
  assert(wire.find("MISSING_PARAM:mode") != std::string::npos);

  // Positional parameters strictly rejected for startCycle
  const char* posCycle = "CMD.startCycle=thread,15000";
  res = parser.parseAndDispatch(posCycle, std::strlen(posCycle), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Positional cycle -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:UNKNOWN_COMMAND\n");
}

void testFeedAndMotionCommands() {
  printHeader("4. Feed & In-Flight Control (hold, resume, step, stop)");
  auto& parser = StandardCommandParser::instance();
  auto ctx = makeAdminContext();

  // Start feed (Key-Value, integer deci-microns)
  const char* feedKv = "CMD.startFeed=axis:Z,feed:2500,dir:-,sync:1";
  CommandResult res = parser.parseAndDispatch(feedKv, std::strlen(feedKv), ctx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "StartFeed (KV) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startFeed\n");

  // In-motion safety gate [SAFE-01] verification: starting new feed while moving must fail!
  const char* feedConflict = "CMD.startFeed=axis:X,feed:1000,dir:+";
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

  // Step mode advance (pending Issue #10 canned cycle modes -> UNAVAILABLE)
  const char* stepCmd = "CMD.step";
  res = parser.parseAndDispatch(stepCmd, std::strlen(stepCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Step -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.step:UNAVAILABLE\n");

  // Stop
  const char* stopCmd = "CMD.stop";
  res = parser.parseAndDispatch(stopCmd, std::strlen(stopCmd), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "Stop -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.stop\n");

  // Start dual-axis compound feed (axis:ZX) requires xdir and taper
  const char* feedZxMissingXdir = "CMD.startFeed=axis:ZX,feed:2500,dir:CHUCK,taper:1000";
  res = parser.parseAndDispatch(feedZxMissingXdir, std::strlen(feedZxMissingXdir), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startFeed:MISSING_PARAM:xdir\n");

  const char* feedZxMissingTaper = "CMD.startFeed=axis:ZX,feed:2500,dir:CHUCK,xdir:RETRACT";
  res = parser.parseAndDispatch(feedZxMissingTaper, std::strlen(feedZxMissingTaper), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startFeed:MISSING_PARAM:taper\n");

  const char* feedZx = "CMD.startFeed=axis:ZX,feed:2500,dir:CHUCK,xdir:RETRACT,taper:1000,sync:1";
  res = parser.parseAndDispatch(feedZx, std::strlen(feedZx), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "StartFeed (ZX) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.startFeed\n");
  assert(s_gearboxMode.isDualAxis());
  assert(MotionEngine::instance().isMoving(AXIS_Z));
  assert(MotionEngine::instance().isMoving(AXIS_X));

  // Stop after ZX feed
  res = parser.parseAndDispatch(stopCmd, std::strlen(stopCmd), ctx);
  assert(res.isOk());
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

  // SYS.get=info & SYS.get=health (Decoupled ISystemInfoHAL seam, Refs Issue #9)
  // 1. Without provider, parser deterministically reports UNAVAILABLE
  parser.setSystemInfoHAL(nullptr);
  const char* sysInfo = "SYS.get=info";
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=info (no hal) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:UNAVAILABLE\n");

  const char* sysHealth = "SYS.get=health";
  res = parser.parseAndDispatch(sysHealth, std::strlen(sysHealth), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=health (no hal) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:UNAVAILABLE\n");

  // 2. With injected MockSystemInfoHAL provider, verifies live formatting and vitals
  MockSystemInfoHAL mockSysHal;
  parser.setSystemInfoHAL(&mockSysHal);

  // SYS.get=info
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=info (mock hal) -> " << wire;
  assert(res.isOk());
  assert(wire.find("SYS.version=H5V38") != std::string::npos);
  assert(wire.find("SYS.chip=ESP32-S3 rev 2") != std::string::npos);
  assert(wire.find("SYS.flash=16 MB") != std::string::npos);
  assert(wire.find("SYS.features=spindleSim,ota") != std::string::npos);

  // Bare SYS.get defaults to info
  const char* sysBare = "SYS.get";
  res = parser.parseAndDispatch(sysBare, std::strlen(sysBare), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get (bare mock hal) -> " << wire;
  assert(res.isOk());
  assert(wire.find("SYS.version=H5V38") != std::string::npos);

  // SYS.get=health
  res = parser.parseAndDispatch(sysHealth, std::strlen(sysHealth), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=health (mock hal) -> " << wire;
  assert(res.isOk());
  assert(wire.find("SYS.uptime=120") != std::string::npos);
  assert(wire.find("SYS.heap=142 KB") != std::string::npos);
  assert(wire.find("SYS.wifi=STA_CONNECTED") != std::string::npos);

  // Invalid parameter
  const char* sysInvalid = "SYS.get=bogus";
  res = parser.parseAndDispatch(sysInvalid, std::strlen(sysInvalid), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:INVALID_PARAM\n");

  // Permission check: without PERM_READ_TELEMETRY, access is rejected
  ClientContext unauthCtx = ctx;
  unauthCtx.effectivePerms = 0;
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), unauthCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:PERMISSION_DENIED\n");

  // 3. Reset provider restores UNAVAILABLE
  parser.setSystemInfoHAL(nullptr);
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:SYS.get:UNAVAILABLE\n");

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
  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

  // jogStep positional
  const char* stepPos = "CMD.jogStep=Z,100";
  CommandResult res = parser.parseAndDispatch(stepPos, std::strlen(stepPos), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (pos) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogStep\n");

  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

  // jogStep key-value
  const char* stepKv = "CMD.jogStep=axis:Z,dist:-500";
  res = parser.parseAndDispatch(stepKv, std::strlen(stepKv), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogStep (KV) -> " << wire;
  assert(res.isOk());
  assert(wire == "OK:CMD.jogStep\n");

  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

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
  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

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
  printHeader("9. Key-Value Feed Enforcement, Simulation Commands & System Ping");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();
  auto readOnlyCtx = makeReadOnlyContext();

  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

  // Positional startFeed must be rejected (KV required for 5-arg startFeed)
  const char* feedPos = "CMD.startFeed=Z,2500,-,1";
  CommandResult res = parser.parseAndDispatch(feedPos, std::strlen(feedPos), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "startFeed (pos rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startFeed:INVALID_PARAM\n");

  // Float decimal-point sniffing on wire must be rejected (pure integer du required)
  const char* feedFloat = "CMD.startFeed=axis:Z,feed:0.25,dir:-";
  res = parser.parseAndDispatch(feedFloat, std::strlen(feedFloat), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "startFeed (float rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startFeed:INVALID_PARAM:feed\n");

  // Unknown key typo (snyc:0 instead of sync:0) must be rejected
  const char* feedTypo = "CMD.startFeed=axis:Z,feed:2500,dir:-,snyc:0";
  res = parser.parseAndDispatch(feedTypo, std::strlen(feedTypo), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "startFeed (unknown key snyc rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.startFeed:INVALID_PARAM:snyc\n");

  // Axis-mismatched direction token (UP on Z axis) must be rejected
  const char* wrongAxisDir = "CMD.jogHold=axis:Z,dir:UP";
  res = parser.parseAndDispatch(wrongAxisDir, std::strlen(wrongAxisDir), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "jogHold (Z with UP rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.jogHold:INVALID_PARAM:dir\n");

  // Axis-mismatched limit bound (LEFT on X axis) must be rejected
  const char* wrongAxisBound = "CMD.axisLimitDu=X,LEFT,-10000";
  res = parser.parseAndDispatch(wrongAxisBound, std::strlen(wrongAxisBound), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "axisLimitDu (X with LEFT rejected) -> " << wire;
  assert(!res.isOk());
  assert(wire == "ERR:CMD.axisLimitDu:INVALID_PARAM:bound\n");

  // Simulation commands (CMD.simEncoder, CMD.simRpm, CMD.simPulses)
  const char* simEnc = "CMD.simEncoder=1";
  res = parser.parseAndDispatch(simEnc, std::strlen(simEnc), adminCtx);
  assert(res.isOk());
  res = parser.parseAndDispatch(simEnc, std::strlen(simEnc), readOnlyCtx);
  assert(!res.isOk());

  const char* simRpm = "CMD.simRpm=300";
  res = parser.parseAndDispatch(simRpm, std::strlen(simRpm), adminCtx);
  assert(res.isOk());

  const char* simPulses = "CMD.simPulses=400";
  res = parser.parseAndDispatch(simPulses, std::strlen(simPulses), adminCtx);
  assert(res.isOk());

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

void testCatalogFeedNudgeAndRefactorFixes() {
  printHeader("10. Unified Catalog (#37), Feed Nudge (#42), Modes=all (#27) & #48 Fixes");
  auto& parser = StandardCommandParser::instance();
  auto adminCtx = makeAdminContext();
  auto readOnlyCtx = makeReadOnlyContext();

  // Ensure idle
  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

  // 1. PROPERTIES.modes=all (#27)
  const char* modesAll = "PROPERTIES.modes=all";
  CommandResult res = parser.parseAndDispatch(modesAll, std::strlen(modesAll), adminCtx);
  String wire = StandardCommandParser::formatWireResponse(res);
  assert(res.isOk());
  assert(wire.find("CONE") != std::string::npos);

  const char* modesBad = "PROPERTIES.modes=invalid";
  res = parser.parseAndDispatch(modesBad, std::strlen(modesBad), adminCtx);
  assert(!res.isHandled());

  // 2. PROPERTIES.catalog (#37) with '=' and spaces inside OData argsStr (#48)
  const char* catRoot = "PROPERTIES.catalog";
  res = parser.parseAndDispatch(catRoot, std::strlen(catRoot), adminCtx);
  assert(res.isOk());
  assert(s_lastCatalogCall == "discover:<root>");

  const char* catDomain = "PROPERTIES.catalog=path=threads";
  res = parser.parseAndDispatch(catDomain, std::strlen(catDomain), adminCtx);
  assert(res.isOk());
  assert(s_lastCatalogCall == "discover:threads");

  const char* catOdata = "PROPERTIES.catalog=path=threads/iso_metric&$select=id,designation&$filter=series eq 'coarse'";
  res = parser.parseAndDispatch(catOdata, std::strlen(catOdata), adminCtx);
  assert(res.isOk());
  assert(s_lastCatalogCall == "get:path=threads/iso_metric&$select=id,designation&$filter=series eq 'coarse'");

  // 3. SETTINGS.catalogSave / SETTINGS.catalogDelete (#37) + permissions + [SAFE-01]
  const char* catSave = "SETTINGS.catalogSave={\"domain\":\"threads\",\"id\":\"custom_m12\",\"pitch_du\":17500}";
  res = parser.parseAndDispatch(catSave, std::strlen(catSave), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.catalogSave:custom_m12\n");

  res = parser.parseAndDispatch(catSave, std::strlen(catSave), readOnlyCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(!res.isOk());
  assert(wire == "ERR:SETTINGS.catalogSave:PERMISSION_DENIED\n");

  const char* catDel = "SETTINGS.catalogDelete={\"domain\":\"threads\",\"id\":\"custom_m12\"}";
  res = parser.parseAndDispatch(catDel, std::strlen(catDel), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.catalogDelete:custom_m12\n");

  // 4. SETTINGS.set=profile:<id> (#37) + [SAFE-01] motion lockout
  const char* setProf = "SETTINGS.set=profile:wm210v_stock";
  res = parser.parseAndDispatch(setProf, std::strlen(setProf), adminCtx);
  wire = StandardCommandParser::formatWireResponse(res);
  assert(res.isOk());
  assert(wire == "OK:SETTINGS.set:profile:wm210v_stock\n");

  const char* setProfEmpty = "SETTINGS.set=profile:";
  res = parser.parseAndDispatch(setProfEmpty, std::strlen(setProfEmpty), adminCtx);
  assert(!res.isOk());
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:SETTINGS.set:INVALID_PARAM:profile\n");

  // Engage motion and verify [SAFE-01] lockouts on profile, catalogSave, catalogDelete
  const char* startFeed = "CMD.startFeed=axis:Z,feed:1500,dir:-,sync:0";
  res = parser.parseAndDispatch(startFeed, std::strlen(startFeed), adminCtx);
  assert(res.isOk());

  res = parser.parseAndDispatch(setProf, std::strlen(setProf), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:SETTINGS.set:MOTION_LOCKED\n");
  res = parser.parseAndDispatch(catSave, std::strlen(catSave), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:SETTINGS.catalogSave:MOTION_LOCKED\n");
  res = parser.parseAndDispatch(catDel, std::strlen(catDel), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:SETTINGS.catalogDelete:MOTION_LOCKED\n");

  assert(parser.parseAndDispatch("CMD.stop", 8, adminCtx).isOk());

  // 5. CMD.feedNudge (#42)
  const char* nudgeBad = "CMD.feedNudge=+2";
  res = parser.parseAndDispatch(nudgeBad, std::strlen(nudgeBad), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:CMD.feedNudge:INVALID_PARAM:delta\n");

  const char* nudgeZero = "CMD.feedNudge=0";
  res = parser.parseAndDispatch(nudgeZero, std::strlen(nudgeZero), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:CMD.feedNudge:INVALID_PARAM:delta\n");

  const char* nudgeMissing = "CMD.feedNudge";
  res = parser.parseAndDispatch(nudgeMissing, std::strlen(nudgeMissing), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:CMD.feedNudge:MISSING_ARGUMENTS\n");

  const char* nudgeUp = "CMD.feedNudge=+1";
  res = parser.parseAndDispatch(nudgeUp, std::strlen(nudgeUp), adminCtx);
  // Note: CoreCommandService may return OK or UNAVAILABLE depending on active mode; parser must dispatch it (not INVALID_PARAM/UNKNOWN_COMMAND)
  assert(res.isHandled());
  assert(res.payload != "INVALID_PARAM:delta");

  // 6. Float overflow rejection in CMD.simRpm (#48 std::from_chars)
  const char* simOverflow = "CMD.simRpm=1e40";
  res = parser.parseAndDispatch(simOverflow, std::strlen(simOverflow), adminCtx);
  assert(StandardCommandParser::formatWireResponse(res) == "ERR:CMD.simRpm:INVALID_PARAM:rpm\n");
}

int main() {
  std::cout << "========================================================\n";
  std::cout << " RUNNING STANDARD COMMAND PARSER NATIVE UNIT TEST SUITE\n";
  std::cout << "========================================================\n";

  static MockStepperHAL mockStepper;
  static MockSpindleEncoderHAL mockEncoder;
  MotionEngine::instance().setStepperHAL(&mockStepper);
  MotionEngine::instance().setEncoderHAL(&mockEncoder);

  testCleanBreakQueries();
  testCycleContract();
  testFeedAndMotionCommands();
  testUniversalNamespaces();
  testStrictGrammarAndSpaceRejection();
  testManualJogging();
  testCoordinatesAndLimits();
  testPositionalFeedAndPing();
  testSettingsAndExtendedIntrospection();
  testCatalogFeedNudgeAndRefactorFixes();

  std::cout << "\n========================================================\n";
  std::cout << " ALL 10 TEST SUITES PASSED CLEANLY (100% SUCCESS)\n";
  std::cout << "========================================================\n";
  return 0;
}
