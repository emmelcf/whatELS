#pragma once

#include "transports/ITransport.h"
#include "core/CommandDispatcher.h"
#include "MachineModel.h"

namespace whatels {
namespace core_client {

class ClientEndpoint {
protected:
  ITransport* _transport;

public:
  explicit ClientEndpoint(ITransport* transport) : _transport(transport) {}
  virtual ~ClientEndpoint() = default;

  ITransport* getTransport() const { return _transport; }
  void setTransport(ITransport* transport) { _transport = transport; }

  // Processes incoming raw payload, executes domain command, and transmits serialized reply
  virtual bool handleIncoming(const char* payload, size_t length) = 0;

  // Telemetry broadcast
  virtual bool broadcast(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) = 0;
  virtual bool broadcastIfChanged(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) = 0;
};

} // namespace core_client
} // namespace whatels
