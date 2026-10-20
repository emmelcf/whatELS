#include "ClientManager.h"

namespace whatels {
namespace core_client {

ClientManager::ClientManager() : _server(nullptr) {
  for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    _wsEndpoints[i] = nullptr;
    _wsTransports[i] = nullptr;
  }
}

ClientManager& ClientManager::instance() {
  static ClientManager inst;
  return inst;
}

void ClientManager::init(WebSocketsServer* wsServer) {
  _server = wsServer;
  for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (_wsTransports[i]) delete _wsTransports[i];
    _wsTransports[i] = new WebSocketTransport(_server, i);
    if (_wsEndpoints[i]) {
      delete _wsEndpoints[i];
      _wsEndpoints[i] = nullptr;
    }
  }
}

void ClientManager::onClientConnected(uint8_t num, const char* path) {
  if (num >= MAX_WS_CLIENTS || !_server) return;

  if (_wsEndpoints[num]) {
    delete _wsEndpoints[num];
    _wsEndpoints[num] = nullptr;
  }

  // Determine client type by request path
  if (!path || *path == '\0' || strcmp(path, "/") == 0 || strncmp(path, "/canonical", 10) == 0) {
    _wsEndpoints[num] = new NativeClient(_wsTransports[num]);
    // Immediate hydration frame
    MachineModel m = MachineModel::captureAll();
    _wsEndpoints[num]->broadcast(m, DOMAIN_ALL);
  }
}

void ClientManager::onClientDisconnected(uint8_t num) {
  if (num >= MAX_WS_CLIENTS) return;
  if (_wsEndpoints[num]) {
    delete _wsEndpoints[num];
    _wsEndpoints[num] = nullptr;
  }
}

bool ClientManager::onClientMessage(uint8_t num, const uint8_t* payload, size_t length) {
  if (num >= MAX_WS_CLIENTS || !_wsEndpoints[num]) return false;
  return _wsEndpoints[num]->handleIncoming((const char*)payload, length);
}

void ClientManager::broadcast(const MachineModel& model, uint8_t domainMask) {
  for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (_wsEndpoints[i]) {
      _wsEndpoints[i]->broadcast(model, domainMask);
    }
  }
}

void ClientManager::broadcastIfChanged(const MachineModel& model, uint8_t domainMask) {
  for (size_t i = 0; i < MAX_WS_CLIENTS; ++i) {
    if (_wsEndpoints[i]) {
      _wsEndpoints[i]->broadcastIfChanged(model, domainMask);
    }
  }
}

ClientEndpoint* ClientManager::getEndpoint(uint8_t num) {
  if (num >= MAX_WS_CLIENTS) return nullptr;
  return _wsEndpoints[num];
}

} // namespace core_client
} // namespace whatels
