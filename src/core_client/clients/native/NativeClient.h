#pragma once

#include "../../ClientEndpoint.h"
#include "core/TelemetrySerializer.h"

namespace nanoels {
namespace core_client {

class NativeClient : public ClientEndpoint {
private:
  uint8_t _activeDomainMask;
  String  _lastTelemetry;

public:
  explicit NativeClient(ITransport* transport)
    : ClientEndpoint(transport), _activeDomainMask(DOMAIN_ALL), _lastTelemetry("") {}

  void setSubscriptionMask(uint8_t mask) { _activeDomainMask = mask; }
  uint8_t getSubscriptionMask() const { return _activeDomainMask; }

  bool handleIncoming(const char* payload, size_t length) override;
  bool broadcast(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) override;
  bool broadcastIfChanged(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) override;

  static String serializeResponse(const nanoels::core::CommandResult& res);
};

} // namespace core_client
} // namespace nanoels
