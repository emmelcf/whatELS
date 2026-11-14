#pragma once

/**
 * @file NativeClient.h
 * @brief Native canonical protocol client endpoint with domain-masked delta telemetry.
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 * @see docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 */

#include "../../ClientEndpoint.h"
#include "core/TelemetrySerializer.h"

namespace whatels {
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

  static String serializeResponse(const whatels::core::CommandResult& res);
};

} // namespace core_client
} // namespace whatels
