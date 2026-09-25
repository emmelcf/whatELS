#pragma once

#include <Arduino.h>
#include "MachineModel.h"
#include "ClientEndpoint.h"

class RichClientAdapter : public ITelemetrySerializer {
public:
  // ITelemetrySerializer interface implementation
  String serialize(const MachineModel& model, uint8_t domainMask) override {
    return buildTelemetry(domainMask);
  }
  
  bool hasChanged(const MachineModel& model) override {
    return hasStateChanged();
  }

  // Static helpers for direct or legacy callers
  static String buildTelemetry(uint8_t domainMask = DOMAIN_ALL);
  static bool hasStateChanged();
};
