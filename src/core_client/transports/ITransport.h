#pragma once

/**
 * @file ITransport.h
 * @brief Pluggable byte/text transport interface for client adapters.
 * @see docs/adr/0004-pluggable-transports-and-webserial.md
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 */

#include <cstddef>
#include <cstdint>

namespace whatels {
namespace core_client {

class ITransport {
public:
  virtual ~ITransport() = default;
  virtual bool isConnected() const = 0;
  virtual bool sendText(const char* text) = 0;
  virtual bool sendBytes(const uint8_t* buffer, size_t length) { (void)buffer; (void)length; return false; }
};

} // namespace core_client
} // namespace whatels
