#pragma once

#include <Arduino.h>

// --- Authoritative Operator Intent Models ---
enum ThreadFormat {
  THREAD_METRIC = 0,
  THREAD_TPI    = 1,
  THREAD_INCH   = 2
};

struct ThreadSetting {
  ThreadFormat format;
  float authorPitch; // Exact operator input: e.g. 1.5 (mm), 13.0 (tpi), or 0.050 (in)
  long dupr;         // Kinematic integer pitch in deci-microns per rev
};

enum TaperModeType {
  TAPER_RATIO  = 0, // 1:k (authorVal = k, e.g. 3.5)
  TAPER_ANGLE  = 1, // Half-angle in degrees (authorVal = angleDeg, e.g. 1.432)
  TAPER_PRESET = 2  // Standard preset (Morse, etc.)
};

struct TaperSetting {
  TaperModeType modeType;
  float authorVal;   // The exact value entered
  float coneRatio;   // Derived kinematic diametral ratio: tan(angle) or 1.0/(2k)
  int presetId;      // Preset index if applicable
};

