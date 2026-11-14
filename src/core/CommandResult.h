#pragma once

/**
 * @file CommandResult.h
 * @brief Standardized command execution status and result payload.
 * @see docs/adr/0003-command-result-and-baseline-client-architecture.md
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 */

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

namespace whatels::core {

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

} // namespace whatels::core
