#pragma once

/**
 * @file ClientManager.h
 * @brief Multi-client session registry and telemetry broadcast coordinator.
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 * @see docs/adr/0003-command-result-and-baseline-client-architecture.md
 */

#include "ClientEndpoint.h"
#include "clients/native/NativeClient.h"
#include "transports/WebSocketTransport.h"
#include <WebSocketsServer.h>

namespace whatels {
namespace core_client {

class ClientManager {
private:
  static constexpr size_t MAX_WS_CLIENTS = 4;
  ClientEndpoint*     _wsEndpoints[MAX_WS_CLIENTS];
  WebSocketTransport* _wsTransports[MAX_WS_CLIENTS];
  WebSocketsServer*   _server;

  ClientManager();

public:
  static ClientManager& instance();

  void init(WebSocketsServer* wsServer);

  // Connection lifecycle
  void onClientConnected(uint8_t num, const char* path);
  void onClientDisconnected(uint8_t num);
  bool onClientMessage(uint8_t num, const uint8_t* payload, size_t length);

  // Broadcast to all active clients
  void broadcast(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL);
  void broadcastIfChanged(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL);

  ClientEndpoint* getEndpoint(uint8_t num);
};

} // namespace core_client
} // namespace whatels
