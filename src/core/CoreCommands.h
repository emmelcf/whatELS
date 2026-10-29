#pragma once
#include <cstdint>
#include "hal/IStepperHAL.h"

namespace whatels::core {

/**
 * @brief Atomic Feed Motion Command (CMD.startFeed).
 */
struct StartFeedCmd {
  AxisId  axis         = AXIS_Z;
  int8_t  direction    = -1;    // -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  bool    syncSpindle  = true;  // true = ELS gearing (dupr), false = async IPM (du/min)
  int32_t feedRate     = 0;     // Magnitude of feed rate (> 0)
  int32_t targetDistDu = -1;    // Travel stroke in deci-microns (-1 = continuous to limit)
};

/**
 * @brief Incremental Single-Step Manual Jog Command (CMD.jogStep).
 */
struct JogStepCmd {
  AxisId  axis   = AXIS_Z;
  int32_t distDu = 0;           // Signed distance in deci-microns
};

/**
 * @brief Continuous Manual Jog Command (CMD.jogHold).
 */
struct JogHoldCmd {
  AxisId axis      = AXIS_Z;
  int8_t direction = -1;        // -1 or +1
};

/**
 * @brief Stop Continuous Manual Jog Command (CMD.jogRelease).
 */
struct JogReleaseCmd {
  AxisId axis = AXIS_Z;
};

/**
 * @brief Zero Axis Origin Command (CMD.axisZero).
 */
struct AxisZeroCmd {
  AxisId axis = AXIS_Z;
};

/**
 * @brief Set Axis Coordinate Preset Command (CMD.axisPosDu).
 */
struct AxisPosCmd {
  AxisId  axis  = AXIS_Z;
  int32_t posDu = 0;
};

/**
 * @brief Set Axis Travel Limit Command (CMD.axisLimitDu).
 */
struct AxisLimitCmd {
  AxisId  axis       = AXIS_Z;
  int8_t  limitBound = 1;       // 1 = Lower / Chuck, 2 = Upper / Tailstock
  int32_t coordDu    = 0;
};

/**
 * @brief Clear Axis Travel Limit Command (CMD.axisClearLimit).
 */
struct AxisClearLimitCmd {
  AxisId axis       = AXIS_Z;
  int8_t limitBound = 1;
};

/**
 * @brief In-Cut Radial Retract Feed Hold Command (CMD.hold).
 */
struct HoldCmd {};

/**
 * @brief Resume Feed from Hold Command (CMD.resume).
 */
struct ResumeCmd {};

/**
 * @brief Advance Next Pass in Step Mode Command (CMD.step).
 */
struct StepCmd {};

/**
 * @brief Universal Emergency Stop Command (CMD.stop).
 */
struct StopCmd {};

/**
 * @brief Line Flush and Heartbeat Ping Command (CMD.ping).
 */
struct PingCmd {};

} // namespace whatels::core

