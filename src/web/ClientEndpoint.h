#pragma once

#include <Arduino.h>
#include "MachineModel.h"
#include "CommandDispatcher.h"

// -----------------------------------------------------------------------------
// 1. Transport Interface (I/O wire: WebSocket, UART, BLE, etc.)
// -----------------------------------------------------------------------------
class ITransport {
public:
  virtual ~ITransport() = default;
  virtual bool isConnected() const = 0;
  virtual bool sendText(const String& text) = 0;
  virtual bool sendBytes(const uint8_t* data, size_t length) { return false; }
};

// -----------------------------------------------------------------------------
// 2. Serializer Interface (Transforms MachineModel -> Wire Protocol)
// -----------------------------------------------------------------------------
class ITelemetrySerializer {
public:
  virtual ~ITelemetrySerializer() = default;
  virtual String serialize(const MachineModel& model, uint8_t domainMask) = 0;
  virtual bool hasChanged(const MachineModel& model) = 0;
};

// GRBL Serializer (CNC Senders / Compact status string)
class GrblSerializer : public ITelemetrySerializer {
public:
  String serialize(const MachineModel& model, uint8_t domainMask) override {
    return buildStatus(model);
  }
  bool hasChanged(const MachineModel& model) override {
    return true;
  }
  static String buildStatus(const MachineModel& model);
};

// DRO Serializer (Minimal Wireless DRO Coordinates)
class DroSerializer : public ITelemetrySerializer {
public:
  String serialize(const MachineModel& model, uint8_t domainMask) override {
    return buildStatus(model);
  }
  bool hasChanged(const MachineModel& model) override {
    return true;
  }
  static String buildStatus(const MachineModel& model);
};

// -----------------------------------------------------------------------------
// 3. Translator Interface (Translates Inbound Wire Events -> Canonical Commands)
// -----------------------------------------------------------------------------
class ICommandTranslator {
public:
  virtual ~ICommandTranslator() = default;
  virtual bool translateAndDispatch(const char* payload, size_t length, String* reply) = 0;
};

// Default text translator directly delegates to canonical CommandDispatcher
class DefaultCommandTranslator : public ICommandTranslator {
public:
  bool translateAndDispatch(const char* payload, size_t length, String* reply) override {
    return CommandDispatcher::dispatch(payload, length, reply);
  }
};

// Canonical Command Translator (sanitizes line buffers, strips transport framing, routes @-actions and CMD.*)
class CanonicalCommandTranslator : public ICommandTranslator {
private:
  uint8_t _clientId;
public:
  CanonicalCommandTranslator(uint8_t clientId = 0) : _clientId(clientId) {}
  void setClientId(uint8_t clientId) { _clientId = clientId; }
  bool translateAndDispatch(const char* payload, size_t length, String* reply) override;
};

// -----------------------------------------------------------------------------
// 4. Client Endpoint (Composite: Transport + Serializer + Translator)
// -----------------------------------------------------------------------------
class ClientEndpoint {
protected:
  ITransport* _transport;
  ITelemetrySerializer* _serializer;
  ICommandTranslator* _translator;

public:
  ClientEndpoint(ITransport* transport, ITelemetrySerializer* serializer, ICommandTranslator* translator)
    : _transport(transport), _serializer(serializer), _translator(translator) {}

  virtual ~ClientEndpoint() = default;

  ITransport* getTransport() const { return _transport; }
  ITelemetrySerializer* getSerializer() const { return _serializer; }
  ICommandTranslator* getTranslator() const { return _translator; }

  // Processes incoming raw payload from transport through translator
  virtual bool handleIncoming(const char* payload, size_t length) {
    if (!_translator) return false;
    String reply = "";
    if (_translator->translateAndDispatch(payload, length, &reply)) {
      if (reply.length() > 0 && _transport && _transport->isConnected()) {
        _transport->sendText(reply);
      }
      return true;
    }
    return false;
  }

  // Serializes and transmits machine model over transport
  virtual bool broadcast(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) {
    if (!_transport || !_serializer || !_transport->isConnected()) return false;
    String text = _serializer->serialize(model, domainMask);
    if (text.length() > 0) {
      return _transport->sendText(text);
    }
    return false;
  }

  // Transmits only if state has changed since last transmission
  virtual bool broadcastIfChanged(const MachineModel& model, uint8_t domainMask = DOMAIN_ALL) {
    if (!_transport || !_serializer || !_transport->isConnected()) return false;
    if (_serializer->hasChanged(model)) {
      return broadcast(model, domainMask);
    }
    return false;
  }
};
