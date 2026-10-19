#pragma once

#include "ITransport.h"
#include <WebSocketsServer.h>

namespace nanoels {
namespace core_client {

class WebSocketTransport : public ITransport {
private:
  WebSocketsServer* _server;
  uint8_t           _clientId;

public:
  WebSocketTransport(WebSocketsServer* server, uint8_t clientId)
    : _server(server), _clientId(clientId) {}

  void setClientId(uint8_t clientId) { _clientId = clientId; }
  uint8_t getClientId() const { return _clientId; }

  bool isConnected() const override {
    return _server != nullptr;
  }

  bool sendText(const char* text) override {
    if (_server && text) {
      return _server->sendTXT(_clientId, text);
    }
    return false;
  }

  bool sendBytes(const uint8_t* buffer, size_t length) override {
    if (_server && buffer && length > 0) {
      return _server->sendBIN(_clientId, buffer, length);
    }
    return false;
  }
};

} // namespace core_client
} // namespace nanoels
