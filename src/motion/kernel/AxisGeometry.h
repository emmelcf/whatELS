#pragma once
#include <cstdint>

/**
 * @brief Physical geometry, gear pitch, and kinematic constraints of a lathe axis.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 2.3.
 */
struct AxisGeometry {
  int32_t screwPitchDu      = 20000; // Leadscrew lead in canonical deci-microns (e.g. 20000 du = 2.0000 mm)
  int32_t motorStepsPerRev  = 800;   // Steps per complete leadscrew revolution (e.g. 800)
  int32_t backlashSteps     = 0;     // Lost motion compensation in motor steps
  float   maxVelocityMmSec  = 25.0f; // Physical rapid travel limit in mm/sec
  float   maxAccelMmSec2    = 500.0f;// Safe acceleration limit in mm/sec^2
};
