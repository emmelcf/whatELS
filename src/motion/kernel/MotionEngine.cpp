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

bool MotionEngine::setSoftLimitDu(AxisId axis, size_t slot, int32_t coordDu, const char* label, bool enable) {
  size_t idx = axisIdx(axis);
  int32_t stepPos = duToSteps(m_axes[idx].geometry, coordDu) - m_axes[idx].originSteps;
  return m_limits[idx].setLimit(slot, coordDu, stepPos, label, enable);
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

  m_stepFraction[idx] = 0;
  if (params.enableSecondaryAxis) {
    size_t sIdx = axisIdx(params.secondaryAxis);
    int32_t den = (params.secondaryRatioDen > 0) ? params.secondaryRatioDen : 1;
    int32_t secSpeedDu = static_cast<int32_t>((static_cast<int64_t>(params.speedDuPerMin) * std::abs(params.secondaryRatioNum)) / den);
    int8_t secDir = (params.secondaryRatioNum >= 0) ? 1 : -1;

    m_activeMode[sIdx] = MotionMode::VELOCITY_FEED;
    m_direction[sIdx]  = secDir;
    m_stepFraction[sIdx] = 0;

    float secSpeedDuSec = static_cast<float>(secSpeedDu) / 60.0f;
    int32_t secSpeedStepsSec = duToSteps(m_axes[sIdx].geometry, static_cast<int32_t>(secSpeedDuSec));
    if (secSpeedStepsSec < 10) secSpeedStepsSec = 10;

    int32_t secAccelDuSec2 = static_cast<int32_t>(m_axes[sIdx].geometry.maxAccelMmSec2 * 10000.0f);
    int32_t secAccelStepsSec2 = duToSteps(m_axes[sIdx].geometry, secAccelDuSec2);
    if (secAccelStepsSec2 <= 0) secAccelStepsSec2 = 1000;

    m_planners[sIdx].configureProfile(50, secSpeedStepsSec, 0, secAccelStepsSec2);

    if (params.targetDistDu > 0) {
      int32_t secDistDu = static_cast<int32_t>((static_cast<int64_t>(params.targetDistDu) * std::abs(params.secondaryRatioNum)) / den);
      m_hasTarget[sIdx]  = true;
      m_targetStep[sIdx] = m_axes[sIdx].posSteps + secDir * duToSteps(m_axes[sIdx].geometry, secDistDu);
    } else {
      m_hasTarget[sIdx]  = false;
      m_targetStep[sIdx] = 0;
    }

    m_feedHold[sIdx] = false;
    m_isMoving[sIdx] = true;

    if (m_stepperHal) {
      m_stepperHal->setEnabled(params.secondaryAxis, true);
      m_stepperHal->setDirection(params.secondaryAxis, secDir);
    }
  }

  m_isDualCoordinated = params.enableSecondaryAxis;
  return true;
}

bool MotionEngine::startSpindleSync(const FeedParameters& params) {
  size_t idx = axisIdx(params.axis);
  if (params.direction == 0 || params.pitchDupr == 0) return false;

  m_activeMode[idx] = MotionMode::SPINDLE_SYNC_GEARING;
  m_direction[idx]  = (params.direction > 0) ? 1 : -1;

  int32_t signedPitch = m_direction[idx] * std::abs(params.pitchDupr);
  m_sync[idx].engage(m_accumulatedSpindlePulses, m_axes[idx].posSteps, signedPitch);

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

  if (params.enableSecondaryAxis) {
    size_t sIdx = axisIdx(params.secondaryAxis);
    int32_t den = (params.secondaryRatioDen > 0) ? params.secondaryRatioDen : 1;
    int32_t secPitch = static_cast<int32_t>((static_cast<int64_t>(params.pitchDupr) * std::abs(params.secondaryRatioNum)) / den);
    if (secPitch > 0) {
      int8_t secDir = (params.secondaryRatioNum >= 0) ? 1 : -1;
      m_activeMode[sIdx] = MotionMode::SPINDLE_SYNC_GEARING;
      m_direction[sIdx]  = secDir;
      m_sync[sIdx].engage(m_accumulatedSpindlePulses, m_axes[sIdx].posSteps, secDir * secPitch);
      m_feedHold[sIdx] = false;
      m_isMoving[sIdx] = true;
      m_hasTarget[sIdx] = false;
      if (m_stepperHal) {
        m_stepperHal->setEnabled(params.secondaryAxis, true);
        m_stepperHal->setDirection(params.secondaryAxis, secDir);
      }
    }
  }

  m_isDualCoordinated = params.enableSecondaryAxis;
  return true;
}

bool MotionEngine::startJog(AxisId axis, int8_t direction, int32_t stepDu) {
  size_t idx = axisIdx(axis);
  if (direction == 0) return false;
  if (m_isMoving[idx]) return false; // Kinematic state invariant: axis is currently moving

  m_activeMode[idx] = MotionMode::MANUAL_JOG;
  m_direction[idx]  = (direction > 0) ? 1 : -1;

  if (stepDu > 0) {
    int32_t jogSteps  = duToSteps(m_axes[idx].geometry, stepDu);
    if (jogSteps < 1) jogSteps = 1;
    m_hasTarget[idx]  = true;
    m_targetStep[idx] = m_axes[idx].posSteps + m_direction[idx] * jogSteps;
  } else {
    m_hasTarget[idx]  = false;
  }

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

  if (m_isDualCoordinated) {
    m_isDualCoordinated = false;
    stop(AXIS_Z, reason == MotionHaltReason::ESTOP);
    stop(AXIS_X, reason == MotionHaltReason::ESTOP);
  } else {
    stop(axis, reason == MotionHaltReason::ESTOP);
  }

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

  if (!m_isMoving[0] && !m_isMoving[1]) {
    m_isDualCoordinated = false;
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
  processCommandPacket(packet);
  return true;
#else
  if (m_cmdQueue.size() < CMD_QUEUE_DEPTH) {
    m_cmdQueue.push(packet);
    return true;
  }
  return false;
#endif
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
      params.enableSecondaryAxis = pkt.enableSecondaryAxis;
      params.secondaryAxis       = pkt.secondaryAxis;
      params.secondaryRatioNum   = pkt.secondaryRatioNum;
      params.secondaryRatioDen   = pkt.secondaryRatioDen;
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
      int32_t signedPitch = m_direction[idx] * std::abs(pkt.pitchDupr);
      m_sync[idx].updatePitchLive(m_accumulatedSpindlePulses, signedPitch);
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
#else
  for (int i = 0; i < 4 && !m_cmdQueue.empty(); ++i) {
    MotionCommandPacket pkt = m_cmdQueue.front();
    m_cmdQueue.pop();
    processCommandPacket(pkt);
  }
#endif

  // 2. Hardware Spindle Sampling
  if (m_encoderHal) {
    if (m_encoderHal->isSimulationActive()) {
#if defined(ESP_PLATFORM)
      uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
      uint32_t lastHb = m_lastSimHeartbeatMs.load(std::memory_order_relaxed);
      if (lastHb > 0 && (nowMs - lastHb > 1500)) {
        m_encoderHal->setSimulatedRpm(0.0f);
        m_encoderHal->setSimulationActive(false);
        stopWithReason(AXIS_Z, MotionHaltReason::ESTOP);
        stopWithReason(AXIS_X, MotionHaltReason::ESTOP);
      }
#endif
    }

    int32_t delta = m_encoderHal->getPulseDelta();
    uint32_t rpm = m_encoderHal->getFilteredRpm();
    onSpindlePulse(delta, rpm);
  }

  // 3. Motion Service for each axis
  int64_t effectivePulses = m_accumulatedSpindlePulses;

  if (m_isDualCoordinated && m_isMoving[0] && m_isMoving[1]) {
    for (size_t a = 0; a < NUM_AXES; ++a) {
      if (!m_sync[a].isEngaged()) continue;
      int8_t aDir = m_direction[a];
      int32_t boundStep = 0;
      bool hasBound = false;

      const SoftLimit* aLim = m_limits[a].findNextEnforcedLimit(m_axes[a].posSteps, aDir);
      if (aLim) {
        boundStep = aLim->stepPos;
        hasBound = true;
      }
      if (m_hasTarget[a]) {
        if (!hasBound || (m_targetStep[a] - boundStep) * aDir < 0) {
          boundStep = m_targetStep[a];
          hasBound = true;
        }
      }

      if (hasBound) {
        int32_t effPpr = (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0)
                             ? m_encoderHal->getCountsPerSpindleRev()
                             : 4096;
        int64_t limitPulse = m_sync[a].calculatePulsesForStep(
            boundStep,
            m_axes[a].geometry.motorStepsPerRev,
            m_axes[a].geometry.screwPitchDu,
            effPpr);
        if (limitPulse > m_sync[a].getAnchorPulses()) {
          effectivePulses = std::min(effectivePulses, limitPulse);
        } else if (limitPulse < m_sync[a].getAnchorPulses()) {
          effectivePulses = std::max(effectivePulses, limitPulse);
        }
      }
    }
  }

  for (size_t idx = 0; idx < NUM_AXES; ++idx) {
    if (!m_isMoving[idx] || m_feedHold[idx] || m_activeMode[idx] == MotionMode::STOPPED) {
      continue;
    }

    AxisId axis = static_cast<AxisId>(idx);
    int8_t dir = m_direction[idx];

    // Evaluate soft limits
    const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir);
    int32_t remainingSteps = INT32_MAX;
    bool limitReached = false;
    if (lim) {
      int32_t distToLim = (lim->stepPos - m_axes[idx].posSteps) * dir;
      if (distToLim <= 0) {
        limitReached = true;
        distToLim = 0;
      }
      remainingSteps = distToLim;
      m_activeLimitSlot[idx] = lim->id;
    } else {
      m_activeLimitSlot[idx] = 0;
    }

    // Evaluate stroke target if set
    bool targetReached = false;
    if (m_hasTarget[idx]) {
      int32_t distToTarget = (m_targetStep[idx] - m_axes[idx].posSteps) * dir;
      if (distToTarget <= 0) {
        targetReached = true;
        distToTarget = 0;
      }
      remainingSteps = std::min(remainingSteps, distToTarget);
    }

    if (limitReached || targetReached) {
      MotionHaltReason reason = limitReached ? MotionHaltReason::SOFT_LIMIT_REACHED
                                             : MotionHaltReason::TARGET_DISTANCE_REACHED;
      stopWithReason(axis, reason);
      continue;
    }

    switch (m_activeMode[idx]) {
      case MotionMode::SPINDLE_SYNC_GEARING: {
        int32_t effectivePpr = (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0)
                                   ? m_encoderHal->getCountsPerSpindleRev()
                                   : 4096;
        int32_t idealStep = m_sync[idx].computeIdealSyncSteps(
            effectivePulses,
            m_axes[idx].geometry.motorStepsPerRev,
            m_axes[idx].geometry.screwPitchDu,
            effectivePpr);

        // Clamp ideal step to limit boundary
        if (lim) {
          if (dir < 0 && idealStep < lim->stepPos) idealStep = lim->stepPos;
          if (dir > 0 && idealStep > lim->stepPos) idealStep = lim->stepPos;
        }

        // Clamp ideal step to stroke target
        if (m_hasTarget[idx]) {
          if (dir < 0 && idealStep < m_targetStep[idx]) idealStep = m_targetStep[idx];
          if (dir > 0 && idealStep > m_targetStep[idx]) idealStep = m_targetStep[idx];
        }

        int32_t stepDiff = idealStep - m_axes[idx].posSteps;
        while (stepDiff != 0) {
          int8_t stepDir = (stepDiff > 0) ? 1 : -1;
          if (m_stepperHal) {
            m_stepperHal->setDirection(axis, stepDir);
            m_stepperHal->pulseStep(axis);
          }
          m_axes[idx].posSteps += stepDir;
          stepDiff = idealStep - m_axes[idx].posSteps;
        }
        break;
      }

      case MotionMode::VELOCITY_FEED: {
        float dt = 0.001f;
        int32_t cmdVel = m_planners[idx].updateVelocity(remainingSteps, dt);
        if (m_planners[idx].isFinished() || cmdVel <= 0) {
          if (m_hasTarget[idx] && m_axes[idx].posSteps != m_targetStep[idx]) {
            int8_t stepDir = (m_targetStep[idx] > m_axes[idx].posSteps) ? 1 : -1;
            if (m_stepperHal) {
              m_stepperHal->setDirection(axis, stepDir);
              m_stepperHal->pulseStep(axis);
            }
            m_axes[idx].posSteps += stepDir;
            if (m_axes[idx].posSteps == m_targetStep[idx]) {
              stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
            }
          } else {
            stopWithReason(axis, m_hasTarget[idx] ? MotionHaltReason::TARGET_DISTANCE_REACHED : MotionHaltReason::NORMAL_STOP);
          }
        } else if (m_stepperHal) {
          m_stepFraction[idx] += cmdVel;
          while (m_stepFraction[idx] >= 1000) {
            m_stepFraction[idx] -= 1000;
            m_stepperHal->setDirection(axis, dir);
            m_stepperHal->pulseStep(axis);
            m_axes[idx].posSteps += dir;
            if (m_hasTarget[idx] && m_axes[idx].posSteps == m_targetStep[idx]) {
              stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
              break;
            }
          }
        }
        break;
      }

      case MotionMode::MANUAL_JOG: {
        if (m_stepperHal) {
          m_stepperHal->setDirection(axis, dir);
          m_stepperHal->pulseStep(axis);
        }
        m_axes[idx].posSteps += dir;
        if (m_hasTarget[idx] && m_axes[idx].posSteps == m_targetStep[idx]) {
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

void MotionEngine::touchSimHeartbeat() {
#if defined(ESP_PLATFORM)
  m_lastSimHeartbeatMs.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_relaxed);
#else
  m_lastSimHeartbeatMs.store(1, std::memory_order_relaxed);
#endif
}

