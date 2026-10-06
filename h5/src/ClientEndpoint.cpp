#include "ClientEndpoint.h"
#include "Config.h"

String GrblSerializer::buildStatus(const MachineModel& model) {
  String out = "<";
  out += model.kinematics.isOn ? "Run" : "Idle";
  out += "|WPos:";
  out += String(model.kinematics.xDu / 10000.0f, 3);
  out += ",";
  out += String(model.kinematics.yDu / 10000.0f, 3);
  out += ",";
  out += String(model.kinematics.zDu / 10000.0f, 3);
  out += "|FS:0,";
  out += String(model.kinematics.rpm);
  out += "|Id:" CONTROLLER_VERSION_TEXT ">\r\n";
  return out;
}

String DroSerializer::buildStatus(const MachineModel& model) {
  String out = "";
  out.reserve(96);
  out += "K.zDu=" + String(model.kinematics.zDu) + "\n";
  out += "K.xDu=" + String(model.kinematics.xDu) + "\n";
  out += "K.rpm=" + String(model.kinematics.rpm) + "\n";
  out += "K.mode=" + String(model.kinematics.mode) + "\n";
  return out;
}
