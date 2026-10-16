#pragma once
#include <cstdint>

#ifndef HAVE_AXIS_ID
#define HAVE_AXIS_ID
enum AxisId : uint8_t {
  AXIS_Z = 0,  // Longitudinal carriage leadscrew
  AXIS_X = 1   // Transverse cross-slide leadscrew
};
#endif
