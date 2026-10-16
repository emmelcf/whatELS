#include "ModeRegistry.h"
#include "../kernel/MotionEngine.h"
#include <ArduinoJson.h>

IELSMode* ModeRegistry::s_modes[ModeRegistry::MAX_MODES] = { nullptr };
size_t    ModeRegistry::s_modeCount = 0;
IELSMode* ModeRegistry::s_activeMode = nullptr;

bool ModeRegistry::registerMode(IELSMode* mode) {
    if (!mode || s_modeCount >= MAX_MODES) return false;

    // Check for duplicate ID or slug
    for (size_t i = 0; i < s_modeCount; ++i) {
        if (s_modes[i]->getId() == mode->getId()) return false;
        if (strcmp(s_modes[i]->getSlug(), mode->getSlug()) == 0) return false;
    }

    s_modes[s_modeCount++] = mode;
    if (!s_activeMode) {
        s_activeMode = mode;
    }
    return true;
}

IELSMode* ModeRegistry::getActiveMode() {
    return s_activeMode;
}

ModeId ModeRegistry::getActiveModeId() {
    return s_activeMode ? s_activeMode->getId() : ModeIds::NONE;
}

IELSMode* ModeRegistry::getMode(ModeId id) {
    for (size_t i = 0; i < s_modeCount; ++i) {
        if (s_modes[i]->getId() == id) return s_modes[i];
    }
    return nullptr;
}

IELSMode* ModeRegistry::getModeBySlug(const char* slug) {
    if (!slug) return nullptr;
    for (size_t i = 0; i < s_modeCount; ++i) {
        if (strcasecmp(s_modes[i]->getSlug(), slug) == 0) return s_modes[i];
    }
    return nullptr;
}

size_t ModeRegistry::getModeCount() {
    return s_modeCount;
}

IELSMode* ModeRegistry::getModeByIndex(size_t index) {
    if (index < s_modeCount) return s_modes[index];
    return nullptr;
}

bool ModeRegistry::activateMode(ModeId id) {
    IELSMode* target = getMode(id);
    if (!target) return false;

    if (s_activeMode == target) return true;

    // Reject mode swap if current mode is actively cutting
    if (s_activeMode && s_activeMode->isRunning()) {
        return false;
    }

    if (s_activeMode) {
        s_activeMode->onExit();
    }

    s_activeMode = target;
    s_activeMode->onEnter(&MotionEngine::instance());
    return true;
}

bool ModeRegistry::activateMode(const char* slug) {
    IELSMode* target = getModeBySlug(slug);
    if (!target) return false;
    return activateMode(target->getId());
}

bool ModeRegistry::isModeSupported(const IELSMode* mode, uint32_t hardwareCapabilities) {
    if (!mode) return false;
    uint32_t req = mode->getRequiredHardware();
    return (hardwareCapabilities & req) == req;
}

String ModeRegistry::serializeDiscoveryJson(uint32_t hardwareCapabilities, bool includeAll) {
    JsonDocument doc;
    doc["active_mode_id"] = getActiveModeId();
    doc["active_mode_slug"] = s_activeMode ? s_activeMode->getSlug() : "none";

    JsonArray modeArray = doc["modes"].to<JsonArray>();

    for (size_t i = 0; i < s_modeCount; ++i) {
        IELSMode* mode = s_modes[i];
        if (!includeAll && !isModeSupported(mode, hardwareCapabilities)) {
            continue;
        }

        const ModeManifest& manifest = mode->getManifest();
        JsonObject mObj = modeArray.add<JsonObject>();
        mObj["id"] = manifest.id;
        mObj["slug"] = manifest.slug;
        mObj["name"] = manifest.name;
        mObj["family"] = manifest.family;
        mObj["category"] = (manifest.category == ModeCategory::COMPOSITE) ? "COMPOSITE" : "PRIMITIVE";
        mObj["required_hardware"] = manifest.requiredHardwareCaps;
        mObj["live_permissions"] = manifest.livePermissions;

        if (manifest.paramCount > 0 && manifest.params) {
            JsonArray params = mObj["params"].to<JsonArray>();
            for (uint8_t p = 0; p < manifest.paramCount; ++p) {
                const ParamDescriptor& desc = manifest.params[p];
                JsonObject pObj = params.add<JsonObject>();
                pObj["key"] = desc.key;
                pObj["label"] = desc.label;
                switch (desc.type) {
                    case ParamType::INT32:  pObj["type"] = "int"; break;
                    case ParamType::FLOAT:  pObj["type"] = "float"; break;
                    case ParamType::BOOL:   pObj["type"] = "bool"; break;
                    case ParamType::ENUM:   pObj["type"] = "enum"; break;
                    case ParamType::STRING: pObj["type"] = "string"; break;
                }
                if (desc.unit && strlen(desc.unit) > 0) pObj["unit"] = desc.unit;
                pObj["default"] = desc.defaultVal;
                pObj["min"] = desc.minVal;
                pObj["max"] = desc.maxVal;
                pObj["step"] = desc.stepVal;
                if (desc.options && strlen(desc.options) > 0) pObj["options"] = desc.options;
            }
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}

#include "composite/UnifiedGearboxMode.h"

static UnifiedGearboxMode s_unifiedGearboxMode;

void ModeRegistry::initStandardModes() {
    registerMode(&s_unifiedGearboxMode);
    activateMode(ModeIds::GEARBOX);
}

UnifiedGearboxMode* ModeRegistry::getGearboxMode() {
    return &s_unifiedGearboxMode;
}

void ModeRegistry::reset() {
    s_modeCount = 0;
    s_activeMode = nullptr;
    for (size_t i = 0; i < MAX_MODES; ++i) {
        s_modes[i] = nullptr;
    }
}
