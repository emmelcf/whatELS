#include "AsyncPrimitive.h"
#include "Motion.h"
#include <climits>

static const ParamDescriptor ASYNC_PARAMS[] = {
    { "speed", "Feed velocity in deci-microns/min", ParamType::INT32, "du/min", 1500000.0f, 1000.0f, 100000000.0f, 100000.0f, nullptr },
    { "axis", "Target Feed Axis (Z or X)", ParamType::ENUM, "", 0.0f, 0.0f, 1.0f, 1.0f, "Z,X" }
};

static const ModeManifest ASYNC_MANIFEST = {
    ModeIds::PRIM_ASYNC_Z,
    ModeIds::NONE,
    "prim_async",
    "Async Velocity Feed",
    "GEARBOX",
    ModeCategory::PRIMITIVE,
    PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE,
    CAP_MOTOR_Z,
    VisibilityPolicy::HIDE_IF_EXTENDED,
    sizeof(ASYNC_PARAMS) / sizeof(ASYNC_PARAMS[0]),
    ASYNC_PARAMS
};

AsyncPrimitive::AsyncPrimitive() : m_state(ModeState::IDLE), m_targetAxis('Z'), m_speedDuPerMin(1500000) {}

const ModeManifest& AsyncPrimitive::getManifest() const {
    return ASYNC_MANIFEST;
}

Axis* AsyncPrimitive::getTargetAxis() const {
    return (m_targetAxis == 'X') ? &x : &z;
}

void AsyncPrimitive::updateTimerSettings() {
    Axis* a = getTargetAxis();
    if (!a || m_speedDuPerMin <= 0) return;

    // Convert du/min to timer ticks
    long stepsPerMin = (m_speedDuPerMin * a->motorSteps) / a->screwPitch;
    long stepsPerSec = stepsPerMin / 60;
    if (stepsPerSec <= 0) stepsPerSec = 1;

    unsigned int timerLimit = min((long)65535, (long)(TIMER_FREQ / stepsPerSec) - 1);
    setDir(a, dupr > 0);
    timerAlarm(async_timer, timerLimit, true, 0);
    timerWrite(async_timer, 0);
}

bool AsyncPrimitive::setParam(const char* key, float value) {
    if (strcmp(key, "speed") == 0) {
        m_speedDuPerMin = static_cast<long>(value);
        if (m_state == ModeState::RUNNING) updateTimerSettings();
        return true;
    }
    return false;
}

bool AsyncPrimitive::setParam(const char* key, const char* strValue) {
    if (!strValue) return false;
    if (strcmp(key, "speed") == 0) {
        m_speedDuPerMin = atol(strValue);
        if (m_state == ModeState::RUNNING) updateTimerSettings();
        return true;
    }
    if (strcmp(key, "axis") == 0) {
        if (m_state == ModeState::RUNNING) return false; // locked during motion
        m_targetAxis = (strValue[0] == 'X' || strValue[0] == 'x') ? 'X' : 'Z';
        return true;
    }
    return false;
}

bool AsyncPrimitive::getParam(const char* key, float& outVal) const {
    if (strcmp(key, "speed") == 0) {
        outVal = static_cast<float>(m_speedDuPerMin);
        return true;
    }
    return false;
}

bool AsyncPrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (m_speedDuPerMin <= 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Speed must be > 0");
        return false;
    }
    return true;
}

void AsyncPrimitive::onEnter(MotionEngine* engine) {
    (void)engine;
    m_state = ModeState::IDLE;
}

void AsyncPrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    m_state = ModeState::IDLE;
}

bool AsyncPrimitive::start() {
    if (m_speedDuPerMin <= 0) return false;
    updateTimerSettings();
    setAsyncTimerEnable(true);
    m_state = ModeState::RUNNING;
    return true;
}

void AsyncPrimitive::stop(StopType stopType) {
    setAsyncTimerEnable(false);
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool AsyncPrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        updateTimerSettings();
        setAsyncTimerEnable(true);
        m_state = ModeState::RUNNING;
        return true;
    }
    return false;
}

void AsyncPrimitive::onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) {
    // Asynchronous feed does not depend on spindle rotation
}

void AsyncPrimitive::onMotionTick() {
    // Stepping is driven by hardware timer interrupt
}

void AsyncPrimitive::onLimitReached(uint8_t axisId, bool isHighLimit) {
    stop(StopType::NORMAL_STOP);
}

void AsyncPrimitive::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("axis", m_targetAxis == 'X' ? "X" : "Z");
    writer.writeKeyValue("speed_du_min", (int32_t)m_speedDuPerMin);
    writer.writeKeyValue("state", (int32_t)m_state);
}
