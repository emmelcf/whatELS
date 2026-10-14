#pragma once

#include "IELSMode.h"
#include "primitives/GearZPrimitive.h"
#include "primitives/GearXPrimitive.h"
#include "primitives/AsyncPrimitive.h"
#include "primitives/ConePrimitive.h"

/**
 * @brief Unified Gearbox Composite Mode for NanoELS.
 * Statically orchestrates GearZ, GearX, Async, and Cone primitives under a single cohesive workspace.
 */
class UnifiedGearboxMode : public IELSMode {
private:
    GearZPrimitive   m_gearZ;
    GearXPrimitive   m_gearX;
    AsyncPrimitive   m_asyncFeed;
    ConePrimitive    m_cone;

    IELSMode*        m_activePrimitive = &m_gearZ;
    ModeState        m_state = ModeState::IDLE;
    char             m_activeAxis = 'Z';
    bool             m_isSync = true;
    bool             m_feedDirForward = true; // Z: Chuck (-Z), X: Center (-X)

public:
    UnifiedGearboxMode();
    ~UnifiedGearboxMode() override = default;

    // --- Introspection ---
    const ModeManifest& getManifest() const override;
    ModeId getId() const override { return ModeIds::GEARBOX; }
    const char* getSlug() const override { return "gearbox"; }
    const char* getName() const override { return "Electronic Gearbox"; }
    ModeCategory getCategory() const override { return ModeCategory::COMPOSITE; }
    uint32_t getRequiredHardware() const override { return CAP_SPINDLE_ENCODER | CAP_MOTOR_Z; }
    uint32_t getLivePermissions() const override {
        return PERM_FEED_HOLD | PERM_LIVE_FEED_TRIM | PERM_LIVE_STEP_CYCLE | PERM_LIVE_RPM_OVERRIDE;
    }

    // --- Sub-Mode & Parameter Control ---
    bool setActiveAxis(const char* axisStr);
    bool setSync(bool sync);
    bool setDirection(const char* dirStr);
    bool nudgeFeed(int direction);

    char getActiveAxis() const { return m_activeAxis; }
    bool isSync() const { return m_isSync; }
    bool isFeedDirForward() const { return m_feedDirForward; }
    IELSMode* getActivePrimitive() const { return m_activePrimitive; }

    // --- Mode-Agnostic Parameter Interface ---
    bool setParam(const char* key, float value) override;
    bool setParam(const char* key, const char* strValue) override;
    bool getParam(const char* key, float& outVal) const override;
    bool validateParams(char* errBuf, size_t errBufLen) const override;

    // --- Lifecycle Hooks ---
    void onEnter() override;
    void onExit() override;

    // --- Execution State Machine Control ---
    bool start() override;
    void stop(StopType stopType) override;
    bool resume() override;

    ModeState getState() const override { return m_state; }
    bool isRunning() const override { return m_state == ModeState::RUNNING; }

    // --- Kinematics ---
    void onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) override;
    void onMotionTick() override;
    void onLimitReached(uint8_t axisId, bool isHighLimit) override;

    // --- Telemetry ---
    void writeTelemetry(TelemetryWriter& writer) const override;
};
