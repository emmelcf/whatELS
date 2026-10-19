#include "UnifiedGearboxMode.h"
#include "../../kernel/MotionEngine.h"
#include "../../../core/SettingsService.h"
#include <climits>

static const ParamDescriptor GEARBOX_PARAMS[] = {
    { "axis", "Active Feed Axis", ParamType::ENUM, "", 0.0f, 0.0f, 1.0f, 1.0f, "Z,X" },
    { "sync", "Spindle Synchronized", ParamType::BOOL, "", 1.0f, 0.0f, 1.0f, 1.0f, nullptr },
    { "dir", "Travel Direction", ParamType::ENUM, "", 0.0f, 0.0f, 3.0f, 1.0f, "CHUCK,TAIL,INFEED,RETRACT" },
    { "pitch_dupr", "Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr },
    { "speed", "Async Speed in deci-microns/min", ParamType::INT32, "du/min", 1500000.0f, 1000.0f, 100000000.0f, 100000.0f, nullptr },
    { "dist", "Target Distance in deci-microns (-1 = continuous)", ParamType::INT32, "du", -1.0f, -1.0f, 100000000.0f, 1000.0f, nullptr }
};

static const ModeManifest GEARBOX_MANIFEST = {
    ModeIds::GEARBOX,
    ModeIds::NONE,
    "gearbox",
    "Electronic Gearbox",
    "GEARBOX",
    ModeCategory::COMPOSITE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE,
    CAP_MOTOR_Z,
    VisibilityPolicy::ALWAYS_VISIBLE,
    sizeof(GEARBOX_PARAMS) / sizeof(GEARBOX_PARAMS[0]),
    GEARBOX_PARAMS
};

UnifiedGearboxMode::UnifiedGearboxMode()
    : m_state(ModeState::IDLE),
      m_axis(AXIS_Z),
      m_isSync(true),
      m_direction(-1),
      m_feedRateDu(1000),
      m_targetDistDu(-1) {
}

const ModeManifest& UnifiedGearboxMode::getManifest() const {
    return GEARBOX_MANIFEST;
}

bool UnifiedGearboxMode::startFeed(AxisId axis, bool sync, int32_t feedRateDu, int8_t direction, int32_t targetDistDu) {
    if (feedRateDu <= 0 || direction == 0) return false;

    if (!m_engine) {
        m_engine = &MotionEngine::instance();
    }
    m_engine->setEventListener(this);

    m_axis = axis;
    m_isSync = sync;
    m_feedRateDu = feedRateDu;
    m_direction = direction;
    m_targetDistDu = targetDistDu;

    bool ok = false;
    if (m_isSync) {
        SpindleSyncParams params;
        params.axis = axis;
        params.direction = direction;
        params.pitchDupr = feedRateDu;
        params.targetDistDu = targetDistDu;
        params.respectSoftLimits = true;
        ok = m_engine->engageSpindleSync(params);
    } else {
        VelocityFeedParams params;
        params.axis = axis;
        params.direction = direction;
        params.feedRateDuPerMin = feedRateDu;
        params.targetDistDu = targetDistDu;
        params.respectSoftLimits = true;
        ok = m_engine->engageVelocityFeed(params);
    }

    if (ok) {
        m_state = ModeState::RUNNING;
        SettingsService::requestIsOn(true);
        if (m_isSync) {
            SettingsService::setFeedrateDupr(-direction * feedRateDu, false);
        }
    } else {
        m_state = ModeState::IDLE;
        SettingsService::requestIsOn(false);
    }
    return ok;
}

bool UnifiedGearboxMode::setActiveAxis(const char* axisStr) {
    if (!axisStr) return false;
    if (m_state == ModeState::RUNNING) return false; // In-motion lockout

    if (strcasecmp(axisStr, "ZX") == 0 || strcasecmp(axisStr, "Z+X") == 0 || strcmp(axisStr, "2") == 0 || strcasecmp(axisStr, "TAPER") == 0) {
        m_isDualAxis = true;
        m_axis = AXIS_Z;
        return true;
    }
    m_isDualAxis = false;
    m_axis = MotionConversions::parseAxisId(axisStr, AXIS_Z);
    return true;
}

bool UnifiedGearboxMode::setSync(bool sync) {
    if (m_state == ModeState::RUNNING) return false; // In-motion lockout
    m_isSync = sync;
    return true;
}

bool UnifiedGearboxMode::setDirection(const char* dirStr) {
    if (!dirStr) return false;
    if (m_state == ModeState::RUNNING) return false; // In-motion lockout

    int8_t dir = MotionConversions::semanticToDirection(m_axis, dirStr);
    if (dir != 0) {
        m_direction = dir;
        return true;
    }
    return false;
}

bool UnifiedGearboxMode::setDirectionSign(int8_t dir) {
    if (dir != -1 && dir != 1) return false;
    if (m_state == ModeState::RUNNING) return false;
    m_direction = dir;
    return true;
}

bool UnifiedGearboxMode::setFeedRate(int32_t feedRateDu) {
    if (feedRateDu <= 0) return false;
    m_feedRateDu = feedRateDu;
    if (m_state == ModeState::RUNNING) {
        if (!m_engine) m_engine = &MotionEngine::instance();
        if (m_isSync) {
            MotionCommandPacket pkt;
            pkt.type = MotionCommandPacket::Type::UPDATE_FEED_PITCH;
            pkt.axis = m_axis;
            pkt.pitchDupr = m_feedRateDu;
            m_engine->postCommand(pkt);
            SettingsService::setFeedrateDupr(-m_direction * m_feedRateDu, false);
        } else {
            MotionCommandPacket pkt;
            pkt.type = MotionCommandPacket::Type::UPDATE_VELOCITY;
            pkt.axis = m_axis;
            pkt.speedDuMin = m_feedRateDu;
            m_engine->postCommand(pkt);
        }
    }
    return true;
}

bool UnifiedGearboxMode::setFeedRateMm(float mm) {
    return setFeedRate(MotionConversions::mmToDu(mm));
}

bool UnifiedGearboxMode::nudgeFeed(int direction) {
    if (!isRunning()) return false;
    long step = SettingsService::getJogStepDu();
    int32_t newRate = m_feedRateDu + (direction > 0 ? step : -step);
    if (newRate <= 0) return false;
    return setFeedRate(newRate);
}

bool UnifiedGearboxMode::setParam(const char* key, float value) {
    if (!key) return false;
    if (strcmp(key, "sync") == 0) return setSync(value != 0.0f);
    if (strcmp(key, "speed") == 0 || strcmp(key, "pitch_dupr") == 0) return setFeedRate(static_cast<int32_t>(value));
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = static_cast<int32_t>(value);
        return true;
    }
    if (strcmp(key, "nudge") == 0) return nudgeFeed(static_cast<int>(value));
    return false;
}

bool UnifiedGearboxMode::setParam(const char* key, const char* strValue) {
    if (!key || !strValue) return false;
    if (strcmp(key, "axis") == 0) return setActiveAxis(strValue);
    if (strcmp(key, "sync") == 0) return setSync(atoi(strValue) != 0);
    if (strcmp(key, "dir") == 0) return setDirection(strValue);
    if (strcmp(key, "speed") == 0 || strcmp(key, "pitch_dupr") == 0) return setFeedRate(atol(strValue));
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = atol(strValue);
        return true;
    }
    if (strcmp(key, "nudge") == 0) return nudgeFeed(atoi(strValue));
    return false;
}

bool UnifiedGearboxMode::getParam(const char* key, float& outVal) const {
    if (!key) return false;
    if (strcmp(key, "sync") == 0) {
        outVal = m_isSync ? 1.0f : 0.0f;
        return true;
    }
    if (strcmp(key, "speed") == 0 || strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(m_feedRateDu);
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        outVal = static_cast<float>(m_targetDistDu);
        return true;
    }
    return false;
}

bool UnifiedGearboxMode::validateParams(char* errBuf, size_t errBufLen) const {
    if (m_feedRateDu <= 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Feedrate must be > 0");
        return false;
    }
    return true;
}

void UnifiedGearboxMode::onEnter(MotionEngine* engine) {
    m_engine = engine ? engine : &MotionEngine::instance();
    m_engine->setEventListener(this);
    m_state = ModeState::IDLE;
}

void UnifiedGearboxMode::onExit() {
    stop(StopType::NORMAL_STOP);
    if (m_engine && m_engine->getEventListener() == this) {
        m_engine->setEventListener(nullptr);
    }
    m_state = ModeState::IDLE;
}

bool UnifiedGearboxMode::start() {
    return startFeed(m_axis, m_isSync, m_feedRateDu, m_direction, m_targetDistDu);
}

void UnifiedGearboxMode::stop(StopType stopType) {
    if (m_engine) {
        m_engine->stop(m_axis, stopType == StopType::EMERGENCY_IMMEDIATE);
    }
    SettingsService::requestIsOn(false);
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool UnifiedGearboxMode::resume() {
    if (m_state == ModeState::PAUSED) {
        return startFeed(m_axis, m_isSync, m_feedRateDu, m_direction, m_targetDistDu);
    }
    return false;
}

void UnifiedGearboxMode::onMotionHalted(
    AxisId axis,
    MotionHaltReason reason,
    int32_t haltedPosSteps,
    int32_t haltedCoordDu,
    const SoftLimit* triggeredLimit) {
    if (axis == m_axis) {
        SettingsService::requestIsOn(false);
        if (reason == MotionHaltReason::FEED_HOLD) {
            m_state = ModeState::PAUSED;
        } else {
            m_state = ModeState::IDLE;
        }
    }
}

void UnifiedGearboxMode::onSpindleSyncAcquired(AxisId axis) {
    if (axis == m_axis && m_state == ModeState::ARMED) {
        m_state = ModeState::RUNNING;
    }
}

void UnifiedGearboxMode::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("gear_axis", getActiveAxisStr());
    writer.writeKeyValue("gear_sync", static_cast<int32_t>(m_isSync ? 1 : 0));
    writer.writeKeyValue("gear_dir", MotionConversions::directionToSemantic(m_axis, m_direction));
    writer.writeKeyValue("feed_rate", m_feedRateDu);
    writer.writeKeyValue("target_dist", m_targetDistDu);
    writer.writeKeyValue("state", static_cast<int32_t>(m_state));
}

bool UnifiedGearboxMode::canExecuteCommand(CommandId cmd, ModeState state, AxisId targetAxis) const {
    if (cmd == CommandId::CMD_JOG) {
        if (state == ModeState::RUNNING || state == ModeState::ARMED) {
            // In single-axis feed, manual jog is permitted on the idle axis (e.g. X cross-slide while Z feeds)
            if (targetAxis != m_axis) {
                return true;
            }
            return false; // Deny jog on active feed axis
        }
    }
    return IELSMode::canExecuteCommand(cmd, state, targetAxis);
}

