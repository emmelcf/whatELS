#include "UnifiedGearboxMode.h"
#include "../../Motion.h"
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

    m_axis = axis;
    m_isSync = sync;
    m_feedRateDu = feedRateDu;
    m_direction = direction;
    m_targetDistDu = targetDistDu;

    Axis* a = (axis == AXIS_X) ? &x : &z;

    if (m_isSync) {
        // Stop velocity timer if running
        stopVelocityFeed();

        // Configure sync spindle gearing
        long signedDupr = direction * feedRateDu;
        LatheMode latheMode = (axis == AXIS_X) ? LatheMode::XGEAR : LatheMode::NORMAL;
        setModeFromTask(latheMode);
        setFeedrateDupr(signedDupr, false);
        setIsOnFromTask(true);
        m_state = ModeState::RUNNING;
        return true;
    } else {
        // Velocity feed
        setModeFromTask(LatheMode::ASYNC);
        dupr = 0;
        setIsOnFromTask(true);
        bool ok = startVelocityFeed(a, feedRateDu, direction, targetDistDu);
        if (ok) {
            m_state = ModeState::RUNNING;
        } else {
            setIsOnFromTask(false);
            m_state = ModeState::IDLE;
        }
        return ok;
    }
}

bool UnifiedGearboxMode::setActiveAxis(const char* axisStr) {
    if (!axisStr) return false;
    if (m_state == ModeState::RUNNING) return false; // In-motion lockout

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
        // Dynamic in-cut update
        if (m_isSync) {
            setFeedrateDupr(m_direction * m_feedRateDu, false);
        } else {
            Axis* a = (m_axis == AXIS_X) ? &x : &z;
            startVelocityFeed(a, m_feedRateDu, m_direction, m_targetDistDu);
        }
    }
    return true;
}

bool UnifiedGearboxMode::setFeedRateMm(float mm) {
    return setFeedRate(MotionConversions::mmToDu(mm));
}

bool UnifiedGearboxMode::nudgeFeed(int direction) {
    if (!isRunning()) return false;
    long step = getFeedStepDu();
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

void UnifiedGearboxMode::onEnter() {
    m_state = ModeState::IDLE;
}

void UnifiedGearboxMode::onExit() {
    stop(StopType::NORMAL_STOP);
    m_state = ModeState::IDLE;
}

bool UnifiedGearboxMode::start() {
    return startFeed(m_axis, m_isSync, m_feedRateDu, m_direction, m_targetDistDu);
}

void UnifiedGearboxMode::stop(StopType stopType) {
    if (!m_isSync) {
        stopVelocityFeed();
    }
    setIsOnFromTask(false);
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

void UnifiedGearboxMode::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    // Spindle pulses for synchronous feed are driven in Core 1 ISR
}

void UnifiedGearboxMode::onMotionTick() {
    if (m_state == ModeState::RUNNING) {
        if (!m_isSync) {
            if (!isVelocityFeedActive()) {
                // Velocity feed reached soft limit or target stroke
                stop(StopType::NORMAL_STOP);
            }
        }
    }
}

void UnifiedGearboxMode::onLimitReached(uint8_t axisId, bool isHighLimit) {
    stop(StopType::NORMAL_STOP);
}

void UnifiedGearboxMode::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("gear_axis", (m_axis == AXIS_X) ? "X" : "Z");
    writer.writeKeyValue("gear_sync", static_cast<int32_t>(m_isSync ? 1 : 0));
    writer.writeKeyValue("gear_dir", MotionConversions::directionToSemantic(m_axis, m_direction));
    writer.writeKeyValue("feed_rate", m_feedRateDu);
    writer.writeKeyValue("target_dist", m_targetDistDu);
    writer.writeKeyValue("state", static_cast<int32_t>(m_state));
}
