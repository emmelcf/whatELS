#pragma once
#include <cstdint>
#include <variant>
#include "hal/IStepperHAL.h"

namespace whatels::core {

/**
 * @brief Canonical cycle types supported by whatELS kinematic kernel.
 */
enum class CycleType : uint8_t {
  THREAD = 0,
  TURN,
  FACE,
  TAPER,
  CUTOFF
};

/**
 * @brief Strongly typed parameters for Multi-Pass Threading Cycle.
 */
struct ThreadingCycleParams {
  int32_t  pitchDupr    = 0;     // Thread lead in deci-microns/spindle revolution (e.g. 15000 = 1.5mm)
  int32_t  depthDu      = 0;     // Total radial thread crest-to-root depth
  uint16_t passes       = 1;     // Total number of cutting passes
  uint16_t springPasses = 0;     // Zero-infeed finish clean-up passes
  float    infeedAngle  = 29.5f; // Compound tool plunge trajectory angle in degrees
  int32_t  zStartDu     = 0;     // Longitudinal thread start point
  int32_t  zEndDu       = 0;     // Longitudinal thread end / shoulder stop (persists in soft limits)
  int32_t  xClearDu     = 0;     // Radial clearance coordinate for rapid return stroke
  bool     stepMode     = false; // Pause after each pass at clearance for operator measurement
};

/**
 * @brief Strongly typed parameters for Multi-Pass Longitudinal Turning Cycle.
 */
struct TurningCycleParams {
  int32_t  feedDupr     = 0;     // Feed per revolution (dupr) or feed per minute (du/min)
  bool     syncSpindle  = true;  // true = ELS gearing (G95), false = velocity feed (G94)
  int32_t  docDu        = 0;     // Depth of cut per pass in deci-microns
  uint16_t passes       = 1;     // Total number of passes
  int32_t  zStartDu     = 0;     // Longitudinal start point
  int32_t  zEndDu       = 0;     // Longitudinal shoulder limit (persists in soft limits)
  int32_t  xStartDu     = 0;     // Radial start coordinate
  int32_t  xClearDu     = 0;     // Radial clearance coordinate
  bool     stepMode     = false; // Pause after each pass at clearance
};

/**
 * @brief Strongly typed parameters for Facing Cycle.
 */
struct FacingCycleParams {
  int32_t  feedDupr     = 0;
  bool     syncSpindle  = true;
  int32_t  docDu        = 0;
  uint16_t passes       = 1;
  int32_t  xStartDu     = 0;     // Radial start coordinate (outer diameter)
  int32_t  xEndDu       = 0;     // Radial end coordinate (centerline or bore)
  int32_t  zStartDu     = 0;     // Longitudinal cut datum
  int32_t  zClearDu     = 0;     // Longitudinal clearance coordinate for return stroke
  bool     stepMode     = false;
};

/**
 * @brief Strongly typed parameters for Taper Turning Cycle.
 */
struct TaperCycleParams {
  int32_t  feedDupr      = 0;
  bool     syncSpindle   = true;
  float    taperAngleDeg = 0.0f; // Half-angle in decimal degrees
  int32_t  zStartDu      = 0;
  int32_t  zEndDu        = 0;
  int32_t  xStartDu      = 0;
  int32_t  xClearDu      = 0;
  bool     stepMode      = false;
};

/**
 * @brief Strongly typed parameters for Peck Parting / Grooving Cycle.
 */
struct CutoffCycleParams {
  int32_t  feedDupr    = 0;
  int32_t  peckDepthDu = 0;     // Incremental radial plunge depth per peck
  int32_t  retractDu   = 0;     // Radial chip-break retract distance
  int32_t  xStartDu    = 0;     // Initial outer diameter coordinate
  int32_t  xEndDu      = 0;     // Target cutoff depth (centerline or internal bore)
  int32_t  zPosDu      = 0;     // Fixed axial location of parting tool
};

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
 * @brief Atomic Canned Cycle Command (CMD.startCycle).
 */
struct StartCycleCmd {
  std::variant<ThreadingCycleParams,
               TurningCycleParams,
               FacingCycleParams,
               TaperCycleParams,
               CutoffCycleParams> params;
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

