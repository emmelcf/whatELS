# NanoELS State, Properties & Persistence Subsystem

> **Location:** `docs/state/`  
> **Target Subsystems:** Subsystem 3 (Core State Model), Subsystem 4 (Properties & NVS Engine)

This directory contains the authoritative specifications for the NanoELS state models, properties hierarchy, and serialization mechanisms:

1. **[`ELS_CORE_STATE_MODEL.md`](./ELS_CORE_STATE_MODEL.md):**  
   - Canonical 4-domain state partition: Kinematics (`K`), Intent (`I`), Cycle (`C`), and Properties (`P`).
   - Bitmask-driven dirty change tracking for low-bandwidth 50 Hz telemetry broadcast.
   - Elimination of floating-point round-trip drift via canonical integer deci-microns (`du`) and authoring intents.

2. **[`ELS_PROPERTIES_AND_SETTINGS.md`](./ELS_PROPERTIES_AND_SETTINGS.md):**  
   - 4-tier property hierarchy: Machine (`machine.*`), Controller (`sys.*`), Workshop (`prefs.*`), and Client (`client.*`).
   - Dynamic default vs. sparse NVS override engine (zero flash wasted on defaults).
   - In-motion flash write lockout (`!isAnyAxisMoving()`) to prevent dual-core SPI flash cache freeze.

3. **[`ELS_PRESET_CATALOG.md`](./ELS_PRESET_CATALOG.md):**  
   - ROM-embedded factory machine configurations and lathe profiles.

4. **[`TIER2_STATE_MAPPING.md`](../archive/TIER2_STATE_MAPPING.md) (Archived):**  
   - Historical audit and variable mapping ledger used during the monolithic-to-modular migration.
