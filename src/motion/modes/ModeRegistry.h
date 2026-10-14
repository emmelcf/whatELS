#pragma once

#include "IELSMode.h"

class UnifiedGearboxMode;

/**
 * @brief Authoritative Mode Registry and State Manager for NanoELS.
 * Decouples the motion loop and supervisory systems from concrete mode types.
 */
class ModeRegistry {
private:
    static constexpr size_t MAX_MODES = 16;
    static IELSMode* s_modes[MAX_MODES];
    static size_t    s_modeCount;
    static IELSMode* s_activeMode;

public:
    // Registration
    static bool registerMode(IELSMode* mode);

    // Lookup & Access
    static IELSMode* getActiveMode();
    static ModeId    getActiveModeId();
    static IELSMode* getMode(ModeId id);
    static IELSMode* getModeBySlug(const char* slug);
    static size_t    getModeCount();
    static IELSMode* getModeByIndex(size_t index);

    // Lifecycle & Mode Activation
    static bool activateMode(ModeId id);
    static bool activateMode(const char* slug);

    // Capability Validation & Discovery
    static bool isModeSupported(const IELSMode* mode, uint32_t hardwareCapabilities);
    static String serializeDiscoveryJson(uint32_t hardwareCapabilities, bool includeAll = false);

    // Standard Core Initialization
    static void initStandardModes();
    static UnifiedGearboxMode* getGearboxMode();

    // Reset / Testing
    static void reset();
};
