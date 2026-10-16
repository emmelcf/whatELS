#include "GearZPrimitive.h"
#include "Motion.h"
#include <climits>

static const ParamDescriptor GEAR_Z_PARAMS[] = {
    { "pitch_dupr", "Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr }
};

static const ModeManifest GEAR_Z_MANIFEST = {
    ModeIds::PRIM_GEAR_Z,
    ModeIds::NONE,
    "prim_gear_z",
    "Gear Z Spindle Sync",
    "GEARBOX",
    ModeCategory::PRIMITIVE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE,
    CAP_SPINDLE_ENCODER | CAP_MOTOR_Z,
    VisibilityPolicy::HIDE_IF_EXTENDED,
    sizeof(GEAR_Z_PARAMS) / sizeof(GEAR_Z_PARAMS[0]),
    GEAR_Z_PARAMS
};

GearZPrimitive::GearZPrimitive() : m_state(ModeState::IDLE) {}

const ModeManifest& GearZPrimitive::getManifest() const {
    return GEAR_Z_MANIFEST;
}

bool GearZPrimitive::setParam(const char* key, float value) {
    if (strcmp(key, "pitch_dupr") == 0) {
        return setFeedrateDupr(static_cast<long>(value), true);
    }
    return false;
}

bool GearZPrimitive::setParam(const char* key, const char* strValue) {
    if (strcmp(key, "pitch_dupr") == 0 && strValue) {
        return setFeedrateDupr(atol(strValue), true);
    }
    return false;
}

bool GearZPrimitive::getParam(const char* key, float& outVal) const {
    if (strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(dupr);
        return true;
    }
    return false;
}

bool GearZPrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (dupr == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Dupr cannot be 0");
        return false;
    }
    return true;
}

void GearZPrimitive::onEnter(MotionEngine* engine) {
    (void)engine;
    m_state = ModeState::IDLE;
}

void GearZPrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    m_state = ModeState::IDLE;
}

bool GearZPrimitive::start() {
    if (dupr == 0) return false;
    m_state = ModeState::RUNNING;
    return true;
}

void GearZPrimitive::stop(StopType stopType) {
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool GearZPrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        m_state = ModeState::RUNNING;
        return true;
    }
    return false;
}

void GearZPrimitive::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    // Spindle pulses are accumulated in spindlePosAvg
}

void GearZPrimitive::onMotionTick() {
    if (m_state != ModeState::RUNNING) return;
    if (z.movingManually) return;

    z.speedMax = LONG_MAX;
    stepToContinuous(&z, posFromSpindle(&z, spindlePosAvg, true));
}

void GearZPrimitive::onLimitReached(uint8_t axisId, bool isHighLimit) {
    // Stop forward motion when hitting hardware limit
    m_state = ModeState::IDLE;
}

void GearZPrimitive::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("z_pos", static_cast<int32_t>(z.pos));
    writer.writeKeyValue("dupr", (int32_t)dupr);
    writer.writeKeyValue("state", (int32_t)m_state);
}
