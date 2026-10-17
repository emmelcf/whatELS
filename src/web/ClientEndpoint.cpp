#include "ClientEndpoint.h"
#include "Config.h"

String GrblSerializer::buildStatus(const MachineModel& model) {
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

String DroSerializer::buildStatus(const MachineModel& model) {
  String out = "";
  out.reserve(96);
  out += "K.zDu=" + String(model.kinematics.z.posDu) + "\n";
  out += "K.xDu=" + String(model.kinematics.x.posDu) + "\n";
  out += "K.rpm=" + String(model.kinematics.spindle.rpm) + "\n";
  out += "K.mode=" + String(model.kinematics.mode) + "\n";
  return out;
}

bool CanonicalCommandTranslator::translateAndDispatch(const char* payload, size_t length, String* reply) {
  if (!payload || length == 0) return false;

  // 1. Sanitize input into clean, null-terminated stack buffer
  char buf[256];
  size_t start = 0;
  while (start < length && (payload[start] == ' ' || payload[start] == '\t' || payload[start] == '\r' || payload[start] == '\n')) {
    start++;
  }
  size_t end = length;
  while (end > start && (payload[end - 1] == ' ' || payload[end - 1] == '\t' || payload[end - 1] == '\r' || payload[end - 1] == '\n')) {
    end--;
  }
  size_t cleanLen = end - start;
  if (cleanLen == 0) return false;
  if (cleanLen >= sizeof(buf)) {
    cleanLen = sizeof(buf) - 1;
  }
  memcpy(buf, payload + start, cleanLen);
  buf[cleanLen] = '\0';

  // 2. Dispatch to CommandDispatcher
  if (CommandDispatcher::dispatch(buf, cleanLen, reply, _clientId)) {
    return true;
  }

  return false;
}
