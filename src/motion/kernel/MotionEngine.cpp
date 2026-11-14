#include "MotionEngine.h"
#include "core/SettingsService.h"
#include <algorithm>
#include <cmath>

#if defined(ESP32) || defined(ARDUINO)
#define MOTION_LOCK()   taskENTER_CRITICAL(&m_spinlock)
#define MOTION_UNLOCK() taskEXIT_CRITICAL(&m_spinlock)
#else
#define MOTION_LOCK()   ((void)0)
#define MOTION_UNLOCK() ((void)0)
#endif

#if defined(ESP32) || defined(ARDUINO)
bool IRAM_ATTR MotionEngine::onSyncTimerAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
  (void)timer;
  (void)edata;
  auto* engine = static_cast<MotionEngine*>(user_ctx);
  BaseType_t hpTaskWoken = pdFALSE;
  if (engine && engine->m_syncTaskHandle) {
    vTaskNotifyGiveFromISR(engine->m_syncTaskHandle, &hpTaskWoken);
  }
  return hpTaskWoken == pdTRUE;
}

void MotionEngine::motionSyncTaskEntry(void* arg) {
  auto* engine = static_cast<MotionEngine*>(arg);

  gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000, // 1 MHz (1 us per tick)
  };
  if (gptimer_new_timer(&timer_config, &engine->m_syncTimer) == ESP_OK) {
    gptimer_event_callbacks_t cbs = {
      .on_alarm = MotionEngine::onSyncTimerAlarm,
    };
    gptimer_register_event_callbacks(engine->m_syncTimer, &cbs, engine);
    gptimer_enable(engine->m_syncTimer);
    gptimer_alarm_config_t alarm_config = {
      .alarm_count = SYNC_TIMER_PERIOD_US, // 250 us = 4 kHz
      .reload_count = 0,
      .flags = { .auto_reload_on_alarm = true },
    };
    gptimer_set_alarm_action(engine->m_syncTimer, &alarm_config);
    gptimer_start(engine->m_syncTimer);
  }

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    engine->onSyncTick();
  }
}

void MotionEngine::motionCore1TaskEntry(void* arg) {
  auto* engine = static_cast<MotionEngine*>(arg);

#ifdef ESP32
  esp_task_wdt_config_t twdtConfig = {
    .timeout_ms = 50,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  if (esp_task_wdt_reconfigure(&twdtConfig) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_init(&twdtConfig);
  }
  esp_task_wdt_add(nullptr);
#endif

  TickType_t lastWakeTime = xTaskGetTickCount();
  while (true) {
#ifdef ESP32
    esp_task_wdt_reset();
#endif
    engine->onMotionTick();
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1)); // 1 kHz supervisory & telemetry loop
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

uint32_t IRAM_ATTR MotionEngine::getNowMs() const {
#if defined(ESP32) || defined(ARDUINO)
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
  return 0;
#endif
}

bool MotionEngine::configureAxis(AxisId axis, const AxisGeometry& geometry, const StepTimingConfig& timing) {
  size_t idx = axisIdx(axis);
  m_axes[idx].id = axis;
  m_axes[idx].geometry = geometry;
  m_stepTiming[idx] = timing;

  if (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0) {
    m_cachedCountsPerRev = m_encoderHal->getCountsPerSpindleRev();
  } else {
    int32_t profileCounts = static_cast<int32_t>(std::round(SettingsService::getActiveProfile().spindle.countsPerRev()));
    if (profileCounts > 0) {
      m_cachedCountsPerRev = profileCounts;
    }
  }
  return true;
}

void IRAM_ATTR MotionEngine::zeroAxis(AxisId axis) {
  size_t idx = axisIdx(axis);
  MOTION_LOCK();
  m_axes[idx].originSteps = -m_axes[idx].posSteps;
  m_limits[idx].updateOrigin(m_axes[idx]);
  MOTION_UNLOCK();
}

void IRAM_ATTR MotionEngine::setAxisPositionDu(AxisId axis, int32_t posDu) {
  size_t idx = axisIdx(axis);
  MOTION_LOCK();
  int32_t steps = duToSteps(m_axes[idx].geometry, posDu);
  m_axes[idx].posSteps = steps - m_axes[idx].originSteps;
  m_limits[idx].updateOrigin(m_axes[idx]);
  MOTION_UNLOCK();
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

bool IRAM_ATTR MotionEngine::startVelocityFeed(const FeedParameters& params) {
  size_t idx = axisIdx(params.axis);
  if (params.direction == 0) return false;

  int8_t dir = (params.direction > 0) ? 1 : -1;
  if (params.respectSoftLimits) {
    const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir);
    if (lim && (lim->stepPos - m_axes[idx].posSteps) * dir <= 0) {
      return false;
    }
  }

  m_activeMode[idx]        = MotionMode::VELOCITY_FEED;
  m_direction[idx]         = dir;
  m_respectSoftLimits[idx] = params.respectSoftLimits;

  float speedDuSec = static_cast<float>(params.speedDuPerMin) / 60.0f;
  int32_t speedStepsSec = duToSteps(m_axes[idx].geometry, static_cast<int32_t>(speedDuSec));
  if (speedStepsSec < 10) speedStepsSec = 10;

  float accelMmSec2 = (params.accelMmSec2 > 0.0f) ? params.accelMmSec2 : m_axes[idx].geometry.maxAccelMmSec2;
  int32_t accelDuSec2 = static_cast<int32_t>(accelMmSec2 * 10000.0f);
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

  m_feedHold[idx]     = false;
  m_isMoving[idx]     = true;
  m_stepFraction[idx] = 0;

  if (m_stepperHal) {
    m_stepperHal->startPulseTrain(params.axis, static_cast<uint32_t>(m_planners[idx].getCurrentVelocity()), m_direction[idx]);
  }

  if (params.enableSecondaryAxis) {
    size_t sIdx = axisIdx(params.secondaryAxis);
    int32_t den = (params.secondaryRatioDen > 0) ? params.secondaryRatioDen : 1;
    int32_t secSpeedDu = static_cast<int32_t>((static_cast<int64_t>(params.speedDuPerMin) * std::abs(params.secondaryRatioNum)) / den);
    int8_t secDir = (params.secondaryRatioNum >= 0) ? 1 : -1;

    m_activeMode[sIdx]        = MotionMode::VELOCITY_FEED;
    m_direction[sIdx]         = secDir;
    m_respectSoftLimits[sIdx] = params.respectSoftLimits;
    m_stepFraction[sIdx]      = 0;

    float secSpeedDuSec = static_cast<float>(secSpeedDu) / 60.0f;
    int32_t secSpeedStepsSec = duToSteps(m_axes[sIdx].geometry, static_cast<int32_t>(secSpeedDuSec));
    if (secSpeedStepsSec < 10) secSpeedStepsSec = 10;

    // Scale secondary acceleration proportionally with taper ratio so both axes ramp synchronously
    int32_t secAccelDuSec2 = static_cast<int32_t>((static_cast<int64_t>(accelDuSec2) * std::abs(params.secondaryRatioNum)) / den);
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
      m_stepperHal->startPulseTrain(params.secondaryAxis, static_cast<uint32_t>(m_planners[sIdx].getCurrentVelocity()), secDir);
    }
  }

  m_masterAxisIdx     = idx;
  m_isDualCoordinated = params.enableSecondaryAxis;
  return true;
}

bool IRAM_ATTR MotionEngine::startSpindleSync(const FeedParameters& params) {
  size_t idx = axisIdx(params.axis);
  if (params.direction == 0 || params.pitchDupr == 0) return false;

  int8_t dir = (params.direction > 0) ? 1 : -1;

  // Verify pre-cut runway clearance against active soft limits / target distance
  int32_t remainingDu = -1;
  if (params.respectSoftLimits) {
    const SoftLimit* lim = m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir);
    if (lim) {
      int32_t distSteps = (lim->stepPos - m_axes[idx].posSteps) * dir;
      if (distSteps <= 0) return false;
      remainingDu = stepsToDu(m_axes[idx].geometry, distSteps);
    }
  }
  if (params.targetDistDu > 0) {
    remainingDu = (remainingDu >= 0) ? std::min(remainingDu, params.targetDistDu) : params.targetDistDu;
  }
  if (remainingDu >= 0 && m_lastRpm > 0) {
    if (!TrajectoryPlanner::verifyRunwaySpindle(m_axes[idx].geometry, remainingDu, m_lastRpm, params.pitchDupr)) {
      return false;
    }
  }

  MOTION_LOCK();
  m_activeMode[idx]        = MotionMode::SPINDLE_SYNC_GEARING;
  m_direction[idx]         = dir;
  m_respectSoftLimits[idx] = params.respectSoftLimits;
  m_waitingForIndex[idx]   = params.syncIndex;

  int64_t startPulses = m_accumulatedSpindlePulses;
  int32_t effPpr = (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0)
                       ? m_encoderHal->getCountsPerSpindleRev()
                       : m_cachedCountsPerRev;
  if (params.syncIndex && m_encoderHal) {
    m_encoderHal->resetIndexSync();
  }
  if ((params.syncIndex || m_lastRpm >= 5.0f) && effPpr > 0) {
    int64_t rem = startPulses % effPpr;
    if (rem < 0) rem += effPpr;
    if (rem > 0) {
      startPulses += (effPpr - rem);
    } else if (params.syncIndex) {
      startPulses += effPpr;
    }
  }

  int32_t signedPitch = m_direction[idx] * std::abs(params.pitchDupr);
  m_sync[idx].engage(startPulses, m_axes[idx].posSteps, signedPitch);

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
      m_activeMode[sIdx]        = MotionMode::SPINDLE_SYNC_GEARING;
      m_direction[sIdx]         = secDir;
      m_respectSoftLimits[sIdx] = params.respectSoftLimits;
      m_waitingForIndex[sIdx]   = params.syncIndex;
      m_sync[sIdx].engage(startPulses, m_axes[sIdx].posSteps, secDir * secPitch);
      m_feedHold[sIdx]  = false;
      m_isMoving[sIdx]  = true;
      m_hasTarget[sIdx] = false;
      if (m_stepperHal) {
        m_stepperHal->setEnabled(params.secondaryAxis, true);
        m_stepperHal->setDirection(params.secondaryAxis, secDir);
      }
    }
  }

  m_masterAxisIdx     = idx;
  m_isDualCoordinated = params.enableSecondaryAxis;
  MOTION_UNLOCK();
  return true;
}

void IRAM_ATTR MotionEngine::renewJogLease(AxisId axis) {
  size_t idx = axisIdx(axis);
  m_lastJogLeaseMs[idx]    = getNowMs();
  m_jogLeaseElapsedMs[idx] = 0;
}

bool IRAM_ATTR MotionEngine::startJog(AxisId axis, int8_t direction, int32_t stepDu) {
  size_t idx = axisIdx(axis);
  if (direction == 0) return false;
  int8_t normDir = (direction > 0) ? 1 : -1;

  // Keepalive renewal for active continuous MANUAL_JOG in the same direction
  if (stepDu <= 0 && m_isMoving[idx] && m_activeMode[idx] == MotionMode::MANUAL_JOG &&
      !m_hasTarget[idx] && m_direction[idx] == normDir) {
    renewJogLease(axis);
    return true;
  }

  if (m_isMoving[idx]) return false; // Kinematic state invariant: axis is currently moving

  m_activeMode[idx]        = MotionMode::MANUAL_JOG;
  m_direction[idx]         = normDir;
  m_respectSoftLimits[idx] = true;
  m_stepFraction[idx]      = 0;

  if (stepDu > 0) {
    int32_t jogSteps  = duToSteps(m_axes[idx].geometry, stepDu);
    if (jogSteps < 1) jogSteps = 1;
    m_hasTarget[idx]  = true;
    m_targetStep[idx] = m_axes[idx].posSteps + m_direction[idx] * jogSteps;
    m_jogFreqHz[idx]  = 0;
    m_feedHold[idx]   = false;
    m_isMoving[idx]   = true;

    if (m_stepperHal) {
      m_stepperHal->setEnabled(axis, true);
      m_stepperHal->setDirection(axis, m_direction[idx]);
    }
  } else {
    m_hasTarget[idx] = false;
    int32_t jogVelDuSec = static_cast<int32_t>(m_axes[idx].geometry.maxVelocityMmSec * 10000.0f);
    int32_t jogFreqHz = duToSteps(m_axes[idx].geometry, jogVelDuSec);
    if (jogFreqHz < 100) jogFreqHz = 1000;
    m_jogFreqHz[idx] = static_cast<uint32_t>(jogFreqHz);

    renewJogLease(axis);
    m_feedHold[idx] = false;
    m_isMoving[idx] = true;

    if (m_stepperHal) {
      m_stepperHal->startPulseTrain(axis, m_jogFreqHz[idx], m_direction[idx]);
    }
  }

  return true;
}

bool MotionEngine::engageVelocityFeed(const VelocityFeedParams& params) {
  FeedParameters feed;
  feed.axis                = params.axis;
  feed.direction           = params.direction;
  feed.speedDuPerMin       = params.feedRateDuPerMin;
  feed.targetDistDu        = params.targetDistDu;
  feed.accelMmSec2         = params.accelMmSec2;
  feed.respectSoftLimits   = params.respectSoftLimits;
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
  feed.respectSoftLimits   = params.respectSoftLimits;
  feed.syncIndex           = params.syncIndex;
  feed.enableSecondaryAxis = params.enableSecondaryAxis;
  feed.secondaryAxis       = params.secondaryAxis;
  feed.secondaryRatioNum   = params.secondaryRatioNum;
  feed.secondaryRatioDen   = params.secondaryRatioDen;
  return startSpindleSync(feed);
}

void IRAM_ATTR MotionEngine::stopWithReason(AxisId axis, MotionHaltReason reason) {
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

void IRAM_ATTR MotionEngine::stop(AxisId axis, bool emergency) {
  size_t idx = axisIdx(axis);
  MOTION_LOCK();
  m_activeMode[idx]      = MotionMode::STOPPED;
  m_isMoving[idx]        = false;
  m_feedHold[idx]        = false;
  m_hasTarget[idx]       = false;
  m_waitingForIndex[idx] = false;
  m_jogFreqHz[idx]       = 0;
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
  MOTION_UNLOCK();
}

void IRAM_ATTR MotionEngine::stopAll(bool emergency) {
  if (emergency) {
    m_emergencyStop.store(true);
  }
  stop(AXIS_Z, emergency);
  stop(AXIS_X, emergency);
}

void IRAM_ATTR MotionEngine::pauseFeedHold(AxisId axis) {
  size_t idx = axisIdx(axis);
  m_feedHold[idx] = true;
  if (m_stepperHal) {
    m_stepperHal->stopPulseTrain(axis);
  }
}

void IRAM_ATTR MotionEngine::resumeFeedHold(AxisId axis) {
  size_t idx = axisIdx(axis);
  m_feedHold[idx] = false;
  if (m_isMoving[idx] && m_stepperHal) {
    if (m_activeMode[idx] == MotionMode::VELOCITY_FEED) {
      int32_t vel = std::max<int32_t>(1, m_planners[idx].getCurrentVelocity());
      m_stepperHal->startPulseTrain(axis, static_cast<uint32_t>(vel), m_direction[idx]);
    } else if (m_activeMode[idx] == MotionMode::MANUAL_JOG && !m_hasTarget[idx] && m_jogFreqHz[idx] > 0) {
      renewJogLease(axis);
      m_stepperHal->startPulseTrain(axis, m_jogFreqHz[idx], m_direction[idx]);
    }
  }
}

bool IRAM_ATTR MotionEngine::isMoving(AxisId axis) const {
  size_t idx = axisIdx(axis);
  if (m_stepperHal && m_stepperHal->isPulseTrainActive(axis)) {
    return true;
  }
  return m_isMoving[idx] && (m_activeMode[idx] != MotionMode::STOPPED);
}

bool IRAM_ATTR MotionEngine::isAnyAxisMoving() const {
  return isMoving(AXIS_Z) || isMoving(AXIS_X);
}

MotionMode MotionEngine::getActiveMode(AxisId axis) const {
  return m_activeMode[axisIdx(axis)];
}

int32_t IRAM_ATTR MotionEngine::getDistanceToNextLimitDu(AxisId axis) const {
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
  if (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0) {
    m_cachedCountsPerRev = m_encoderHal->getCountsPerSpindleRev();
  } else {
    int32_t profileCounts = static_cast<int32_t>(std::round(SettingsService::getActiveProfile().spindle.countsPerRev()));
    if (profileCounts > 0) {
      m_cachedCountsPerRev = profileCounts;
    }
  }

  if (m_syncTaskHandle == nullptr) {
    UBaseType_t syncPrio = (priority + 1 < configMAX_PRIORITIES) ? (priority + 1) : priority;
    BaseType_t syncRes = xTaskCreatePinnedToCore(
        motionSyncTaskEntry,
        "MotionSync4kHz",
        8192,
        this,
        syncPrio,
        &m_syncTaskHandle,
        coreId);
    if (syncRes != pdPASS) {
      return false;
    }
  }

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
  if (m_syncTimer != nullptr) {
    gptimer_stop(m_syncTimer);
    gptimer_disable(m_syncTimer);
    gptimer_del_timer(m_syncTimer);
    m_syncTimer = nullptr;
  }
  if (m_syncTaskHandle != nullptr) {
    vTaskDelete(m_syncTaskHandle);
    m_syncTaskHandle = nullptr;
  }
  if (m_taskHandle != nullptr) {
#ifdef ESP32
    esp_task_wdt_delete(m_taskHandle);
#endif
    vTaskDelete(m_taskHandle);
    m_taskHandle = nullptr;
  }
#endif
}

void IRAM_ATTR MotionEngine::processCommandPacket(const MotionCommandPacket& pkt) {
  switch (pkt.type) {
    case MotionCommandPacket::Type::START_SPINDLE_SYNC: {
      FeedParameters params;
      params.axis                = pkt.axis;
      params.direction           = pkt.direction;
      params.pitchDupr           = pkt.pitchDupr;
      params.targetDistDu        = pkt.targetCoordDu;
      params.respectSoftLimits   = pkt.respectSoftLimits;
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
      params.respectSoftLimits   = pkt.respectSoftLimits;
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
      MOTION_LOCK();
      m_sync[idx].updatePitchLive(m_accumulatedSpindlePulses, signedPitch);
      MOTION_UNLOCK();
      break;
    }
    case MotionCommandPacket::Type::UPDATE_VELOCITY: {
      size_t idx = axisIdx(pkt.axis);
      if (m_activeMode[idx] == MotionMode::VELOCITY_FEED && m_isMoving[idx] && pkt.speedDuMin > 0) {
        float speedDuSec = static_cast<float>(pkt.speedDuMin) / 60.0f;
        int32_t speedStepsSec = duToSteps(m_axes[idx].geometry, static_cast<int32_t>(speedDuSec));
        if (speedStepsSec < 10) speedStepsSec = 10;
        int32_t curVel = std::max<int32_t>(1, m_planners[idx].getCurrentVelocity());
        int32_t curAccel = (m_planners[idx].getAccel() > 0) ? m_planners[idx].getAccel() : 1000;
        m_planners[idx].configureProfile(curVel, speedStepsSec, 0, curAccel);
      }
      break;
    }
    case MotionCommandPacket::Type::UPDATE_SOFT_LIMITS: {
      size_t idx = axisIdx(pkt.axis);
      MOTION_LOCK();
      if (!pkt.respectSoftLimits && pkt.direction == 0) {
        m_limits[idx].clearAll();
      } else {
        size_t slot = (pkt.direction >= 0) ? 1 : 0;
        if (!pkt.respectSoftLimits) {
          m_limits[idx].setEnabled(slot, false);
        } else {
          int32_t stepPos = duToSteps(m_axes[idx].geometry, pkt.targetCoordDu) - m_axes[idx].originSteps;
          m_limits[idx].setLimit(slot, pkt.targetCoordDu, stepPos, nullptr, true);
        }
      }
      MOTION_UNLOCK();
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

void IRAM_ATTR MotionEngine::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
  m_accumulatedSpindlePulses += deltaPulses;
  m_lastRpm = currentRpm;
  m_sync[0].updateSpindlePhase(deltaPulses);
  m_sync[1].updateSpindlePhase(deltaPulses);
}

void IRAM_ATTR MotionEngine::onSyncTick() {
  if (m_emergencyStop.load(std::memory_order_relaxed)) {
    return;
  }

  // 1. Hardware Spindle Sampling
  if (m_encoderHal) {
    int32_t delta = m_encoderHal->getPulseDelta();
    uint32_t rpm = m_encoderHal->getFilteredRpm();
    onSpindlePulse(delta, rpm);
  }

  // 2. 4 kHz Synchronous Bresenham Dispatch
  bool syncAcquired[NUM_AXES] = { false, false };
  bool pendingHalt[NUM_AXES] = { false, false };
  MotionHaltReason haltReasons[NUM_AXES] = { MotionHaltReason::NORMAL_STOP, MotionHaltReason::NORMAL_STOP };

  MOTION_LOCK();
  int64_t effectivePulses = m_accumulatedSpindlePulses;

  if (m_isDualCoordinated && m_isMoving[0] && m_isMoving[1]) {
    for (size_t a = 0; a < NUM_AXES; ++a) {
      if (!m_sync[a].isEngaged()) continue;
      int8_t aDir = m_direction[a];
      int32_t boundStep = 0;
      bool hasBound = false;

      if (m_respectSoftLimits[a]) {
        const SoftLimit* aLim = m_limits[a].findNextEnforcedLimit(m_axes[a].posSteps, aDir);
        if (aLim) {
          boundStep = aLim->stepPos;
          hasBound = true;
        }
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
                             : m_cachedCountsPerRev;
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
    if (!m_isMoving[idx] || m_feedHold[idx] || m_activeMode[idx] != MotionMode::SPINDLE_SYNC_GEARING) {
      continue;
    }

    AxisId axis = static_cast<AxisId>(idx);
    int8_t dir = m_direction[idx];

    // Evaluate soft limits
    const SoftLimit* lim = m_respectSoftLimits[idx]
        ? m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir)
        : nullptr;
    bool limitReached = false;
    if (lim) {
      int32_t distToLim = (lim->stepPos - m_axes[idx].posSteps) * dir;
      if (distToLim <= 0) {
        limitReached = true;
      }
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
      }
    }

    if (limitReached || targetReached) {
      if (m_isDualCoordinated && targetReached && !limitReached && idx != m_masterAxisIdx) {
        continue;
      }
      pendingHalt[idx] = true;
      haltReasons[idx] = limitReached ? MotionHaltReason::SOFT_LIMIT_REACHED
                                      : MotionHaltReason::TARGET_DISTANCE_REACHED;
      continue;
    }

    if (m_waitingForIndex[idx]) {
      if (m_encoderHal && m_encoderHal->hasIndexSynced()) {
        m_sync[idx].engage(effectivePulses, m_axes[idx].posSteps, m_sync[idx].getPitchDupr());
        m_waitingForIndex[idx] = false;
        syncAcquired[idx] = true;
      } else if (effectivePulses >= m_sync[idx].getAnchorPulses()) {
        m_waitingForIndex[idx] = false;
        syncAcquired[idx] = true;
      } else {
        continue;
      }
    } else if (effectivePulses < m_sync[idx].getAnchorPulses()) {
      // Waiting for armed spindle revolution boundary before initiating step pulses
      continue;
    }

    int32_t effectivePpr = (m_encoderHal && m_encoderHal->getCountsPerSpindleRev() > 0)
                               ? m_encoderHal->getCountsPerSpindleRev()
                               : m_cachedCountsPerRev;
    int32_t idealStep = m_sync[idx].computeIdealSyncSteps(
        effectivePulses,
        m_axes[idx].geometry.motorStepsPerRev,
        m_axes[idx].geometry.screwPitchDu,
        effectivePpr);

    if (lim) {
      if (dir < 0 && idealStep < lim->stepPos) idealStep = lim->stepPos;
      if (dir > 0 && idealStep > lim->stepPos) idealStep = lim->stepPos;
    }

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
  }
  MOTION_UNLOCK();

  for (size_t idx = 0; idx < NUM_AXES; ++idx) {
    AxisId axis = static_cast<AxisId>(idx);
    if (syncAcquired[idx] && m_listener) {
      m_listener->onSpindleSyncAcquired(axis);
    }
    if (pendingHalt[idx]) {
      stopWithReason(axis, haltReasons[idx]);
    }
  }
}

void IRAM_ATTR MotionEngine::onMotionTick() {
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

  // 2. Simulation Heartbeat Watchdog
  if (m_encoderHal && m_encoderHal->isSimulationActive()) {
#if defined(ESP_PLATFORM)
    uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    uint32_t lastHb = m_lastSimHeartbeatMs.load(std::memory_order_relaxed);
    if (lastHb > 0 && (nowMs - lastHb > 1500)) {
      m_encoderHal->setSimulatedRpm(0.0f);
      m_encoderHal->setSimulationActive(false);
      stopWithReason(AXIS_Z, MotionHaltReason::ESTOP);
      stopWithReason(AXIS_X, MotionHaltReason::ESTOP);
    }
#endif
  }

  // 3. Synchronous Spindle Dispatch (Host fallback when 4 kHz GPTimer task is not active)
#if defined(ESP32) || defined(ARDUINO)
  if (!m_syncTaskHandle) {
    onSyncTick();
  }
#else
  onSyncTick();
#endif

  // 4. 1 kHz Supervisory & Hardware Pulse-Train Service (VELOCITY_FEED & MANUAL_JOG)
  for (size_t idx = 0; idx < NUM_AXES; ++idx) {
    if (!m_isMoving[idx] || m_feedHold[idx] ||
        m_activeMode[idx] == MotionMode::STOPPED ||
        m_activeMode[idx] == MotionMode::SPINDLE_SYNC_GEARING) {
      continue;
    }

    AxisId axis = static_cast<AxisId>(idx);
    int8_t dir = m_direction[idx];

    // Evaluate soft limits
    const SoftLimit* lim = m_respectSoftLimits[idx]
        ? m_limits[idx].findNextEnforcedLimit(m_axes[idx].posSteps, dir)
        : nullptr;
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
      if (m_isDualCoordinated && targetReached && !limitReached && idx != m_masterAxisIdx) {
        if (m_stepperHal) m_stepperHal->stopPulseTrain(axis);
        continue;
      }
      MotionHaltReason reason = limitReached ? MotionHaltReason::SOFT_LIMIT_REACHED
                                             : MotionHaltReason::TARGET_DISTANCE_REACHED;
      stopWithReason(axis, reason);
      continue;
    }

    switch (m_activeMode[idx]) {
      case MotionMode::VELOCITY_FEED: {
        constexpr float dt = 0.001f;
        int32_t cmdVel = m_planners[idx].updateVelocity(remainingSteps, dt);
        if (m_planners[idx].isFinished() || cmdVel <= 0) {
          if (m_stepperHal) {
            m_stepperHal->stopPulseTrain(axis);
          }
          if (m_hasTarget[idx] && m_axes[idx].posSteps != m_targetStep[idx]) {
            int8_t stepDir = (m_targetStep[idx] > m_axes[idx].posSteps) ? 1 : -1;
            if (m_stepperHal) {
              m_stepperHal->setDirection(axis, stepDir);
              m_stepperHal->pulseStep(axis);
            }
            m_axes[idx].posSteps += stepDir;
            if (m_axes[idx].posSteps == m_targetStep[idx]) {
              if (!m_isDualCoordinated || idx == m_masterAxisIdx) {
                stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
              }
            }
          } else {
            if (!m_isDualCoordinated || idx == m_masterAxisIdx) {
              stopWithReason(axis, m_hasTarget[idx] ? MotionHaltReason::TARGET_DISTANCE_REACHED : MotionHaltReason::NORMAL_STOP);
            }
          }
        } else {
          if (m_stepperHal) {
            if (!m_stepperHal->isPulseTrainActive(axis)) {
              m_stepperHal->startPulseTrain(axis, static_cast<uint32_t>(cmdVel), dir);
            } else {
              m_stepperHal->updatePulseFrequency(axis, static_cast<uint32_t>(cmdVel));
            }
          }
          // Track kinematic position from hardware pulse-train frequency (no software pulseStep())
          m_stepFraction[idx] += cmdVel;
          while (m_stepFraction[idx] >= 1000) {
            m_stepFraction[idx] -= 1000;
            m_axes[idx].posSteps += dir;
            if (m_hasTarget[idx] && m_axes[idx].posSteps == m_targetStep[idx]) {
              if (!m_isDualCoordinated || idx == m_masterAxisIdx) {
                stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
              }
              break;
            }
          }
        }
        break;
      }

      case MotionMode::MANUAL_JOG: {
        if (m_hasTarget[idx]) {
          // Discrete single-step jog
          if (m_stepperHal) {
            m_stepperHal->setDirection(axis, dir);
            m_stepperHal->pulseStep(axis);
          }
          m_axes[idx].posSteps += dir;
          if (m_axes[idx].posSteps == m_targetStep[idx]) {
            stopWithReason(axis, MotionHaltReason::TARGET_DISTANCE_REACHED);
          }
        } else {
          // Continuous jog: 200 ms Jog Lease Watchdog + hardware pulse-train tracking
          bool leaseExpired = false;
#if defined(ESP32) || defined(ARDUINO)
          uint32_t nowMs = getNowMs();
          leaseExpired = (nowMs - m_lastJogLeaseMs[idx]) >= JOG_LEASE_TIMEOUT_MS;
#else
          m_jogLeaseElapsedMs[idx] += 1;
          leaseExpired = (m_jogLeaseElapsedMs[idx] >= JOG_LEASE_TIMEOUT_MS);
#endif
          if (leaseExpired) {
            stopWithReason(axis, MotionHaltReason::NORMAL_STOP);
            break;
          }

          if (m_stepperHal && !m_stepperHal->isPulseTrainActive(axis) && m_jogFreqHz[idx] > 0) {
            m_stepperHal->startPulseTrain(axis, m_jogFreqHz[idx], dir);
          }
          m_stepFraction[idx] += static_cast<int32_t>(m_jogFreqHz[idx]);
          while (m_stepFraction[idx] >= 1000) {
            m_stepFraction[idx] -= 1000;
            m_axes[idx].posSteps += dir;
          }
        }
        break;
      }

      default:
        break;
    }
  }

  // 5. Telemetry publication (50 Hz / 20 ms cadence)
  publishTelemetrySnapshot();
}

void IRAM_ATTR MotionEngine::publishTelemetrySnapshot() {
#if defined(ESP32) || defined(ARDUINO)
  uint32_t now = getNowMs();
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

void IRAM_ATTR MotionEngine::touchSimHeartbeat() {
#if defined(ESP_PLATFORM)
  m_lastSimHeartbeatMs.store(static_cast<uint32_t>(esp_timer_get_time() / 1000ULL), std::memory_order_relaxed);
#else
  m_lastSimHeartbeatMs.store(1, std::memory_order_relaxed);
#endif
}
