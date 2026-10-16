#pragma once

#include "../IELSMode.h"
#include "Axis.h"

class MotionEngine;

class GearZPrimitive : public IELSMode {
private:
    ModeState m_state = ModeState::IDLE;

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

    // --- Kinematics ---
    void onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm);
    void onMotionTick();
    void onLimitReached(uint8_t axisId, bool isHighLimit);

    // --- Telemetry ---
    void writeTelemetry(TelemetryWriter& writer) const override;
};
