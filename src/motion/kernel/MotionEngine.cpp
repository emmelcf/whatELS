#include "MotionEngine.h"
#include <algorithm>

#if defined(ESP32) || defined(ARDUINO)
static void motionCore1TaskEntry(void* arg) {
  auto* engine = static_cast<MotionEngine*>(arg);
  TickType_t lastWakeTime = xTaskGetTickCount();
  while (true) {
    engine->onMotionTick();
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1)); // 1 kHz tick pacing for task loop
  }
}
#endif

MotionEngine& MotionEngine::instance() {
  static MotionEngine s_instance;
  return s_instance;
}

MotionEngine::MotionEngine(IStepperHAL* stepperHal, ISpindleEncoderHAL* encoderHal)
    : m_stepperHal(stepperHal), m_encoderHal(encoderHal) {
  m_axes[0].id = AXIS_Z;
  m_axes[1].id = AXIS_X;

#if defined(ESP32) || defined(ARDUINO)
  m_cmdQueue = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(MotionCommandPacket));
#endif
}

MotionEngine::~MotionEngine() {
  stopTask();
#if defined(ESP32) || defined(ARDUINO)
  if (m_cmdQueue) {
    vQueueDelete(m_cmdQueue);
    m_cmdQueue = nullptr;
  }
#endif
}

bool MotionEngine::configureAxis(AxisId axis, const AxisGeometry& geometry, const StepTimingConfig& timing) {
  size_t idx = axisIdx(axis);
  m_axes[idx].id = axis;
  m_axes[idx].geometry = geometry;
  (void)timing;
  return true;
}

void MotionEngine::zeroAxis(AxisId axis) {
  size_t idx = axisIdx(axis);
  m_axes[idx].originSteps = -m_axes[idx].posSteps;
  m_limits[idx].updateOrigin(m_axes[idx]);
}

void MotionEngine::setAxisPositionDu(AxisId axis, int32_t posDu) {
  size_t idx = axisIdx(axis);
  int32_t steps = duToSteps(m_axes[idx].geometry, posDu);
  m_axes[idx].posSteps = steps - m_axes[idx].originSteps;
  m_limits[idx].updateOrigin(m_axes[idx]);
}

int32_t MotionEngine::getAxisPositionDu(AxisId axis) const {
  size_t idx = axisIdx(axis);
  return getWorkpieceCoordDu(m_axes[idx]);
}

int32_t MotionEngine::getAxisPositionSteps(AxisId axis) const {
  size_t idx = axisIdx(axis);
  return m_axes[idx].posSteps;
}

SoftLimitTable& MotionEngine::getSoftLimits(AxisId axis) {
  return m_limits[axisIdx(axis)];
}

const SoftLimitTable& MotionEngine::getSoftLimits(AxisId axis) const {
  return m_limits[axisIdx(axis)];
}

bool MotionEngine::startVelocityFeed(const FeedParameters& params) {
  size_t idx = axisIdx(params.axis);
  if (params.direction == 0) return false;

  m_activeMode[idx] = MotionMode::VELOCITY_FEED;
  m_direction[idx]  = (params.direction > 0) ? 1 : -1;

  float speedDuSec = static_cast<float>(params.speedDuPerMin) / 60.0f;
  int32_t speedStepsSec = duToSteps(m_axes[idx].geometry, static_cast<int32_t>(speedDuSec));
  if (speedStepsSec < 10) speedStepsSec = 10;

  int32_t accelDuSec2 = static_cast<int32_t>(m_axes[idx].geometry.maxAccelMmSec2 * 10000.0f);
  int32_t accelStepsSec2 = duToSteps(m_axes[idx].geometry, accelDuSec2);
  if (accelStepsSec2 <= 0) accelStepsSec2 = 1000;

  m_planners[idx].configureProfile(50, speedStepsSec, 0, accelStepsSec2);

  if (params.targetDistDu > 0) {
    m_hasTarget[idx]  = true;
    m_targetStep[idx] = m_axes[idx].posSteps + m_direction[idx] * duToSteps(m_axes[idx].geometry, params.targetDistDu);
  } else {
    m_hasTarget[idx]  = false;
    m_targetStep[idx] = 0;
  }

  m_feedHold[idx] = false;
  m_isMoving[idx] = true;

  if (m_stepperHal) {
    m_stepperHal->setEnabled(params.axis, true);
    m_stepperHal->setDirection(params.axis, m_direction[idx]);
  }

  return true;
}

bool MotionEngine::startSpindleSync(const FeedParameters& params) {
  size_t idx = axisIdx(params.axis);
  if (params.direction == 0 || params.pitchDupr == 0) return false;

  m_activeMode[idx] = MotionMode::SPINDLE_SYNC_GEARING;
  m_direction[idx]  = (params.direction > 0) ? 1 : -1;

  m_sync[idx].engage(m_accumulatedSpindlePulses, m_axes[idx].posSteps, params.pitchDupr);

  if (params.targetDistDu > 0) {
    m_hasTarget[idx]  = true;
    m_targetStep[idx] = m_axes[idx].posSteps + m_direction[idx] * duToSteps(m_axes[idx].geometry, params.targetDistDu);
  } else {
    m_hasTarget[idx]  = false;
    m_targetStep[idx] = 0;
  }

  m_feedHold[idx] = false;
  m_isMoving[idx] = true;

  if (m_stepperHal) {
    m_stepperHal->setEnabled(params.axis, true);
    m_stepperHal->setDirection(params.axis, m_direction[idx]);
  }

  return true;
}

bool MotionEngine::startJog(AxisId axis, int8_t direction, int32_t stepDu) {
  size_t idx = axisIdx(axis);
  if (direction == 0 || stepDu <= 0) return false;

  m_activeMode[idx] = MotionMode::MANUAL_JOG;
  m_direction[idx]  = (direction > 0) ? 1 : -1;

  int32_t jogSteps  = duToSteps(m_axes[idx].geometry, stepDu);
  if (jogSteps < 1) jogSteps = 1;

  m_hasTarget[idx]  = true;
  m_targetStep[idx] = m_axes[idx].posSteps + m_direction[idx] * jogSteps;
  m_feedHold[idx]   = false;
  m_isMoving[idx]   = true;

  if (m_stepperHal) {
    m_stepperHal->setEnabled(axis, true);
    m_stepperHal->setDirection(axis, m_direction[idx]);
  }

  return true;
}

bool MotionEngine::engageVelocityFeed(const VelocityFeedParams& params) {
  FeedParameters feed;
  feed.axis                = params.axis;
  feed.direction           = params.direction;
  feed.speedDuPerMin       = params.feedRateDuPerMin;
  feed.targetDistDu        = params.targetDistDu;
  feed.enableSecondaryAxis = params.enableSecondaryAxis;
  feed.secondaryAxis       = params.secondaryAxis;
  feed.secondaryRatioNum   = params.secondaryRatioNum;
  feed.secondaryRatioDen   = params.secondaryRatioDen;
  return startVelocityFeed(feed);
}

bool MotionEngine::engageSpindleSync(const SpindleSyncParams& params) {
  FeedParameters feed;
  feed.axis                = params.axis;
  feed.direction           = params.direction;
  feed.pitchDupr           = params.pitchDupr;
  feed.targetDistDu        = params.targetDistDu;
  feed.enableSecondaryAxis = params.enableSecondaryAxis;
  feed.secondaryAxis       = params.secondaryAxis;
  feed.secondaryRatioNum   = params.secondaryRatioNum;
  feed.secondaryRatioDen   = params.secondaryRatioDen;
  return startSpindleSync(feed);
}

void MotionEngine::stopWithReason(AxisId axis, MotionHaltReason reason) {
  size_t idx = axisIdx(axis);
  int32_t haltedSteps = m_axes[idx].posSteps;
  int32_t haltedDu    = getWorkpieceCoordDu(m_axes[idx]);
  const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, m_direction[idx]);

  stop(axis, reason == MotionHaltReason::ESTOP);

  if (m_listener) {
    m_listener->onMotionHalted(axis, reason, haltedSteps, haltedDu, lim);
  }
}

void MotionEngine::stop(AxisId axis, bool emergency) {
  size_t idx = axisIdx(axis);
  m_activeMode[idx] = MotionMode::STOPPED;
  m_isMoving[idx]   = false;
  m_feedHold[idx]   = false;
  m_hasTarget[idx]  = false;
  m_sync[idx].disengage();
  m_planners[idx].reset();

  if (m_stepperHal) {
    m_stepperHal->stopPulseTrain(axis);
    if (emergency) {
      m_stepperHal->setEnabled(axis, false);
    }
  }
}

void MotionEngine::stopAll(bool emergency) {
  if (emergency) {
    m_emergencyStop.store(true);
  }
  stop(AXIS_Z, emergency);
  stop(AXIS_X, emergency);
}

void MotionEngine::pauseFeedHold(AxisId axis) {
  m_feedHold[axisIdx(axis)] = true;
}

void MotionEngine::resumeFeedHold(AxisId axis) {
  m_feedHold[axisIdx(axis)] = false;
}

bool MotionEngine::isMoving(AxisId axis) const {
  size_t idx = axisIdx(axis);
  if (m_stepperHal && m_stepperHal->isPulseTrainActive(axis)) {
    return true;
  }
  return m_isMoving[idx] && (m_activeMode[idx] != MotionMode::STOPPED);
}

bool MotionEngine::isAnyAxisMoving() const {
  return isMoving(AXIS_Z) || isMoving(AXIS_X);
}

MotionMode MotionEngine::getActiveMode(AxisId axis) const {
  return m_activeMode[axisIdx(axis)];
}

int32_t MotionEngine::getDistanceToNextLimitDu(AxisId axis) const {
  size_t idx = axisIdx(axis);
  const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, m_direction[idx]);
  if (!lim) return -1;
  int32_t distSteps = std::abs(lim->stepPos - m_axes[idx].posSteps);
  return stepsToDu(m_axes[idx].geometry, distSteps);
}

bool MotionEngine::isFeedHold(AxisId axis) const {
  return m_feedHold[axisIdx(axis)];
}

bool MotionEngine::postCommand(const MotionCommandPacket& packet) {
  if (packet.type == MotionCommandPacket::Type::STOP_EMERGENCY) {
    stopAll(true);
    return true;
  }

#if defined(ESP32) || defined(ARDUINO)
  if (m_cmdQueue) {
    return xQueueSend(m_cmdQueue, &packet, 0) == pdPASS;
  }
#endif

  processCommandPacket(packet);
  return true;
}

bool MotionEngine::startTask(UBaseType_t priority, BaseType_t coreId) {
#if defined(ESP32) || defined(ARDUINO)
  if (m_taskHandle != nullptr) return true; // Already running
  BaseType_t res = xTaskCreatePinnedToCore(
      motionCore1TaskEntry,
      "MotionCore1",
      12288,
      this,
      priority,
      &m_taskHandle,
      coreId);
  return res == pdPASS;
#else
  (void)priority;
  (void)coreId;
  return false;
#endif
}

void MotionEngine::stopTask() {
#if defined(ESP32) || defined(ARDUINO)
  if (m_taskHandle != nullptr) {
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }
#endif
}

void MotionEngine::processCommandPacket(const MotionCommandPacket& pkt) {
  switch (pkt.type) {
    case MotionCommandPacket::Type::START_SPINDLE_SYNC: {
      FeedParameters params;
      params.axis                = pkt.axis;
      params.direction           = pkt.direction;
      params.pitchDupr           = pkt.pitchDupr;
      params.targetDistDu        = pkt.targetCoordDu;
      params.enableSecondaryAxis = pkt.enableSecondaryAxis;
      params.secondaryAxis       = pkt.secondaryAxis;
      params.secondaryRatioNum   = pkt.secondaryRatioNum;
      params.secondaryRatioDen   = pkt.secondaryRatioDen;
      startSpindleSync(params);
      break;
    }
    case MotionCommandPacket::Type::START_VELOCITY_FEED: {
      FeedParameters params;
      params.axis                = pkt.axis;
      params.direction           = pkt.direction;
      params.speedDuPerMin       = pkt.speedDuMin;
      params.targetDistDu        = pkt.targetCoordDu;
      startVelocityFeed(params);
      break;
    }
    case MotionCommandPacket::Type::EXECUTE_JOG: {
      startJog(pkt.axis, pkt.direction, pkt.targetCoordDu);
      break;
    }
    case MotionCommandPacket::Type::STOP_NORMAL: {
      stop(pkt.axis, false);
      break;
    }
    case MotionCommandPacket::Type::STOP_EMERGENCY: {
      stopAll(true);
      break;
    }
    case MotionCommandPacket::Type::PAUSE_FEED_HOLD: {
      pauseFeedHold(pkt.axis);
      break;
    }
    case MotionCommandPacket::Type::RESUME_FEED_HOLD: {
      resumeFeedHold(pkt.axis);
      break;
    }
    case MotionCommandPacket::Type::UPDATE_FEED_PITCH: {
      size_t idx = axisIdx(pkt.axis);
      m_sync[idx].updatePitchLive(m_accumulatedSpindlePulses, pkt.pitchDupr);
      break;
    }
    case MotionCommandPacket::Type::ZERO_AXIS: {
      zeroAxis(pkt.axis);
      break;
    }
    default:
      break;
  }
}

void MotionEngine::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
  m_accumulatedSpindlePulses += deltaPulses;
  m_lastRpm = currentRpm;
  m_sync[0].updateSpindlePhase(deltaPulses);
  m_sync[1].updateSpindlePhase(deltaPulses);
}

void MotionEngine::onMotionTick() {
  if (m_emergencyStop.load()) {
    return;
  }

  // 1. Drain incoming command packets from Core 0
#if defined(ESP32) || defined(ARDUINO)
  MotionCommandPacket pkt;
  for (int i = 0; i < 4 && m_cmdQueue; ++i) {
    if (xQueueReceive(m_cmdQueue, &pkt, 0) == pdPASS) {
      processCommandPacket(pkt);
    } else {
      break;
    }
  }
#endif

  // 2. Hardware Spindle Sampling
  if (m_encoderHal) {
    int32_t delta = m_encoderHal->getPulseDelta();
    uint32_t rpm = m_encoderHal->getFilteredRpm();
    onSpindlePulse(delta, rpm);
  }

  // 3. Motion Service for each axis
  for (size_t idx = 0; idx < NUM_AXES; ++idx) {
    if (!m_isMoving[idx] || m_feedHold[idx] || m_activeMode[idx] == MotionMode::STOPPED) {
      continue;
    }

    AxisId axis = static_cast<AxisId>(idx);
    int8_t dir = m_direction[idx];

    // Evaluate soft limits
    const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir);
    int32_t remainingSteps = INT32_MAX;
    if (lim) {
      remainingSteps = std::abs(lim->stepPos - m_axes[idx].posSteps);
      m_activeLimitSlot[idx] = lim->id;
    } else {
      m_activeLimitSlot[idx] = 0;
    }

    // Evaluate stroke target if set
    if (m_hasTarget[idx]) {
      int32_t distToTarget = std::abs(m_targetStep[idx] - m_axes[idx].posSteps);
      remainingSteps = std::min(remainingSteps, distToTarget);
    }

    if (remainingSteps <= 0) {
      // Limit or target reached!
      MotionHaltReason reason = (lim && std::abs(lim->stepPos - m_axes[idx].posSteps) <= 0)
                                    ? MotionHaltReason::SOFT_LIMIT_REACHED
                                    : MotionHaltReason::TARGET_DISTANCE_REACHED;
      stopWithReason(axis, reason);
      continue;
    }

    switch (m_activeMode[idx]) {
      case MotionMode::SPINDLE_SYNC_GEARING: {
        int32_t effectivePpr = 4096; // Standard 1024 PPR 4x quadrature
        int32_t idealStep = m_sync[idx].computeIdealSyncSteps(
            m_accumulatedSpindlePulses,
            m_axes[idx].geometry.motorStepsPerRev,
            m_axes[idx].geometry.screwPitchDu,
            effectivePpr);

        // Clamp ideal step to limit boundary
        if (lim) {
          if (dir < 0 && idealStep < lim->stepPos) idealStep = lim->stepPos;
          if (dir > 0 && idealStep > lim->stepPos) idealStep = lim->stepPos;
        }

        int32_t stepDiff = idealStep - m_axes[idx].posSteps;
        if (stepDiff != 0) {
          int8_t stepDir = (stepDiff > 0) ? 1 : -1;
          if (m_stepperHal) {
            m_stepperHal->setDirection(axis, stepDir);
            m_stepperHal->pulseStep(axis);
          }
          m_axes[idx].posSteps += stepDir;
        }
        break;
      }

      case MotionMode::VELOCITY_FEED: {
        float dt = 0.001f;
        int32_t cmdVel = m_planners[idx].updateVelocity(remainingSteps, dt);
        if (m_planners[idx].isFinished() || cmdVel <= 0) {
          stopWithReason(axis, m_hasTarget[idx] ? MotionHaltReason::TARGET_DISTANCE_REACHED : MotionHaltReason::NORMAL_STOP);
        } else if (m_stepperHal) {
          m_stepperHal->setDirection(axis, dir);
          m_stepperHal->pulseStep(axis);
          m_axes[idx].posSteps += dir;
        }
        break;
      }

      case MotionMode::MANUAL_JOG: {
        if (m_stepperHal) {
          m_stepperHal->setDirection(axis, dir);
          m_stepperHal->pulseStep(axis);
        }
        m_axes[idx].posSteps += dir;
        if (m_axes[idx].posSteps == m_targetStep[idx]) {
          stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
        }
        break;
      }

      default:
        break;
    }
  }

  // 4. Telemetry publication (50 Hz / 20 ms cadence)
  publishTelemetrySnapshot();
}

void MotionEngine::publishTelemetrySnapshot() {
#if defined(ESP32) || defined(ARDUINO)
  uint32_t now = millis();
  if (now - m_lastTelemetryTimeMs < 20) {
    return;
  }
  m_lastTelemetryTimeMs = now;
#endif

  MotionTelemetrySnapshot snapshot;
  snapshot.zPosSteps       = m_axes[0].posSteps;
  snapshot.zCoordDu        = getWorkpieceCoordDu(m_axes[0]);
  snapshot.xPosSteps       = m_axes[1].posSteps;
  snapshot.xCoordDu        = getWorkpieceCoordDu(m_axes[1]);
  snapshot.spindleRpm      = m_lastRpm;
  snapshot.spindlePulses   = m_accumulatedSpindlePulses;
  snapshot.motionMode      = static_cast<uint16_t>(m_activeMode[0]);
  snapshot.isMoving        = isAnyAxisMoving();
  snapshot.isFeedHold      = m_feedHold[0] || m_feedHold[1];
  snapshot.activeLimitSlot = m_activeLimitSlot[0] ? m_activeLimitSlot[0] : m_activeLimitSlot[1];
  snapshot.distToLimitDu   = getDistanceToNextLimitDu(AXIS_Z);
  snapshot.sequenceNumber  = ++m_telemetrySeqNumber;

  m_telemetryMailbox.publish(snapshot);

  if (m_listener) {
    m_listener->onMotionTelemetry(snapshot.zCoordDu, snapshot.xCoordDu, snapshot.spindleRpm, snapshot.isMoving);
  }
}
