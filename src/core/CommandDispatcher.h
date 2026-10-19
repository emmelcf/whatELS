#pragma once

#include <Arduino.h>
#include "../motion/modes/IELSMode.h"

namespace nanoels {
namespace core {

enum class CommandStatus : uint8_t {
  OK = 0,            // Command recognized and executed successfully
  FAILED,            // Command recognized, but rejected or failed during execution
  UNRECOGNIZED       // Command was not recognized in any routing table
};

struct CommandResult {
  CommandStatus status = CommandStatus::UNRECOGNIZED;
  const char*   command = "";      // Canonical command name (e.g. "stop", "axisZero", "startFeed")
  String        payload = "";      // Error reason on failure, custom data/ack on success
  uint32_t      errorCode = 0;     // Structured error code (0 = NONE)

  static CommandResult ok(const char* cmd, const String& data = "") {
    return { CommandStatus::OK, cmd, data, 0 };
  }

  static CommandResult failed(const char* cmd, const String& reason, uint32_t code = 0) {
    return { CommandStatus::FAILED, cmd, reason, code };
  }

  static CommandResult unrecognized(const char* cmd = "") {
    return { CommandStatus::UNRECOGNIZED, cmd, "", 0 };
  }

  bool isOk() const { return status == CommandStatus::OK; }
  bool isHandled() const { return status != CommandStatus::UNRECOGNIZED; }
};

class CommandDispatcher {
public:
  // Dispatches an incoming canonical command string from any transport.
  // Returns strongly typed CommandResult. Zero transport or wire string formatting.
  static CommandResult dispatch(const char* payload, size_t length, uint8_t clientId = 255);
};

} // namespace core
} // namespace nanoels
