#pragma once
#include <cstdint>
#include "hal/IStepperHAL.h"

namespace whatels::core {

static constexpr AxisId AXIS_INVALID = static_cast<AxisId>(0xFF);

/**
 * @brief Atomic Feed Motion Command (CMD.startFeed).
 */
struct StartFeedCmd {
  AxisId  axis         = AXIS_INVALID;  // Mandatory: AXIS_Z or AXIS_X
  int8_t  direction    = 0;             // Mandatory: -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  bool    syncSpindle  = true;          // Physical fallback: true = ELS gearing (dupr), false = async IPM (du/min)
  int32_t feedRate     = 0;             // Mandatory: Magnitude of feed rate (> 0)
  int32_t targetDistDu = -1;            // Physical fallback: Travel stroke in deci-microns (-1 = continuous to limit)

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    if (direction != -1 && direction != 1) {
      if (errReason) *errReason = "INVALID_PARAM:dir";
      return false;
    }
    if (feedRate <= 0) {
      if (errReason) *errReason = "INVALID_PARAM:feed";
      return false;
    }
    if (targetDistDu < -1 || targetDistDu == 0) {
      if (errReason) *errReason = "INVALID_PARAM:dist";
      return false;
    }
    return true;
  }
};

/**
 * @brief Incremental Single-Step Manual Jog Command (CMD.jogStep).
 */
struct JogStepCmd {
  AxisId  axis   = AXIS_INVALID;        // Mandatory: AXIS_Z or AXIS_X
  int32_t distDu = 0;                   // Mandatory: Signed distance in deci-microns (!= 0)

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    if (distDu == 0) {
      if (errReason) *errReason = "INVALID_PARAM:dist";
      return false;
    }
    return true;
  }
};

/**
 * @brief Continuous Manual Jog Command (CMD.jogHold).
 */
struct JogHoldCmd {
  AxisId axis      = AXIS_INVALID;      // Mandatory: AXIS_Z or AXIS_X
  int8_t direction = 0;                 // Mandatory: -1 or +1

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    if (direction != -1 && direction != 1) {
      if (errReason) *errReason = "INVALID_PARAM:dir";
      return false;
    }
    return true;
  }
};

/**
 * @brief Stop Continuous Manual Jog Command (CMD.jogRelease).
 */
struct JogReleaseCmd {
  bool   hasAxis = false;               // If false, stops all axes
  AxisId axis    = AXIS_INVALID;

  bool validate(const char** errReason = nullptr) const {
    if (hasAxis && axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    return true;
  }
};

/**
 * @brief Zero Axis Origin Command (CMD.axisZero).
 */
struct AxisZeroCmd {
  AxisId axis = AXIS_INVALID;           // Mandatory: AXIS_Z or AXIS_X

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    return true;
  }
};

/**
 * @brief Set Axis Coordinate Preset Command (CMD.axisPosDu).
 */
struct AxisPosCmd {
  AxisId  axis  = AXIS_INVALID;         // Mandatory: AXIS_Z or AXIS_X
  int32_t posDu = 0;                    // Mandatory: Target coordinate in deci-microns

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    return true;
  }
};

/**
 * @brief Set Axis Travel Limit Command (CMD.axisLimitDu).
 */
struct AxisLimitCmd {
  AxisId  axis       = AXIS_INVALID;    // Mandatory: AXIS_Z or AXIS_X
  int8_t  limitBound = 0;               // Mandatory: 1 = Lower / Chuck, 2 = Upper / Tailstock
  int32_t coordDu    = 0;               // Mandatory: Coordinate in deci-microns

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    if (limitBound != 1 && limitBound != 2) {
      if (errReason) *errReason = "INVALID_PARAM:bound";
      return false;
    }
    return true;
  }
};

/**
 * @brief Clear Axis Travel Limit Command (CMD.axisClearLimit).
 */
struct AxisClearLimitCmd {
  AxisId axis       = AXIS_INVALID;     // Mandatory: AXIS_Z or AXIS_X
  int8_t limitBound = 0;                // Mandatory: 1 = Lower / Chuck, 2 = Upper / Tailstock

  bool validate(const char** errReason = nullptr) const {
    if (axis != AXIS_Z && axis != AXIS_X) {
      if (errReason) *errReason = "INVALID_PARAM:axis";
      return false;
    }
    if (limitBound != 1 && limitBound != 2) {
      if (errReason) *errReason = "INVALID_PARAM:bound";
      return false;
    }
    return true;
  }
};

/**
 * @brief Simulated Spindle RPM Override Command (CMD.simRpm).
 */
struct SimRpmCmd {
  float rpm = -1.0f;

  bool validate(const char** errReason = nullptr) const {
    if (rpm < 0.0f) {
      if (errReason) *errReason = "INVALID_PARAM:rpm";
      return false;
    }
    return true;
  }
};

/**
 * @brief Simulated Spindle Encoder Enable/Disable Command (CMD.simEncoder).
 */
struct SimEncoderCmd {
  bool hasActive = false;
  bool active    = false;

  bool validate(const char** errReason = nullptr) const {
    if (!hasActive) {
      if (errReason) *errReason = "INVALID_PARAM:active";
      return false;
    }
    return true;
  }
};

/**
 * @brief Simulated Spindle Pulse Injection Command (CMD.simPulses).
 */
struct SimPulsesCmd {
  int32_t pulses = 0;

  bool validate(const char** errReason = nullptr) const {
    if (pulses == 0) {
      if (errReason) *errReason = "INVALID_PARAM:pulses";
      return false;
    }
    return true;
  }
};

/**
 * @brief In-Cut Radial Retract Feed Hold Command (CMD.hold).
 */
struct HoldCmd {
  bool validate(const char** = nullptr) const { return true; }
};

/**
 * @brief Resume Feed from Hold Command (CMD.resume).
 */
struct ResumeCmd {
  bool validate(const char** = nullptr) const { return true; }
};

/**
 * @brief Advance Next Pass in Step Mode Command (CMD.step).
 */
struct StepCmd {
  bool validate(const char** = nullptr) const { return true; }
};

/**
 * @brief Universal Emergency Stop Command (CMD.stop).
 */
struct StopCmd {
  bool validate(const char** = nullptr) const { return true; }
};

/**
 * @brief Line Flush and Heartbeat Ping Command (CMD.ping).
 */
struct PingCmd {
  bool validate(const char** = nullptr) const { return true; }
};

} // namespace whatels::core

