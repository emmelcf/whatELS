#pragma once

#include <Arduino.h>

/**
 * @file PresetService.h
 * @brief Authoritative machining preset catalog service for whatELS.
 * @details Implements the Preset Catalog specification defined in docs/ELS_PRESET_CATALOG.md.
 *          Resolves queries over ROM Flash PROGMEM standards and LittleFS user overrides.
 * @see docs/state/ELS_PRESET_CATALOG.md
 * @see docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md
 */
class PresetService {
public:
  /**
   * @brief Dispatches preset catalog queries.
   * @param payload Command string starting after "PRESET." (e.g. "discover", "get path=...")
   * @param length Length of payload string.
   * @param reply Output pointer to populate with response.
   * @return true if command was handled.
   */
  static bool dispatch(const char* payload, size_t length, String* reply);

  /**
   * @brief Discovers available domains or catalog files.
   * @param path Optional path filter (e.g. "" for root domains, "threads" for catalogs in threads).
   * @param reply Output pointer to populate with JSON response.
   */
  static bool discover(const char* path, String* reply);

  /**
   * @brief Retrieves catalog or item details with optional $select, $filter, and $metadata.
   * @param query Query parameter string (e.g. "path=threads/iso_metric $select=id,designation $filter=choice eq 1")
   * @param reply Output pointer to populate with JSON response.
   */
  static bool get(const char* query, String* reply);
};
