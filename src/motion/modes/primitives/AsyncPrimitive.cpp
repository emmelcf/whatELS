#include "AsyncPrimitive.h"
#include "../../MotionConversions.h"
#include <climits>
#include <cmath>

static const ParamDescriptor ASYNC_PARAMS[] = {
    { "speed", "Feed velocity in deci-microns/min", ParamType::INT32, "du/min", 1500000.0f, 1000.0f, 100000000.0f, 100000.0f, nullptr },
    { "axis", "Target Feed Axis (Z or X)", ParamType::ENUM, "", 0.0f, 0.0f, 1.0f, 1.0f, "Z,X" },
    { "dir", "Travel Direction", ParamType::ENUM, "", 0.0f, 0.0f, 3.0f, 1.0f, "CHUCK,TAIL,INFEED,RETRACT" },
    { "dist", "Target Distance in deci-microns (-1 = continuous)", ParamType::INT32, "du", -1.0f, -1.0f, 100000000.0f, 1000.0f, nullptr }
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

AsyncPrimitive::AsyncPrimitive()
    : m_state(ModeState::IDLE),
      m_axis(AXIS_Z),
      m_direction(-1),
      m_speedDuPerMin(1500000),
      m_targetDistDu(-1) {}

const ModeManifest& AsyncPrimitive::getManifest() const {
    return ASYNC_MANIFEST;
}

bool AsyncPrimitive::setParam(const char* key, float value) {
    if (!key) return false;
    if (strcmp(key, "speed") == 0) {
        m_speedDuPerMin = static_cast<int32_t>(value);
        if (m_state == ModeState::RUNNING && m_engine) {
            MotionCommandPacket pkt;
            pkt.type = MotionCommandPacket::Type::UPDATE_VELOCITY;
            pkt.axis = m_axis;
            pkt.speedDuMin = m_speedDuPerMin;
            m_engine->postCommand(pkt);
        }
        return true;
    }
    if (strcmp(key, "dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        m_direction = (value >= 0.0f) ? 1 : -1;
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = static_cast<int32_t>(value);
        return true;
    }
    return false;
}

bool AsyncPrimitive::setParam(const char* key, const char* strValue) {
    if (!key || !strValue) return false;
    if (strcmp(key, "speed") == 0) {
        return setParam(key, static_cast<float>(atol(strValue)));
    }
    if (strcmp(key, "axis") == 0) {
        if (m_state == ModeState::RUNNING) return false; // In-motion lockout
        m_axis = MotionConversions::parseAxisId(strValue, AXIS_Z);
        return true;
    }
    if (strcmp(key, "dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        int8_t dir = MotionConversions::semanticToDirection(m_axis, strValue);
        if (dir != 0) {
            m_direction = dir;
            return true;
        }
        return false;
    }
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = atol(strValue);
        return true;
    }
    return false;
}

bool AsyncPrimitive::getParam(const char* key, float& outVal) const {
    if (!key) return false;
    if (strcmp(key, "speed") == 0) {
        outVal = static_cast<float>(m_speedDuPerMin);
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        outVal = static_cast<float>(m_targetDistDu);
        return true;
    }
    if (strcmp(key, "dir") == 0) {
        outVal = static_cast<float>(m_direction);
        return true;
    }
    return false;
}

bool AsyncPrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (m_speedDuPerMin <= 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Speed must be > 0");
        return false;
    }
    if (m_direction == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Direction cannot be 0");
        return false;
    }
    return true;
}

void AsyncPrimitive::onEnter(MotionEngine* engine) {
    m_engine = engine ? engine : &MotionEngine::instance();
    m_engine->setEventListener(this);
    m_state = ModeState::IDLE;
}

void AsyncPrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    if (m_engine && m_engine->getEventListener() == this) {
        m_engine->setEventListener(nullptr);
    }
    m_state = ModeState::IDLE;
}

bool AsyncPrimitive::start() {
    if (m_speedDuPerMin <= 0 || m_direction == 0) return false;
    if (!m_engine) m_engine = &MotionEngine::instance();
    m_engine->setEventListener(this);

    VelocityFeedParams params;
    params.axis = m_axis;
    params.direction = m_direction;
    params.feedRateDuPerMin = m_speedDuPerMin;
    params.respectSoftLimits = true;
    params.targetDistDu = m_targetDistDu;

    bool ok = m_engine->engageVelocityFeed(params);
    m_state = ok ? ModeState::RUNNING : ModeState::IDLE;
    return ok;
}

void AsyncPrimitive::stop(StopType stopType) {
    if (m_engine) {
        m_engine->stop(m_axis, stopType == StopType::EMERGENCY_IMMEDIATE);
    }
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool AsyncPrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        return start();
    }
    return false;
}

void AsyncPrimitive::onMotionHalted(
    AxisId axis,
    MotionHaltReason reason,
    int32_t haltedPosSteps,
    int32_t haltedCoordDu,
    const SoftLimit* triggeredLimit) {
    if (axis == m_axis) {
        if (reason == MotionHaltReason::FEED_HOLD) {
            m_state = ModeState::PAUSED;
        } else {
            m_state = ModeState::IDLE;
        }
    }
}

void AsyncPrimitive::writeTelemetry(TelemetryWriter& writer) const {
    writer.writeKeyValue("axis", (m_axis == AXIS_X) ? "X" : "Z");
    writer.writeKeyValue("speed_du_min", m_speedDuPerMin);
    writer.writeKeyValue("dir", MotionConversions::directionToSemantic(m_axis, m_direction));
    writer.writeKeyValue("target_dist", m_targetDistDu);
    if (m_engine) {
        writer.writeKeyValue("pos_du", m_engine->getAxisPositionDu(m_axis));
    }
    writer.writeKeyValue("state", static_cast<int32_t>(m_state));
}
