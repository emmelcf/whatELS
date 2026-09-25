#pragma once

#include <Arduino.h>

class RichClientAdapter {
public:
  static String buildTelemetry();
  static bool hasStateChanged();
};
