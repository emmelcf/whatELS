#pragma once

#include <Arduino.h>

namespace nanoels {
namespace core_client {

class ITransport {
public:
  virtual ~ITransport() = default;
  virtual bool isConnected() const = 0;
  virtual bool sendText(const char* text) = 0;
  virtual bool sendBytes(const uint8_t* buffer, size_t length) { (void)buffer; (void)length; return false; }
};

} // namespace core_client
} // namespace nanoels
