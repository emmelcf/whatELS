#include "NativeClient.h"
#include "../../StandardCommandParser.h"

namespace whatels {
namespace core_client {

String NativeClient::serializeResponse(const whatels::core::CommandResult& res) {
  return StandardCommandParser::formatWireResponse(res);
}

bool NativeClient::handleIncoming(const char* payload, size_t length) {
  if (!payload || length == 0) return false;

  whatels::core::CommandResult result = StandardCommandParser::instance().parseAndDispatch(payload, length, _ctx);
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
