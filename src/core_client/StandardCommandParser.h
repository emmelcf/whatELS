#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "core/CommandResult.h"
#include "core/CoreCommands.h"
#include "core_client/ClientContext.h"

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

  // Namespace Dispatchers
  core::CommandResult parseCmd(const char* action, const char* args, const ClientContext& ctx);
  core::CommandResult parseState(const char* action, const char* args, const ClientContext& ctx);
  core::CommandResult parseSettings(const char* action, const char* args, const ClientContext& ctx);
  core::CommandResult parseProperties(const char* action, const char* args, const ClientContext& ctx);
  core::CommandResult parseSys(const char* action, const char* args, const ClientContext& ctx);
  core::CommandResult parseClient(const char* action, const char* args, const ClientContext& ctx);

  // Command Action Parsers
  core::CommandResult parseStartFeed(const char* args, const ClientContext& ctx);
  core::CommandResult parseStartCycle(const char* args, const ClientContext& ctx);
  core::CommandResult parseJogStep(const char* args, const ClientContext& ctx);
  core::CommandResult parseJogHold(const char* args, const ClientContext& ctx);
  core::CommandResult parseJogRelease(const char* args, const ClientContext& ctx);
  core::CommandResult parseAxisZero(const char* args, const ClientContext& ctx);
  core::CommandResult parseAxisPosDu(const char* args, const ClientContext& ctx);
  core::CommandResult parseAxisLimitDu(const char* args, const ClientContext& ctx);
  core::CommandResult parseAxisClearLimit(const char* args, const ClientContext& ctx);
};

} // namespace whatels::core_client

