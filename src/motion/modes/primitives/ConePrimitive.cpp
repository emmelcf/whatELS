#include "ConePrimitive.h"
#include "Motion.h"
#include <climits>

static const ParamDescriptor CONE_PARAMS[] = {
    { "cone_ratio", "Diametral Taper Ratio (e.g. 0.05)", ParamType::FLOAT, "ratio", 0.05f, 0.0001f, 10.0f, 0.001f, nullptr },
    { "pitch_dupr", "Z Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr }
};

static const ModeManifest CONE_MANIFEST = {
    ModeIds::PRIM_CONE,
    ModeIds::NONE,
    "prim_cone",
    "Cone Taper Sync",
    "GEARBOX",
    ModeCategory::PRIMITIVE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE,
    CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_MOTOR_X,
    VisibilityPolicy::HIDE_IF_EXTENDED,
    sizeof(CONE_PARAMS) / sizeof(CONE_PARAMS[0]),
    CONE_PARAMS
};

ConePrimitive::ConePrimitive() : m_state(ModeState::IDLE) {}

const ModeManifest& ConePrimitive::getManifest() const {
    return CONE_MANIFEST;
}

bool ConePrimitive::setParam(const char* key, float value) {
    if (strcmp(key, "cone_ratio") == 0) {
        setConeRatio(value);
        applyConeRatio();
        return true;
    }
    if (strcmp(key, "pitch_dupr") == 0) {
        return setFeedrateDupr(static_cast<long>(value), true);
    }
    return false;
}

bool ConePrimitive::setParam(const char* key, const char* strValue) {
    if (!strValue) return false;
    if (strcmp(key, "cone_ratio") == 0) {
        return setParam(key, static_cast<float>(atof(strValue)));
    }
    if (strcmp(key, "pitch_dupr") == 0) {
        return setFeedrateDupr(atol(strValue), true);
    }
    return false;
}

bool ConePrimitive::getParam(const char* key, float& outVal) const {
    if (strcmp(key, "cone_ratio") == 0) {
        outVal = coneRatio;
        return true;
    }
    if (strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(dupr);
        return true;
    }
    return false;
}

bool ConePrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (dupr == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Dupr cannot be 0");
        return false;
    }
    if (coneRatio == 0.0f) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Cone ratio cannot be 0");
        return false;
    }
    return true;
}

void ConePrimitive::onEnter() {
    m_state = ModeState::IDLE;
}

void ConePrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    m_state = ModeState::IDLE;
}

bool ConePrimitive::start() {
    if (dupr == 0 || coneRatio == 0.0f) return false;
    m_state = ModeState::RUNNING;
    return true;
}

void ConePrimitive::stop(StopType stopType) {
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool ConePrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        m_state = ModeState::RUNNING;
        return true;
    }
    return false;
}

void ConePrimitive::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    // Spindle pulses are accumulated in spindlePosAvg
}

void ConePrimitive::onMotionTick() {
    if (m_state != ModeState::RUNNING) return;
    if (z.movingManually || x.movingManually || coneRatio == 0.0f || dupr == 0) return;

    float zToXRatio = -coneRatio / 2.0f / z.motorSteps * x.motorSteps / x.screwPitch * z.screwPitch * (auxForward ? 1.0f : -1.0f);
    if (zToXRatio == 0.0f) return;

    x.speedMax = LONG_MAX;
    z.speedMax = LONG_MAX;

    long spindle = spindlePosAvg;
    long spindleMin = LONG_MIN;
    long spindleMax = LONG_MAX;

    if (z.leftStop != LONG_MAX) {
        (dupr > 0 ? spindleMax : spindleMin) = spindleFromPos(&z, z.leftStop);
    }
    if (z.rightStop != LONG_MIN) {
        (dupr > 0 ? spindleMin : spindleMax) = spindleFromPos(&z, z.rightStop);
    }
    if (x.leftStop != LONG_MAX) {
        long lim = spindleFromPos(&z, round(x.leftStop / zToXRatio));
        if (zToXRatio < 0) {
            (dupr > 0 ? spindleMin : spindleMax) = lim;
        } else {
            (dupr > 0 ? spindleMax : spindleMin) = lim;
        }
    }
    if (x.rightStop != LONG_MIN) {
        long lim = spindleFromPos(&z, round(x.rightStop / zToXRatio));
        if (zToXRatio < 0) {
            (dupr > 0 ? spindleMax : spindleMin) = lim;
        } else {
            (dupr > 0 ? spindleMin : spindleMax) = lim;
        }
    }

    if (spindle > spindleMax) spindle = spindleMax;
    else if (spindle < spindleMin) spindle = spindleMin;

    stepToContinuous(&z, posFromSpindle(&z, spindle, true));
    stepToContinuous(&x, round(z.pos * zToXRatio));
}

void ConePrimitive::onLimitReached(uint8_t axisId, bool isHighLimit) {
    m_state = ModeState::IDLE;
}

void ConePrimitive::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("z_pos", z.pos);
    writer.writeKeyValue("x_pos", x.pos);
    writer.writeKeyValue("cone_ratio", coneRatio, 4);
    writer.writeKeyValue("state", (int32_t)m_state);
}
