#pragma once

/**
 * @file StandardCommandParser.h
 * @brief Authoritative wire protocol string parser for Subsystem 5.
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 * @see docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include "core/CommandResult.h"
#include "core/CoreCommands.h"
#include "core_client/ClientContext.h"

namespace whatels::core {
class ISystemInfoHAL;
}

namespace whatels::core_client {

/**
 * @brief Authoritative Wire Protocol String Parser for Subsystem 5.
 * @details Translates raw ASCII wire messages across the 5 canonical namespaces
 *          (CMD, STATE, SETTINGS, PROPERTIES, SYS) plus CLIENT into typed C++ structs.
 *          Has ZERO kinematic or motion knowledge; hands off typed execution to CoreCommandService.
 *          Enforces clean break rule: all legacy '?' queries are rejected with ERR:UNKNOWN_COMMAND.
 */
class StandardCommandParser {
public:
  static StandardCommandParser& instance();

  /**
   * @brief Injects the system diagnostics and appliance hardware info HAL provider.
   */
  void setSystemInfoHAL(core::ISystemInfoHAL* hal) { _systemInfoHal = hal; }
  core::ISystemInfoHAL* getSystemInfoHAL() const { return _systemInfoHal; }

  /**
   * @brief Parses an incoming ASCII wire line and dispatches to appropriate service.
   * @param line Raw line string (may include \r or \n)
   * @param length Length of line buffer
   * @param ctx Calling client session identity and permission bitmask
   * @return Formatted CommandResult
   */
  core::CommandResult parseAndDispatch(const char* line, size_t length, const ClientContext& ctx);

  /**
   * @brief Formats a CommandResult into a canonical wire ASCII response terminated with \n.
   */
  static String formatWireResponse(const core::CommandResult& res);

private:
  StandardCommandParser() = default;

  core::ISystemInfoHAL* _systemInfoHal = nullptr;
};

} // namespace whatels::core_client
