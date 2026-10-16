#include "GearZPrimitive.h"
#include "../../MotionConversions.h"
#include <climits>
#include <cmath>

static const ParamDescriptor GEAR_Z_PARAMS[] = {
    { "pitch_dupr", "Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr },
    { "dir", "Travel Direction", ParamType::ENUM, "", 0.0f, 0.0f, 1.0f, 1.0f, "CHUCK,TAIL" },
    { "dist", "Target Distance in deci-microns (-1 = continuous)", ParamType::INT32, "du", -1.0f, -1.0f, 100000000.0f, 1000.0f, nullptr }
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

GearZPrimitive::GearZPrimitive()
    : m_state(ModeState::IDLE),
      m_direction(-1),
      m_pitchDupr(1000),
      m_targetDistDu(-1) {}

const ModeManifest& GearZPrimitive::getManifest() const {
    return GEAR_Z_MANIFEST;
}

bool GearZPrimitive::setParam(const char* key, float value) {
    if (!key) return false;
    if (strcmp(key, "pitch_dupr") == 0) {
        m_pitchDupr = static_cast<int32_t>(value);
        if (m_state == ModeState::RUNNING && m_engine) {
            MotionCommandPacket pkt;
            pkt.type = MotionCommandPacket::Type::UPDATE_FEED_PITCH;
            pkt.axis = AXIS_Z;
            pkt.pitchDupr = m_pitchDupr;
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

bool GearZPrimitive::setParam(const char* key, const char* strValue) {
    if (!key || !strValue) return false;
    if (strcmp(key, "pitch_dupr") == 0) {
        return setParam(key, static_cast<float>(atol(strValue)));
    }
    if (strcmp(key, "dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        int8_t dir = MotionConversions::semanticToDirection(AXIS_Z, strValue);
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

bool GearZPrimitive::getParam(const char* key, float& outVal) const {
    if (!key) return false;
    if (strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(m_pitchDupr);
        return true;
    }
    if (strcmp(key, "dir") == 0) {
        outVal = static_cast<float>(m_direction);
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        outVal = static_cast<float>(m_targetDistDu);
        return true;
    }
    return false;
}

bool GearZPrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (m_pitchDupr <= 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Pitch must be > 0");
        return false;
    }
    if (m_direction == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Direction cannot be 0");
        return false;
    }
    return true;
}

void GearZPrimitive::onEnter(MotionEngine* engine) {
    m_engine = engine ? engine : &MotionEngine::instance();
    m_engine->setEventListener(this);
    m_state = ModeState::IDLE;
}

void GearZPrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    if (m_engine && m_engine->getEventListener() == this) {
        m_engine->setEventListener(nullptr);
    }
    m_state = ModeState::IDLE;
}

bool GearZPrimitive::start() {
    if (m_pitchDupr <= 0 || m_direction == 0) return false;
    if (!m_engine) m_engine = &MotionEngine::instance();
    m_engine->setEventListener(this);

    SpindleSyncParams params;
    params.axis = AXIS_Z;
    params.direction = m_direction;
    params.pitchDupr = m_pitchDupr;
    params.respectSoftLimits = true;
    params.targetDistDu = m_targetDistDu;

    bool ok = m_engine->engageSpindleSync(params);
    m_state = ok ? ModeState::RUNNING : ModeState::IDLE;
    return ok;
}

void GearZPrimitive::stop(StopType stopType) {
    if (m_engine) {
        m_engine->stop(AXIS_Z, stopType == StopType::EMERGENCY_IMMEDIATE);
    }
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool GearZPrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        return start();
    }
    return false;
}

void GearZPrimitive::onMotionHalted(
    AxisId axis,
    MotionHaltReason reason,
    int32_t haltedPosSteps,
    int32_t haltedCoordDu,
    const SoftLimit* triggeredLimit) {
    if (axis == AXIS_Z) {
        if (reason == MotionHaltReason::FEED_HOLD) {
            m_state = ModeState::PAUSED;
        } else {
            m_state = ModeState::IDLE;
        }
    }
}

void GearZPrimitive::writeTelemetry(TelemetryWriter& writer) const {
    if (m_engine) {
        writer.writeKeyValue("z_pos", m_engine->getAxisPositionDu(AXIS_Z));
    }
    writer.writeKeyValue("pitch_dupr", m_pitchDupr);
    writer.writeKeyValue("dir", MotionConversions::directionToSemantic(AXIS_Z, m_direction));
    writer.writeKeyValue("state", static_cast<int32_t>(m_state));
}
