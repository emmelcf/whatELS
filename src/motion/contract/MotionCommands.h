#pragma once
#include <cstdint>
#include "hal/IStepperHAL.h"
#include "kernel/SoftLimit.h"

/**
 * @brief Reason why a motion move came to a halt.
 * @details Conforms to MODE_INTEGRATION_CONTRACT.md Section 2.2.
 */
enum class MotionHaltReason : uint8_t {
  NORMAL_STOP = 0,          // Operator commanded stop, completed deceleration ramp
  SOFT_LIMIT_REACHED,       // Decelerated smoothly and landed exactly on an active soft limit
  TARGET_DISTANCE_REACHED,  // Stroke move reached commanded coordinate
  FEED_HOLD,                // Paused by operator; position latched, sync preserved
  HARD_ENDSTOP_TRIPPED,     // Physical machine switch hit (Emergency hard stop)
  DRIVER_FAULT,             // Stepper driver alarm line asserted
  ESTOP                     // Emergency stop button hit or supervisory abort
};

/**
 * @brief Motion command parameters for Spindle-Synchronous Gearing.
 * @details Supports [KIN-04] coordinated dual-axis taper extension.
 */
struct SpindleSyncParams {
  AxisId  axis                = AXIS_Z;
  int8_t  direction           = 0;      // -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  int32_t pitchDupr           = 0;      // Thread lead in deci-microns/rev (e.g. 20000 = 2.0 mm/rev)
  int32_t maxRapidSpeedDuMin  = 0;      // Maximum rapid velocity clamp during spindle sync
  bool    respectSoftLimits   = true;   // True = halt automatically at nearest enforced limit
  bool    syncIndex           = false;  // True = wait for 1/rev index marker before step initiation
  int32_t targetDistDu        = -1;     // Optional stroke limit (-1 = continuous to soft limit)

  // Coordinated Dual-Axis / Taper Extension [KIN-04]
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;      // Radial delta in du (signed for taper direction)
  int32_t secondaryRatioDen   = 1;      // Axial delta in du (must be > 0)
};

/**
 * @brief Motion command parameters for Continuous / Profiled Velocity Feed.
 * @details Supports [KIN-04] coordinated dual-axis taper extension.
 */
struct VelocityFeedParams {
  AxisId  axis                = AXIS_Z;
  int8_t  direction           = 0;      // -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  int32_t feedRateDuPerMin    = 0;      // Velocity in deci-microns per minute
  float   accelMmSec2         = 0.0f;   // Acceleration ramp rate (0.0 = use axis default)
  bool    respectSoftLimits   = true;   // True = halt automatically at nearest enforced limit
  int32_t targetDistDu        = -1;     // Optional stroke limit (-1 = continuous to soft limit)

  // Coordinated Dual-Axis / Taper Extension [KIN-04]
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;
  int32_t secondaryRatioDen   = 1;
};

/**
 * @brief Motion command parameters for Rapid Positioning & Multi-Pass Clearance.
 */
struct StrokeParams {
  AxisId  axis                = AXIS_Z;
  int32_t targetCoordDu       = 0;      // Absolute workpiece coordinate target in deci-microns
  int32_t rapidSpeedDuMin     = 0;      // Rapid traverse rate
  float   accelMmSec2         = 0.0f;   // Acceleration ramp rate
  bool    respectSoftLimits   = true;   // True = enforce limits during stroke

  // Coordinated Dual-Axis Extension
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryTargetCoordDu = 0;
};

/**
 * @brief Layer 2 Motion Event Listener Contract.
 * @details Conforms to MODE_INTEGRATION_CONTRACT.md Section 3.
 */
class IMotionEventListener {
public:
  virtual ~IMotionEventListener() = default;

  /**
   * @brief Fired when motion on an axis halts for any reason.
   */
  virtual void onMotionHalted(
      AxisId axis,
      MotionHaltReason reason,
      int32_t haltedPosSteps,
      int32_t haltedCoordDu,
      const SoftLimit* triggeredLimit) {}

  /**
   * @brief Fired in threading mode when spindle 1/rev index marker is acquired.
   */
  virtual void onSpindleSyncAcquired(AxisId axis) {}

  /**
   * @brief Fired periodically (50 Hz) to deliver filtered motion telemetry.
   */
  virtual void onMotionTelemetry(
      int32_t zCoordDu,
      int32_t xCoordDu,
      uint32_t currentRpm,
      bool isMoving) {}
};
