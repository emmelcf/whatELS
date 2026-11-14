#pragma once

#include <cstddef>
#include "core/PlatformCompat.h"

/**
 * @file PresetService.h
 * @brief Authoritative unified machining catalog service for whatELS.
 * @details Implements the Preset Catalog specification defined in docs/state/ELS_PRESET_CATALOG.md.
 *          Resolves OData-style queries across ROM Flash PROGMEM standards and LittleFS user
 *          overrides for profiles, threads, and tapers, and persists custom user entries.
 * @see docs/state/ELS_PRESET_CATALOG.md
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 */
class PresetService {
public:
  /**
   * @brief Discovers available domains or catalog files within a domain.
   * @param path Optional path filter (nullptr/"" for root domains, "threads" for catalogs in threads).
   * @param reply Output pointer to populate with JSON response.
   */
  [[nodiscard]] static bool discover(const char* path, String* reply);

  /**
   * @brief Retrieves catalog or item details with optional $select, $filter, and $metadata.
   * @param query Query parameter string (e.g. "path=threads/iso_metric&$select=id,designation&$filter=choice eq 1").
   * @param reply Output pointer to populate with JSON response.
   */
  [[nodiscard]] static bool get(const char* query, String* reply);

  /**
   * @brief Saves or updates a custom user catalog entry under LittleFS /presets/user/<domain>/.
   * @param jsonPayload JSON object containing at minimum "domain" and "id" (optional "catalog").
   * @param reply Output pointer populated with saved entry id on success, or error reason on failure.
   */
  [[nodiscard]] static bool save(const char* jsonPayload, String* reply);

  /**
   * @brief Deletes a custom user catalog entry from LittleFS /presets/user/<domain>/.
   * @details Protects ROM entries, active machine profile, and default machine profile.
   * @param jsonPayload JSON object containing "domain" and "id" (optional "catalog").
   * @param reply Output pointer populated with deleted entry id on success, or error reason on failure.
   */
  [[nodiscard]] static bool remove(const char* jsonPayload, String* reply);
};
