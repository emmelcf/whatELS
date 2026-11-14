#pragma once

/**
 * @file AsyncPrimitive.h
 * @brief Legacy asynchronous velocity feed primitive mode.
 * @see docs/modes/ELS_MODE_ARCHITECTURE.md
 * @see docs/adr/0001-motion-invocation-and-gearbox-consolidation.md
 */

#include "../IELSMode.h"
#include "../../kernel/MotionEngine.h"

class AsyncPrimitive : public IELSMode {
private:
    MotionEngine* m_engine = nullptr;
    ModeState     m_state = ModeState::IDLE;
    AxisId        m_axis = AXIS_Z;
    int8_t        m_direction = -1; // -1 (Toward Chuck / Centerline), +1 (Toward Tail / Retract)
    int32_t       m_speedDuPerMin = 1500000; // Default 150 mm/min
    int32_t       m_targetDistDu = -1; // -1 = continuous until limit or stop

public:
    AsyncPrimitive();
    ~AsyncPrimitive() override = default;

    // --- Introspection ---
    const ModeManifest& getManifest() const override;
    ModeId getId() const override { return ModeIds::PRIM_ASYNC_Z; }
    const char* getSlug() const override { return "prim_async"; }
    const char* getName() const override { return "Async Velocity Feed"; }
    ModeCategory getCategory() const override { return ModeCategory::PRIMITIVE; }
    uint32_t getRequiredHardware() const override { return CAP_MOTOR_Z; }
    uint32_t getLivePermissions() const override {
        return PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE;
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
