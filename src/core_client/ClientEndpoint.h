#pragma once

/**
 * @file ClientEndpoint.h
 * @brief Base class for connected client sessions binding transport and permissions.
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 * @see docs/adr/0004-pluggable-transports-and-webserial.md
 */

#include "transports/ITransport.h"
#include "ClientContext.h"
#include "core/CommandResult.h"
#include "MachineModel.h"

namespace whatels {
namespace core_client {

class ClientEndpoint {
protected:
  ITransport*   _transport;
  ClientContext _ctx;

public:
  explicit ClientEndpoint(ITransport* transport) : _transport(transport) {
    _ctx.sessionId = 1;
    _ctx.effectivePerms = PERM_ALL;
  }
  virtual ~ClientEndpoint() = default;

  ITransport* getTransport() const { return _transport; }
  void setTransport(ITransport* transport) { _transport = transport; }

  ClientContext& getContext() { return _ctx; }
  const ClientContext& getContext() const { return _ctx; }

  // Processes incoming raw payload, executes domain command, and transmits serialized reply
  virtual bool handleIncoming(const char* payload, size_t length) = 0;

  // Telemetry broadcast
  virtual bool broadcast(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) = 0;
  virtual bool broadcastIfChanged(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) = 0;
};

} // namespace core_client
} // namespace whatels
