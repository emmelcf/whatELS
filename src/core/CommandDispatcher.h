#pragma once

#include <Arduino.h>
#include "CommandResult.h"
#include "../motion/modes/IELSMode.h"

namespace whatels {
namespace core {

class CommandDispatcher {
public:
  // Dispatches an incoming canonical command string from any transport.
  // Returns strongly typed CommandResult. Zero transport or wire string formatting.
  static CommandResult dispatch(const char* payload, size_t length, uint8_t clientId = 255);
};

} // namespace core
} // namespace whatels
