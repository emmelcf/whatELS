#include "NativeClient.h"

namespace whatels {
namespace core_client {

String NativeClient::serializeResponse(const whatels::core::CommandResult& res) {
  switch (res.status) {
    case whatels::core::CommandStatus::OK: {
      if (res.payload.length() == 0) {
        return "OK:CMD." + String(res.command) + "\n";
      }
      // If payload is already a fully formed multi-line block (e.g. CMD.state, SETTINGS, JSON)
      if (res.payload.indexOf('\n') >= 0 || res.payload.startsWith("{") || res.payload.startsWith("SYSINFO") || res.payload.startsWith("SETTINGS")) {
        String out = res.payload;
        if (!out.endsWith("\n")) out += "\n";
        return out;
      }
      // Single-line data or parameter
      return "OK:CMD." + String(res.command) + ":" + res.payload + "\n";
    }

    case whatels::core::CommandStatus::FAILED: {
      if (res.payload.length() == 0) {
        return "ERR:CMD." + String(res.command) + "_FAILED\n";
      }
      if (res.payload.startsWith("ERR:")) {
        String out = res.payload;
        if (!out.endsWith("\n")) out += "\n";
        return out;
      }
      return "ERR:" + res.payload + "\n";
    }

    case whatels::core::CommandStatus::UNRECOGNIZED:
    default:
      return "CMD.error=unknown_command\n";
  }
}

bool NativeClient::handleIncoming(const char* payload, size_t length) {
  if (!payload || length == 0) return false;

  whatels::core::CommandResult result = whatels::core::CommandDispatcher::dispatch(payload, length);
  String wireFrame = serializeResponse(result);
  if (wireFrame.length() > 0 && _transport && _transport->isConnected()) {
    _transport->sendText(wireFrame.c_str());
  }
  return result.isHandled();
}

bool NativeClient::broadcast(const MachineModel& model, uint8_t domainMask) {
  if (!_transport || !_transport->isConnected()) return false;
  uint8_t effectiveMask = domainMask & _activeDomainMask;
  if (effectiveMask == 0) return false;

  String text = TelemetrySerializer::buildTelemetry(model, effectiveMask);
  if (text.length() > 0) {
    _lastTelemetry = text;
    return _transport->sendText(text.c_str());
  }
  return false;
}

bool NativeClient::broadcastIfChanged(const MachineModel& model, uint8_t domainMask) {
  if (!_transport || !_transport->isConnected()) return false;
  uint8_t effectiveMask = domainMask & _activeDomainMask;
  if (effectiveMask == 0) return false;

  String text = TelemetrySerializer::buildTelemetry(model, effectiveMask);
  if (text.length() > 0 && text != _lastTelemetry) {
    _lastTelemetry = text;
    return _transport->sendText(text.c_str());
  }
  return false;
}

} // namespace core_client
} // namespace whatels
