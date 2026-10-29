#pragma once

#include "../IELSMode.h"
#include "../../MotionConversions.h"

/**
 * @brief Consolidated Electronic Gearbox Operational Mode (Subsystem 3).
 * Directly orchestrates synchronized (G95) and asynchronous velocity (G94) kinematic stepping
 * without intermediate primitive wrappers. Adheres to ADR 0001 & ADR 0002.
 */
class UnifiedGearboxMode : public IELSMode {
private:
    ModeState m_state = ModeState::IDLE;

    AxisId    m_axis = AXIS_Z;
    bool      m_isDualAxis = false;
    bool      m_isSync = true;
    int8_t    m_direction = -1;        // Cartesian polarity: -1 (chuck/infeed), +1 (tail/retract)
    int8_t    m_xDirection = -1;       // Secondary X polarity when m_isDualAxis is true: -1 (infeed), +1 (retract)
    int32_t   m_taperRatioDu = 0;      // Secondary X radial delta (du) per 10,000 du of Z when m_isDualAxis is true
    int32_t   m_feedRateDu = 1000;     // Canonical magnitude: dupr if sync, du/min if async
    int32_t   m_targetDistDu = -1;     // Stroke distance in du (-1 = continuous to soft limit)

    MotionEngine* m_engine = nullptr;

public:
    UnifiedGearboxMode();
    ~UnifiedGearboxMode() override = default;

    // --- Introspection ---
    const ModeManifest& getManifest() const override;
    ModeId getId() const override { return ModeIds::GEARBOX; }
    const char* getSlug() const override { return "gearbox"; }
    const char* getName() const override { return "Electronic Gearbox"; }
    ModeCategory getCategory() const override { return ModeCategory::COMPOSITE; }
    uint32_t getRequiredHardware() const override { return CAP_MOTOR_Z; }
    uint32_t getLivePermissions() const override {
        return PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE;
    }

    bool canExecuteCommand(CommandId cmd, ModeState state, AxisId targetAxis = AXIS_Z) const override;

    // --- Atomic Motion Invocation (ADR 0001 / ADR 0002) ---
    bool startFeed(AxisId axis, bool sync, int32_t feedRateDu, int8_t direction,
                   int32_t targetDistDu = -1, bool isDualAxis = false,
                   int8_t xDirection = 0, int32_t taperRatioDu = 0);

    // --- Staging & Parameter Control ---
    bool setActiveAxis(const char* axisStr);
    bool setSync(bool sync);
    bool setDirection(const char* dirStr);
    bool setDirectionSign(int8_t dir);
    bool setFeedRate(int32_t feedRateDu);
    bool setFeedRateMm(float mm);
    bool nudgeFeed(int direction);

    AxisId getAxis() const { return m_axis; }
    bool isDualAxis() const { return m_isDualAxis; }
    const char* getActiveAxisStr() const { return m_isDualAxis ? "ZX" : ((m_axis == AXIS_X) ? "X" : "Z"); }
    const char* getActiveAxis() const { return getActiveAxisStr(); }
    bool isSync() const { return m_isSync; }
    int8_t getDirection() const { return m_direction; }
    bool isFeedDirTowardsChuck() const { return m_direction < 0; }
    bool isFeedDirTowardsTail() const { return m_direction > 0; }
    int32_t getFeedRateDu() const { return m_feedRateDu; }
    int32_t getTargetDistDu() const { return m_targetDistDu; }

    // --- Mode-Agnostic Parameter Interface ---
    bool setParam(const char* key, float value) override;
    bool setParam(const char* key, const char* strValue) override;
    bool getParam(const char* key, float& outVal) const override;
    bool validateParams(char* errBuf, size_t errBufLen) const override;

    // --- Lifecycle Hooks ---
    void onEnter(MotionEngine* engine = nullptr) override;
    void onExit() override;

    // --- Execution State Machine Control ---
    bool start() override;
    void stop(StopType stopType) override;
    bool resume() override;

    ModeState getState() const override { return m_state; }
    bool isRunning() const override { return m_state == ModeState::RUNNING; }

    // --- Layer 2 Motion Event Listener Callbacks ---
    void onMotionHalted(
        AxisId axis,
        MotionHaltReason reason,
        int32_t haltedPosSteps,
        int32_t haltedCoordDu,
        const SoftLimit* triggeredLimit) override;
    void onSpindleSyncAcquired(AxisId axis) override;

    // --- Telemetry ---
    void writeTelemetry(TelemetryWriter& writer) const override;
};
