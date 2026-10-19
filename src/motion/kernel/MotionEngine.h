#pragma once
#include <cstdint>
#include <atomic>
#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#else
#include <queue>
using UBaseType_t = uint32_t;
using BaseType_t = int32_t;
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#include "AxisState.h"
#include "SoftLimitTable.h"
#include "SpindleSynchronizer.h"
#include "TrajectoryPlanner.h"
#include "hal/IStepperHAL.h"
#include "hal/ISpindleEncoderHAL.h"
#include "contract/TelemetryMailbox.h"
#include "contract/MotionCommandPacket.h"
#include "contract/MotionCommands.h"

enum class MotionMode : uint8_t {
  STOPPED = 0,
  VELOCITY_FEED,
  SPINDLE_SYNC_GEARING,
  MANUAL_JOG
};

struct FeedParameters {
  AxisId  axis                = AXIS_Z;
  int8_t  direction           = 0;     // -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  int32_t speedDuPerMin       = 0;     // Commanded velocity (for async power feed)
  int32_t pitchDupr           = 0;     // Commanded thread lead (for spindle sync gearing)
  int32_t targetDistDu        = -1;    // Optional stroke distance (-1 = continuous to soft limit)

  // Coordinated Dual-Axis / Taper Extension [KIN-04]
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;
  int32_t secondaryRatioDen   = 1;
};

/**
 * @brief Core 1 Real-Time Motion Orchestrator.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 6 and
 *          CONCURRENCY_AND_TIMING.md Section 2 & 4.
 *          Manages dual axes (Z/X), SoftLimitTable lookaheads, SpindleSynchronizer,
 *          TrajectoryPlanner, and TelemetryMailbox streaming.
 */
class MotionEngine {
public:
  static constexpr size_t NUM_AXES = 2;
  static constexpr size_t CMD_QUEUE_DEPTH = 16;

  static MotionEngine& instance();

  MotionEngine(IStepperHAL* stepperHal = nullptr, ISpindleEncoderHAL* encoderHal = nullptr);
  ~MotionEngine();

  // Axis Configuration
  bool configureAxis(AxisId axis, const AxisGeometry& geometry, const StepTimingConfig& timing);

  // Coordinate & Zeroing APIs
  void zeroAxis(AxisId axis);
  void setAxisPositionDu(AxisId axis, int32_t posDu);
  int32_t getAxisPositionDu(AxisId axis) const;
  int32_t getAxisPositionSteps(AxisId axis) const;

  // Soft Limit Table Management
  SoftLimitTable& getSoftLimits(AxisId axis);
  const SoftLimitTable& getSoftLimits(AxisId axis) const;
  bool setSoftLimitDu(AxisId axis, size_t slot, int32_t coordDu, const char* label = nullptr, bool enable = true);

  // Motion Execution APIs
  bool startVelocityFeed(const FeedParameters& params);
  bool startSpindleSync(const FeedParameters& params);
  bool engageVelocityFeed(const VelocityFeedParams& params);
  bool engageSpindleSync(const SpindleSyncParams& params);
  bool startJog(AxisId axis, int8_t direction, int32_t stepDu);
  void stop(AxisId axis, bool emergency = false);
  void stopWithReason(AxisId axis, MotionHaltReason reason);
  void stopAll(bool emergency = false);
  void pauseFeedHold(AxisId axis);
  void resumeFeedHold(AxisId axis);

  // Event Listener Registration
  void setEventListener(IMotionEventListener* listener) { m_listener = listener; }
  IMotionEventListener* getEventListener() const { return m_listener; }

  // Status & Telemetry
  bool isMoving(AxisId axis) const;
  bool isAnyAxisMoving() const;
  MotionMode getActiveMode(AxisId axis) const;
  int32_t getDistanceToNextLimitDu(AxisId axis) const;
  bool isFeedHold(AxisId axis) const;

  // Inter-Core Command Queue (Core 0 -> Core 1)
  bool postCommand(const MotionCommandPacket& packet);

  // Core 1 FreeRTOS Task Management
  bool startTask(UBaseType_t priority = 22, BaseType_t coreId = 1);
  void stopTask();

  // Real-Time Service Hooks (Called from Core 1 ISR / Loop)
  void IRAM_ATTR onMotionTick();
  void IRAM_ATTR onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm);

  // Telemetry Mailbox Access
  const TelemetryMailbox& getTelemetryMailbox() const { return m_telemetryMailbox; }

  // Dependency Injections
  void setStepperHAL(IStepperHAL* stepperHal) { m_stepperHal = stepperHal; }
  void setEncoderHAL(ISpindleEncoderHAL* encoderHal) { m_encoderHal = encoderHal; }
  ISpindleEncoderHAL* getEncoderHAL() { return m_encoderHal; }

  // Simulation Watchdog Touch
  void touchSimHeartbeat();

private:
  static inline size_t axisIdx(AxisId axis) {
    return (axis == AXIS_X) ? 1 : 0;
  }

  void processCommandPacket(const MotionCommandPacket& pkt);
  void publishTelemetrySnapshot();

  AxisState           m_axes[NUM_AXES];
  SoftLimitTable      m_limits[NUM_AXES];
  SpindleSynchronizer m_sync[NUM_AXES];
  TrajectoryPlanner   m_planners[NUM_AXES];

  IStepperHAL*        m_stepperHal;
  ISpindleEncoderHAL* m_encoderHal;
  TelemetryMailbox    m_telemetryMailbox;
  IMotionEventListener* m_listener = nullptr;
  std::atomic<uint32_t> m_lastSimHeartbeatMs{0};

  MotionMode          m_activeMode[NUM_AXES] = { MotionMode::STOPPED, MotionMode::STOPPED };
  bool                m_isMoving[NUM_AXES]   = { false, false };
  bool                m_feedHold[NUM_AXES]   = { false, false };
  int8_t              m_direction[NUM_AXES]  = { 0, 0 };
  int32_t             m_targetStep[NUM_AXES] = { 0, 0 };
  bool                m_hasTarget[NUM_AXES]  = { false, false };
  uint8_t             m_activeLimitSlot[NUM_AXES] = { 0, 0 };
  bool                m_isDualCoordinated = false;

#if defined(ESP32) || defined(ARDUINO)
  QueueHandle_t       m_cmdQueue   = nullptr;
  TaskHandle_t        m_taskHandle = nullptr;
#else
  std::queue<MotionCommandPacket> m_cmdQueue;
#endif

  std::atomic<bool>   m_emergencyStop{false};
  uint32_t            m_lastTelemetryTimeMs = 0;
  uint32_t            m_telemetrySeqNumber  = 0;
  uint32_t            m_lastRpm             = 0;
  int64_t             m_accumulatedSpindlePulses = 0;
};
