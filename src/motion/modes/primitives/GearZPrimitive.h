#pragma once

/**
 * @file GearZPrimitive.h
 * @brief Legacy Z-axis spindle-synchronized gearing primitive mode.
 * @see docs/modes/ELS_MODE_ARCHITECTURE.md
 * @see docs/adr/0001-motion-invocation-and-gearbox-consolidation.md
 */

#include "../IELSMode.h"
#include "../../kernel/MotionEngine.h"

class GearZPrimitive : public IELSMode {
private:
    MotionEngine* m_engine = nullptr;
    ModeState     m_state = ModeState::IDLE;
    int8_t        m_direction = -1; // -1 (Toward Chuck), +1 (Toward Tail)
    int32_t       m_pitchDupr = 1000; // Default 0.1 mm/rev (1000 dupr)
    int32_t       m_targetDistDu = -1; // -1 = continuous until limit or stop

public:
    GearZPrimitive();
    ~GearZPrimitive() override = default;

    // --- Introspection ---
    const ModeManifest& getManifest() const override;
    ModeId getId() const override { return ModeIds::PRIM_GEAR_Z; }
    const char* getSlug() const override { return "prim_gear_z"; }
    const char* getName() const override { return "Gear Z Spindle Sync"; }
    ModeCategory getCategory() const override { return ModeCategory::PRIMITIVE; }
    uint32_t getRequiredHardware() const override { return CAP_SPINDLE_ENCODER | CAP_MOTOR_Z; }
    uint32_t getLivePermissions() const override {
        return PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE;
    }

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

    // --- Motion Event Listener ---
    void onMotionHalted(
        AxisId axis,
        MotionHaltReason reason,
        int32_t haltedPosSteps,
        int32_t haltedCoordDu,
        const SoftLimit* triggeredLimit) override;

    // --- Telemetry ---
    void writeTelemetry(TelemetryWriter& writer) const override;
};
