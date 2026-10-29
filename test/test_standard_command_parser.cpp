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

  // SYS.get=info
  const char* sysInfo = "SYS.get=info";
  res = parser.parseAndDispatch(sysInfo, std::strlen(sysInfo), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=info -> " << wire;
  assert(res.isOk());
  assert(wire.find("SYS.version=") != std::string::npos);

  // SYS.get=health
  const char* sysHealth = "SYS.get=health";
  res = parser.parseAndDispatch(sysHealth, std::strlen(sysHealth), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "SYS.get=health -> " << wire;
  assert(res.isOk());
  assert(wire.find("SYS.uptime=") != std::string::npos);

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

  // CLIENT.hello handshake
  const char* clientHello = "CLIENT.hello type=smarttouch,ver=1.2.0,label=\"Apron Console\"";
  res = parser.parseAndDispatch(clientHello, std::strlen(clientHello), ctx);
  wire = StandardCommandParser::formatWireResponse(res);
  std::cout << "CLIENT.hello -> " << wire;
  assert(res.isOk());
  assert(wire.find("session_id=1") != std::string::npos);
  assert(wire.find("type=smarttouch") != std::string::npos);
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

  std::cout << "\n========================================================\n";
  std::cout << " ALL 5 TEST SUITES PASSED CLEANLY (100% SUCCESS)\n";
  std::cout << "========================================================\n";
  return 0;
}
