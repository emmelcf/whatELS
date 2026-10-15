#include "UnifiedGearboxMode.h"
#include "Motion.h"
#include <climits>

static const ParamDescriptor GEARBOX_PARAMS[] = {
    { "axis", "Active Feed Axis", ParamType::ENUM, "", 0.0f, 0.0f, 2.0f, 1.0f, "Z,X,ZX" },
    { "sync", "Spindle Synchronized", ParamType::BOOL, "", 1.0f, 0.0f, 1.0f, 1.0f, nullptr },
    { "dir", "Travel Direction", ParamType::ENUM, "", 0.0f, 0.0f, 3.0f, 1.0f, "CHUCK,TAIL,CENTER,RETRACT" },
    { "pitch_dupr", "Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr },
    { "speed", "Async Speed in deci-microns/min", ParamType::INT32, "du/min", 1500000.0f, 1000.0f, 100000000.0f, 100000.0f, nullptr },
    { "cone_ratio", "Cone Taper Ratio", ParamType::FLOAT, "ratio", 0.05f, 0.0001f, 10.0f, 0.001f, nullptr }
};

static const ModeManifest GEARBOX_MANIFEST = {
    ModeIds::GEARBOX,
    ModeIds::NONE,
    "gearbox",
    "Electronic Gearbox",
    "GEARBOX",
    ModeCategory::COMPOSITE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE,
    CAP_SPINDLE_ENCODER | CAP_MOTOR_Z,
    VisibilityPolicy::ALWAYS_VISIBLE,
    sizeof(GEARBOX_PARAMS) / sizeof(GEARBOX_PARAMS[0]),
    GEARBOX_PARAMS
};

UnifiedGearboxMode::UnifiedGearboxMode()
    : m_activePrimitive(&m_gearZ),
      m_state(ModeState::IDLE),
      m_isSync(true),
      m_feedDirForward(true) {
    strncpy(m_activeAxis, "Z", sizeof(m_activeAxis));
}

const ModeManifest& UnifiedGearboxMode::getManifest() const {
    return GEARBOX_MANIFEST;
}

bool UnifiedGearboxMode::setActiveAxis(const char* axisStr) {
    if (!axisStr) return false;
    if (m_state == ModeState::RUNNING) return false; // In-motion safety lock!

    if (m_activePrimitive) m_activePrimitive->onExit();

    if (strcasecmp(axisStr, "X") == 0) {
        strncpy(m_activeAxis, "X", sizeof(m_activeAxis));
        if (m_isSync) {
            m_activePrimitive = &m_gearX;
        } else {
            m_asyncFeed.setParam("axis", "X");
            m_activePrimitive = &m_asyncFeed;
        }
    } else if (strcasecmp(axisStr, "ZX") == 0 || strcasecmp(axisStr, "Z+X") == 0 || strcasecmp(axisStr, "TAPER") == 0) {
        strncpy(m_activeAxis, "ZX", sizeof(m_activeAxis)); // compound dual-axis
        m_activePrimitive = &m_cone;
    } else {
        strncpy(m_activeAxis, "Z", sizeof(m_activeAxis));
        if (m_isSync) {
            m_activePrimitive = &m_gearZ;
        } else {
            m_asyncFeed.setParam("axis", "Z");
            m_activePrimitive = &m_asyncFeed;
        }
    }

    m_activePrimitive->onEnter();
    return true;
}

bool UnifiedGearboxMode::setSync(bool sync) {
    if (m_state == ModeState::RUNNING) return false; // In-motion safety lock!
    m_isSync = sync;
    return setActiveAxis(m_activeAxis);
}

bool UnifiedGearboxMode::setDirection(const char* dirStr) {
    if (!dirStr) return false;
    if (m_state == ModeState::RUNNING) return false; // In-motion safety lock!

    bool forward = true;
    if (strcasecmp(dirStr, "TAIL") == 0 || strcasecmp(dirStr, "RETRACT") == 0 || strcasecmp(dirStr, "REV") == 0 || strcasecmp(dirStr, "OUT") == 0) {
        forward = false;
    }

    m_feedDirForward = forward;
    if ((forward && dupr < 0) || (!forward && dupr > 0)) {
        reverseFeedDirection();
    }
    return true;
}

bool UnifiedGearboxMode::nudgeFeed(int direction) {
    if (!isRunning()) return false;
    long step = getFeedStepDu();
    long currentDupr = getThreadSetting().dupr;
    long newDupr = currentDupr + (direction > 0 ? step : -step);
    if (newDupr <= 0) return false;
    return setFeedrateDupr(newDupr, true);
}

bool UnifiedGearboxMode::setParam(const char* key, float value) {
    if (!key) return false;
    if (strcmp(key, "sync") == 0) return setSync(value != 0.0f);
    if (strcmp(key, "nudge") == 0) return nudgeFeed(static_cast<int>(value));
    return m_activePrimitive->setParam(key, value);
}

bool UnifiedGearboxMode::setParam(const char* key, const char* strValue) {
    if (!key || !strValue) return false;
    if (strcmp(key, "axis") == 0) return setActiveAxis(strValue);
    if (strcmp(key, "sync") == 0) return setSync(atoi(strValue) != 0);
    if (strcmp(key, "dir") == 0) return setDirection(strValue);
    if (strcmp(key, "nudge") == 0) return nudgeFeed(atoi(strValue));
    return m_activePrimitive->setParam(key, strValue);
}

bool UnifiedGearboxMode::getParam(const char* key, float& outVal) const {
    if (!key) return false;
    if (strcmp(key, "sync") == 0) {
        outVal = m_isSync ? 1.0f : 0.0f;
        return true;
    }
    return m_activePrimitive->getParam(key, outVal);
}

bool UnifiedGearboxMode::validateParams(char* errBuf, size_t errBufLen) const {
    return m_activePrimitive->validateParams(errBuf, errBufLen);
}

void UnifiedGearboxMode::onEnter() {
    m_state = ModeState::IDLE;
    m_activePrimitive->onEnter();
}

void UnifiedGearboxMode::onExit() {
    stop(StopType::NORMAL_STOP);
    m_activePrimitive->onExit();
    m_state = ModeState::IDLE;
}

bool UnifiedGearboxMode::start() {
    bool ok = m_activePrimitive->start();
    if (ok) {
        m_state = ModeState::RUNNING;
    }
    return ok;
}

void UnifiedGearboxMode::stop(StopType stopType) {
    m_activePrimitive->stop(stopType);
    m_state = m_activePrimitive->getState();
}

bool UnifiedGearboxMode::resume() {
    bool ok = m_activePrimitive->resume();
    if (ok) {
        m_state = ModeState::RUNNING;
    }
    return ok;
}

void UnifiedGearboxMode::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    if (m_state == ModeState::RUNNING) {
        m_activePrimitive->onSpindlePulse(deltaPulses, currentRpm);
    }
}

void UnifiedGearboxMode::onMotionTick() {
    if (m_state == ModeState::RUNNING) {
        m_activePrimitive->onMotionTick();
    }
}

void UnifiedGearboxMode::onLimitReached(uint8_t axisId, bool isHighLimit) {
    m_activePrimitive->onLimitReached(axisId, isHighLimit);
    m_state = m_activePrimitive->getState();
}

void UnifiedGearboxMode::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("gear_axis", m_activeAxis);
    writer.writeKeyValue("gear_sync", static_cast<int32_t>(m_isSync ? 1 : 0));
    writer.writeKeyValue("gear_dir", m_feedDirForward ? "FWD" : "REV");
    m_activePrimitive->writeTelemetry(writer);
}
