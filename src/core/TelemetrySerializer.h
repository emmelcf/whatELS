#pragma once

#include <Arduino.h>
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

