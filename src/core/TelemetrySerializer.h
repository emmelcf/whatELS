#pragma once

#include <Arduino.h>
#include "MachineModel.h"
#include "web/ClientEndpoint.h"

class TelemetrySerializer : public ITelemetrySerializer {
public:
  // ITelemetrySerializer interface implementation
  String serialize(const MachineModel& model, uint8_t domainMask) override {
    return buildTelemetry(model, domainMask);
  }
  
  bool hasChanged(const MachineModel& model) override {
    return hasStateChanged();
  }

  // Universal telemetry builders for all client translation layers
  static String buildTelemetry(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL);
  static String buildTelemetry(uint8_t domainMask = DOMAIN_ALL) {
    return buildTelemetry(MachineModel::captureAll(), domainMask);
  }
  static bool hasStateChanged();
};

