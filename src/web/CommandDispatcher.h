#pragma once

#include <Arduino.h>

class CommandDispatcher {
public:
  // Dispatches an incoming command line from any transport.
  // Returns true if command was recognized and handled.
  // Optional reply string can be populated if caller wants an immediate response.
  // Optional clientId identifies the requesting socket (e.g. for unicast response tracking).
  static bool dispatch(const char* payload, size_t length, String* reply = nullptr, uint8_t clientId = 255);
};
