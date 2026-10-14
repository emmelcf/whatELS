#pragma once

#include <Arduino.h>
#include "LatheProfile.h"

// -----------------------------------------------------------------------------
// 1. Identifiers, State Enums & Stop Types
// -----------------------------------------------------------------------------

typedef uint16_t ModeId;

namespace ModeIds {
    constexpr ModeId NONE         = 0;

    // Primitive Kinematic Kernels (10 - 49)
    constexpr ModeId PRIM_GEAR_Z  = 10;
    constexpr ModeId PRIM_GEAR_X  = 11;
    constexpr ModeId PRIM_ASYNC_Z = 12;
    constexpr ModeId PRIM_ASYNC_X = 13;
    constexpr ModeId PRIM_CONE    = 14;
    constexpr ModeId PRIM_RAPID   = 15;

    // Composite / Orchestrated Modes (50 - 99)
    constexpr ModeId GEARBOX      = 50;
    constexpr ModeId TURNING      = 51;
    constexpr ModeId FACING       = 52;
    constexpr ModeId THREADING    = 53;
    constexpr ModeId CUTOFF       = 54;
    constexpr ModeId SLOTTING     = 55;
    constexpr ModeId SPHERE       = 56;
}

// Execution State Machine
enum class ModeState : uint8_t {
    IDLE = 0,         // Ready, waiting for operator start
    ARMED,            // Waiting for spindle sync index / trigger
    RUNNING,          // Actively stepping motor(s)
    PAUSED,           // Feed hold active; axes decelerated to stop
    RETRACTING,       // Executing automatic clearance move
    COMPLETED,        // Cycle completed successfully
    FAULT             // Limit switch tripped, stall detected, or e-stop
};

// Mode Classification
enum class ModeCategory : uint8_t {
    PRIMITIVE = 0,    // Direct hardware stepper/encoder kernel
    COMPOSITE,        // Orchestrates one or more sub-modes
    UTILITY           // Setup, calibration, test
};

// Runtime Permission Flags governing what live interventions are permitted during active motion
enum LivePermissionFlags : uint32_t {
    PERM_NONE                 = 0,
    PERM_FEED_HOLD            = 1 << 0,  // Supports Feed Hold (pause with sync retained)
    PERM_LIVE_FEED_TRIM       = 1 << 1,  // Supports runtime feedrate nudge ([−] / [+]) under chip load
    PERM_LIVE_STEP_CYCLE      = 1 << 2,  // Supports cycling feed step increment (e.g. 0.01 -> 0.05mm)
    PERM_LIVE_RPM_OVERRIDE    = 1 << 3,  // Supports VFD / spindle RPM adjustment while running
    PERM_PAUSE_MANUAL_JOG     = 1 << 4,  // Allows operator manual jog during pause
    PERM_LIVE_LIMIT_TRIM      = 1 << 5,  // Supports dynamic limit adjustment during operation
    PERM_MULTI_PASS_TELEMETRY = 1 << 6   // Exposes pass count / progress telemetry
};

// Canonical Controller Command Identifiers for In-Motion Safety Validation
enum class CommandId : uint8_t {
    CMD_STOP = 0,             // Normal deceleration stop (universally permitted)
    CMD_ESTOP,                // Immediate emergency stop (universally permitted)
    CMD_RUN,                  // Engage / start motion
    CMD_FEED_HOLD,            // Temporary pause / feed hold
    CMD_FEED_RESUME,          // Resume from feed hold
    CMD_FEED_NUDGE,           // Live feed trim ([−] / [+])
    CMD_STEP_CYCLE,           // Cycle feed step size
    CMD_RPM_OVERRIDE,         // VFD spindle override
    CMD_SET_PARAM,            // Generic parameter write
    CMD_SET_AXIS,             // Switch active motion axis
    CMD_SET_SYNC,             // Switch between spindle sync and async velocity
    CMD_SET_DIR,              // Flip motion travel direction
    CMD_SET_PITCH,            // Major pitch / lead re-specification
    CMD_SET_LIMIT,            // Adjust axis limit stops
    CMD_ZERO_AXIS,            // Zero out DRO coordinate
    CMD_SWITCH_MODE           // Change operating mode
};

// Stop actions
enum class StopType : uint8_t {
    NORMAL_STOP = 0,      // Decelerate and stop in place
    FEED_HOLD,            // Temporary pause, maintain synchronized index
    ABORT_RETRACT,        // Pull tool out along clearance vector, then stop
    EMERGENCY_IMMEDIATE   // Kill step pulses immediately (hard stop)
};

// -----------------------------------------------------------------------------
// 2. Parameter Descriptor & Mode Manifest
// -----------------------------------------------------------------------------

enum class ParamType : uint8_t {
    INT32 = 0,
    FLOAT,
    BOOL,
    ENUM,
    STRING
};

enum class VisibilityPolicy : uint8_t {
    ALWAYS_VISIBLE = 0,    // Always show in menus if hardware requirements are met
    PREFER_EXTENDED,       // Show extended mode by default, allow base mode via toggle
    HIDE_IF_EXTENDED       // Completely hide base mode if an extended descendant is active
};

struct ParamDescriptor {
    const char* key;        // Machine key, e.g. "axis", "pitch_mm", "taper_ratio"
    const char* label;      // Human readable label
    ParamType   type;
    const char* unit;       // "mm", "mm/rev", "deg", "TPI", "steps"
    float       defaultVal;
    float       minVal;
    float       maxVal;
    float       stepVal;
    const char* options;    // Comma-separated enum options, e.g. "Z,X,ZX"
};

struct ModeManifest {
    ModeId                 id;                   // Unique Mode ID (e.g. ModeIds::GEARBOX)
    ModeId                 baseModeId;           // Ancestor / Base Mode ID (or ModeIds::NONE)
    const char*            slug;                 // Machine slug ("gearbox", "thread_auto")
    const char*            name;                 // Display name ("Electronic Gearbox")
    const char*            family;               // Family group ("GEARBOX", "THREADING")
    ModeCategory           category;             // PRIMITIVE, COMPOSITE, or UTILITY
    uint32_t               livePermissions;      // Bitmask of LivePermissionFlags allowed when running
    uint32_t               requiredHardwareCaps; // Bitmask of required LatheCapabilityFlags
    VisibilityPolicy       visibility;           // How it surfaces relative to base mode
    uint8_t                paramCount;
    const ParamDescriptor* params;
};

// -----------------------------------------------------------------------------
// 3. Telemetry Serialization Forward Declaration
// -----------------------------------------------------------------------------

class TelemetryWriter {
public:
    virtual ~TelemetryWriter() = default;
    virtual void writeKeyValue(const char* key, const char* value) = 0;
    virtual void writeKeyValue(const char* key, int32_t value) = 0;
    virtual void writeKeyValue(const char* key, float value, uint8_t decimals = 3) = 0;
};

// -----------------------------------------------------------------------------
// 4. Abstract Mode Interface: IELSMode
// -----------------------------------------------------------------------------

class IELSMode {
public:
    virtual ~IELSMode() = default;

    // --- 1. Introspection & Metadata ---
    virtual const ModeManifest& getManifest() const = 0;
    virtual ModeId getId() const = 0;
    virtual const char* getSlug() const = 0;
    virtual const char* getName() const = 0;
    virtual ModeCategory getCategory() const = 0;
    virtual uint32_t getRequiredHardware() const = 0;

    // --- 2. Mode-Agnostic Parameter Interface ---
    virtual bool setParam(const char* key, float value) = 0;
    virtual bool setParam(const char* key, const char* strValue) = 0;
    virtual bool getParam(const char* key, float& outVal) const = 0;
    virtual bool validateParams(char* errBuf, size_t errBufLen) const = 0;

    // --- 3. Lifecycle Hooks ---
    virtual void onEnter() = 0;
    virtual void onExit() = 0;

    // --- 4. Execution State Machine Control ---
    virtual bool start() = 0;
    virtual void stop(StopType stopType) = 0;
    virtual bool resume() = 0;

    virtual ModeState getState() const = 0;
    virtual bool isRunning() const = 0;

    // --- 5. In-Motion Safety Contract & Gatekeeper ---
    virtual uint32_t getLivePermissions() const = 0;

    virtual bool canExecuteCommand(CommandId cmd, ModeState state) const {
        // Stop and E-Stop are inviolable and universally permitted in all states
        if (cmd == CommandId::CMD_STOP || cmd == CommandId::CMD_ESTOP) return true;

        if (state == ModeState::RUNNING) {
            if (cmd == CommandId::CMD_FEED_HOLD) return (getLivePermissions() & PERM_FEED_HOLD) != 0;
            if (cmd == CommandId::CMD_FEED_NUDGE) return (getLivePermissions() & PERM_LIVE_FEED_TRIM) != 0;
            if (cmd == CommandId::CMD_STEP_CYCLE) return (getLivePermissions() & PERM_LIVE_STEP_CYCLE) != 0;
            if (cmd == CommandId::CMD_RPM_OVERRIDE) return (getLivePermissions() & PERM_LIVE_RPM_OVERRIDE) != 0;
            // Structural modifications (axis, direction, mode swap, pitch rewrite) are strictly locked
            return false;
        }

        if (state == ModeState::PAUSED) {
            if (cmd == CommandId::CMD_FEED_RESUME) return true;
            if (cmd == CommandId::CMD_FEED_NUDGE) return (getLivePermissions() & PERM_LIVE_FEED_TRIM) != 0;
            if (cmd == CommandId::CMD_STEP_CYCLE) return (getLivePermissions() & PERM_LIVE_STEP_CYCLE) != 0;
            return false;
        }

        return true; // All configuration commands permitted when IDLE/STOPPED
    }

    // --- 6. Real-Time Kinematic Hooks (Core 1 Motion Task / Interrupts) ---
    virtual void onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm) = 0;
    virtual void onMotionTick() = 0;
    virtual void onLimitReached(uint8_t axisId, bool isHighLimit) = 0;

    // --- 7. Telemetry & Progress Serialization ---
    virtual void writeTelemetry(TelemetryWriter& writer) const = 0;
};
