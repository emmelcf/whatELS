#pragma once

/**
 * @file ClientSerializers.h
 * @brief Protocol-specific telemetry serializers (e.g., GRBL-compatible status).
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 */

#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#include "MachineModel.h"
#include "Config.h"

namespace whatels {
namespace core_client {

class GrblSerializer {
public:
  static String buildStatus(const MachineModel& model) {
    String out = "<";
    out += model.kinematics.isOn ? "Run" : "Idle";
    out += "|WPos:";
    out += String(model.kinematics.x.posDu / 10000.0f, 3);
    out += ",0.000,";
    out += String(model.kinematics.z.posDu / 10000.0f, 3);
    out += "|FS:0,";
    out += String(model.kinematics.spindle.rpm);
    out += "|Id:" CONTROLLER_VERSION_TEXT ">\r\n";
    return out;
  }
};

class DroSerializer {
public:
  static String buildStatus(const MachineModel& model) {
    String out = "";
    out.reserve(96);
    out += "K.zDu=" + String(model.kinematics.z.posDu) + "\n";
    out += "K.xDu=" + String(model.kinematics.x.posDu) + "\n";
    out += "K.rpm=" + String(model.kinematics.spindle.rpm) + "\n";
    out += "K.mode=" + String(model.kinematics.mode) + "\n";
    return out;
  }
};

} // namespace core_client
} // namespace whatels
#endif
