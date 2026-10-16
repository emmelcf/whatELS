#include "ConePrimitive.h"
#include "../../MotionConversions.h"
#include <climits>
#include <cmath>

static const ParamDescriptor CONE_PARAMS[] = {
    { "cone_ratio", "Diametral Taper Ratio (e.g. 0.05)", ParamType::FLOAT, "ratio", 0.05f, 0.0001f, 10.0f, 0.001f, nullptr },
    { "pitch_dupr", "Z Pitch in deci-microns/rev", ParamType::INT32, "dupr", 1000.0f, 10.0f, 500000.0f, 100.0f, nullptr },
    { "dir", "Z Travel Direction", ParamType::ENUM, "", 0.0f, 0.0f, 1.0f, 1.0f, "CHUCK,TAIL" },
    { "dist", "Z Target Distance in deci-microns (-1 = continuous)", ParamType::INT32, "du", -1.0f, -1.0f, 100000000.0f, 1000.0f, nullptr }
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

ConePrimitive::ConePrimitive()
    : m_state(ModeState::IDLE),
      m_zDirection(-1),
      m_pitchDupr(1000),
      m_coneRatio(0.05f),
      m_auxForward(false),
      m_targetDistDu(-1) {}

const ModeManifest& ConePrimitive::getManifest() const {
    return CONE_MANIFEST;
}

bool ConePrimitive::setParam(const char* key, float value) {
    if (!key) return false;
    if (strcmp(key, "cone_ratio") == 0) {
        if (value <= 0.0f) return false;
        m_coneRatio = value;
        return true;
    }
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
        m_zDirection = (value >= 0.0f) ? 1 : -1;
        return true;
    }
    if (strcmp(key, "aux_dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        m_auxForward = (value != 0.0f);
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = static_cast<int32_t>(value);
        return true;
    }
    return false;
}

bool ConePrimitive::setParam(const char* key, const char* strValue) {
    if (!key || !strValue) return false;
    if (strcmp(key, "cone_ratio") == 0) {
        return setParam(key, static_cast<float>(atof(strValue)));
    }
    if (strcmp(key, "pitch_dupr") == 0) {
        return setParam(key, static_cast<float>(atol(strValue)));
    }
    if (strcmp(key, "dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        int8_t dir = MotionConversions::semanticToDirection(AXIS_Z, strValue);
        if (dir != 0) {
            m_zDirection = dir;
            return true;
        }
        return false;
    }
    if (strcmp(key, "aux_dir") == 0) {
        if (m_state == ModeState::RUNNING) return false;
        m_auxForward = (atoi(strValue) != 0);
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        m_targetDistDu = atol(strValue);
        return true;
    }
    return false;
}

bool ConePrimitive::getParam(const char* key, float& outVal) const {
    if (!key) return false;
    if (strcmp(key, "cone_ratio") == 0) {
        outVal = m_coneRatio;
        return true;
    }
    if (strcmp(key, "pitch_dupr") == 0) {
        outVal = static_cast<float>(m_pitchDupr);
        return true;
    }
    if (strcmp(key, "dir") == 0) {
        outVal = static_cast<float>(m_zDirection);
        return true;
    }
    if (strcmp(key, "aux_dir") == 0) {
        outVal = m_auxForward ? 1.0f : 0.0f;
        return true;
    }
    if (strcmp(key, "dist") == 0) {
        outVal = static_cast<float>(m_targetDistDu);
        return true;
    }
    return false;
}

bool ConePrimitive::validateParams(char* errBuf, size_t errBufLen) const {
    if (m_pitchDupr <= 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Dupr must be > 0");
        return false;
    }
    if (m_coneRatio <= 0.0f) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Cone ratio must be > 0");
        return false;
    }
    if (m_zDirection == 0) {
        if (errBuf && errBufLen > 0) snprintf(errBuf, errBufLen, "Z Direction cannot be 0");
        return false;
    }
    return true;
}

void ConePrimitive::onEnter(MotionEngine* engine) {
    m_engine = engine ? engine : &MotionEngine::instance();
    m_engine->setEventListener(this);
    m_state = ModeState::IDLE;
}

void ConePrimitive::onExit() {
    stop(StopType::NORMAL_STOP);
    if (m_engine && m_engine->getEventListener() == this) {
        m_engine->setEventListener(nullptr);
    }
    m_state = ModeState::IDLE;
}

bool ConePrimitive::start() {
    if (m_pitchDupr <= 0 || m_coneRatio <= 0.0f || m_zDirection == 0) return false;
    if (!m_engine) m_engine = &MotionEngine::instance();
    m_engine->setEventListener(this);

    // Calculate secondary axis taper ratio
    // Radial taper ratio = coneRatio / 2.0 (diametral to radial)
    int32_t den = 10000;
    float radialRatio = (m_coneRatio / 2.0f) * (m_auxForward ? 1.0f : -1.0f);
    int32_t num = static_cast<int32_t>(std::round(radialRatio * static_cast<float>(den)));

    SpindleSyncParams params;
    params.axis = AXIS_Z;
    params.direction = m_zDirection;
    params.pitchDupr = m_pitchDupr;
    params.respectSoftLimits = true;
    params.targetDistDu = m_targetDistDu;
    params.enableSecondaryAxis = true;
    params.secondaryAxis = AXIS_X;
    params.secondaryRatioNum = num;
    params.secondaryRatioDen = den;

    bool ok = m_engine->engageSpindleSync(params);
    m_state = ok ? ModeState::RUNNING : ModeState::IDLE;
    return ok;
}

void ConePrimitive::stop(StopType stopType) {
    if (m_engine) {
        m_engine->stop(AXIS_Z, stopType == StopType::EMERGENCY_IMMEDIATE);
        m_engine->stop(AXIS_X, stopType == StopType::EMERGENCY_IMMEDIATE);
    }
    if (stopType == StopType::FEED_HOLD) {
        m_state = ModeState::PAUSED;
    } else {
        m_state = ModeState::IDLE;
    }
}

bool ConePrimitive::resume() {
    if (m_state == ModeState::PAUSED) {
        return start();
    }
    return false;
}

void ConePrimitive::onMotionHalted(
    AxisId axis,
    MotionHaltReason reason,
    int32_t haltedPosSteps,
    int32_t haltedCoordDu,
    const SoftLimit* triggeredLimit) {
    if (axis == AXIS_Z || axis == AXIS_X) {
        if (m_engine) {
            m_engine->stop(AXIS_Z);
            m_engine->stop(AXIS_X);
        }
        if (reason == MotionHaltReason::FEED_HOLD) {
            m_state = ModeState::PAUSED;
        } else {
            m_state = ModeState::IDLE;
        }
    }
}

void ConePrimitive::writeTelemetry(TelemetryWriter& writer) const {
    if (m_engine) {
        writer.writeKeyValue("z_pos", m_engine->getAxisPositionDu(AXIS_Z));
        writer.writeKeyValue("x_pos", m_engine->getAxisPositionDu(AXIS_X));
    }
    writer.writeKeyValue("pitch_dupr", m_pitchDupr);
    writer.writeKeyValue("cone_ratio", m_coneRatio, 4);
    writer.writeKeyValue("dir", MotionConversions::directionToSemantic(AXIS_Z, m_zDirection));
    writer.writeKeyValue("state", static_cast<int32_t>(m_state));
}
