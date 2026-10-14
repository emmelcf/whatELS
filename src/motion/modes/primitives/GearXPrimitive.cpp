#include "GearXPrimitive.h"
#include "Motion.h"
#include <climits>

static const ParamDescriptor GEAR_X_PARAMS[] = {
    { "pitch_dupr", "Pitch in deci-microns/rev", ParamType::INT32, "dupr", 500.0f, 10.0f, 250000.0f, 50.0f, nullptr }
};

static const ModeManifest GEAR_X_MANIFEST = {
    ModeIds::PRIM_GEAR_X,
    ModeIds::NONE,
    "prim_gear_x",
    "Gear X Spindle Sync",
    "GEARBOX",
    ModeCategory::PRIMITIVE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE,
    CAP_SPINDLE_ENCODER | CAP_MOTOR_X,
    VisibilityPolicy::HIDE_IF_EXTENDED,
    sizeof(GEAR_X_PARAMS) / sizeof(GEAR_X_PARAMS[0]),
    GEAR_X_PARAMS
};

GearXPrimitive::GearXPrimitive() : m_state(ModeState::IDLE) {}

const ModeManifest& GearXPrimitive::getManifest() const {
    return GEAR_X_MANIFEST;
}

bool GearXPrimitive::setParam(const char* key, float value) {
    if (strcmp(key, "pitch_dupr") == 0) {
        return setFeedrateDupr(static_cast<long>(value), true);
    }
    return false;
}

bool GearXPrimitive::setParam(const char* key, const char* strValue) {
    if (strcmp(key, "pitch_dupr") == 0 && strValue) {
        return setFeedrateDupr(atol(strValue), true);
    }
    return false;
}

bool GearXPrimitive::getParam(const char* key, float& outVal) const {
    if (strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(dupr);
        return true;
    }
    return false;
}

bool GearXPrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (dupr == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Dupr cannot be 0");
        return false;
    }
    return true;
}

void GearXPrimitive::onEnter() {
    m_state = ModeState::IDLE;
}

void GearXPrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    m_state = ModeState::IDLE;
}

bool GearXPrimitive::start() {
    if (dupr == 0) return false;
    m_state = ModeState::RUNNING;
    return true;
}

void GearXPrimitive::stop(StopType stopType) {
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool GearXPrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        m_state = ModeState::RUNNING;
        return true;
    }
    return false;
}

void GearXPrimitive::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    // Spindle pulses are accumulated in spindlePosAvg
}

void GearXPrimitive::onMotionTick() {
    if (m_state != ModeState::RUNNING) return;
    if (x.movingManually) return;

    x.speedMax = LONG_MAX;
    stepToContinuous(&x, posFromSpindle(&x, spindlePosAvg, true));
}

void GearXPrimitive::onLimitReached(uint8_t axisId, bool isHighLimit) {
    m_state = ModeState::IDLE;
}

void GearXPrimitive::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("x_pos", x.pos);
    writer.writeKeyValue("dupr", (int32_t)dupr);
    writer.writeKeyValue("state", (int32_t)m_state);
}
