#pragma once

/**
 * @file ClientContext.h
 * @brief Formal client capability and permission bitmask context.
 * @see docs/clients/ELS_CLIENT_ARCHITECTURE.md
 * @see docs/adr/0003-command-result-and-baseline-client-architecture.md
 */
#include <cstdint>

namespace whatels::core_client {

/**
 * @brief Formal client capability and permission bitmask.
 * @details Conforms to ELS_CLIENT_ARCHITECTURE.md Section 3.5.
 */
enum ClientPermission : uint32_t {
  PERM_NONE           = 0,
  PERM_READ_TELEMETRY = 1 << 0,  // Can observe state, read positions & settings
  PERM_JOG_PULSE      = 1 << 1,  // Can execute manual step jogs & continuous jogs
  PERM_MOTION_START   = 1 << 2,  // Can engage feeds and start cutting cycles
  PERM_SETTINGS_WRITE = 1 << 3,  // Can modify persistent calibration & settings
  PERM_SIMULATION     = 1 << 4,  // Can inject fake RPM & encoder steps
  PERM_SYSTEM_RESET   = 1 << 5,  // Can trigger reboot or factory defaults
  PERM_ALL            = 0xFFFFFFFF
};

/**
 * @brief Calling client session identity and permission mask.
 */
struct ClientContext {
  uint8_t  sessionId      = 255;
  uint32_t effectivePerms = PERM_ALL; // Granted permissions bitmask
  bool     holdsLock      = false;    // True if holding motion execution lock

  bool hasPermission(uint32_t perm) const {
    return (effectivePerms & perm) == perm;
  }
};

} // namespace whatels::core_client
