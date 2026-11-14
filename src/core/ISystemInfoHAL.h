#pragma once

/**
 * @file ISystemInfoHAL.h
 * @brief Abstract system diagnostics and appliance vitals hardware abstraction layer.
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 * @see docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 * @see docs/testing/README.md
 */

#include <cstdint>
#if defined(ESP32) || defined(ARDUINO)
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

namespace whatels::core {

/**
 * @brief Abstract System Diagnostics and Appliance Vitals Hardware Abstraction Layer.
 * @details Conforms to ADR-0005 Pillar 1 (SYS namespace) and CANONICAL_COMMAND_REFERENCE.md Section 4.1.
 *          Decouples StandardCommandParser from bare-metal ESP32 registers and platform APIs.
 */
class ISystemInfoHAL {
public:
  virtual ~ISystemInfoHAL() = default;

  // Runtime Health Vitals (SYS.get=health)
  virtual uint32_t getUptimeSeconds() const = 0;
  virtual uint32_t getFreeHeapBytes() const = 0;
  virtual String getWifiStatus() const = 0;

  // Appliance & Silicon Architecture (SYS.get=info / SYS.get)
  virtual const char* getVersion() const = 0;
  virtual const char* getChipModel() const = 0;
  virtual uint32_t getChipRevision() const = 0;
  virtual uint32_t getFlashSizeBytes() const = 0;
  virtual const char* getFeatures() const = 0;
};

} // namespace whatels::core

