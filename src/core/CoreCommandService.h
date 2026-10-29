#pragma once

#include "CoreCommands.h"
#include "CommandResult.h"
#include "core_client/ClientContext.h"
#include "motion/kernel/MotionEngine.h"

namespace whatels::core {

/**
 * @brief Authoritative Core Domain Execution Service and Motion Safety Gate.
 * @details Conforms to SPECIFICATION_PROTOCOL_RATIONALIZATION_AND_SUBSYSTEM5.md Section 4.
 *          Has ZERO knowledge of string parsing or wire protocols.
 *          Enforces [SAFE-01] motion lockout and ADR-15 zero ghost limit persistence.
 */
class CoreCommandService {
public:
  static CoreCommandService& instance();

  // Typed Command Execution Surface
  CommandResult execute(const StartFeedCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const StartCycleCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const JogStepCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const JogHoldCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const JogReleaseCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const AxisZeroCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const AxisPosCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const AxisLimitCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const AxisClearLimitCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const HoldCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const ResumeCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const StepCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const StopCmd& cmd, const core_client::ClientContext& ctx);
  CommandResult execute(const PingCmd& cmd, const core_client::ClientContext& ctx);

  // [SAFE-01] Motion Safety Interlock Query
  bool canExecuteMotion(const core_client::ClientContext& ctx, String* errReason) const;

private:
  CoreCommandService() = default;

  // Validation helpers
  bool validateEnvelope(int32_t zStart, int32_t zEnd, int32_t xStart, int32_t xEnd, String* errReason) const;
};

} // namespace whatels::core

