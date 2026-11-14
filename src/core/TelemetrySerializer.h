#pragma once

/**
 * @file TelemetrySerializer.h
 * @brief Domain-masked telemetry frame serializer (K, I, C, P) for client transports.
 * @see docs/state/ELS_CORE_STATE_MODEL.md
 * @see docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 */

#include "core/PlatformCompat.h"
#include "MachineModel.h"

class TelemetrySerializer {
public:
  // Universal telemetry builders for all client translation layers
  static String buildTelemetry(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL);
  static String buildTelemetry(uint8_t domainMask = DOMAIN_ALL) {
    return buildTelemetry(MachineModel::captureAll(), domainMask);
  }
  static bool hasStateChanged();
};

