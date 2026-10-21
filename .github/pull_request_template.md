## Summary & Linked Issue
Closes #

## Architectural & Zero-Hack Checklist
- [ ] **No Point Hacks:** Root cause addressed; zero arbitrary delays or client-side kinematic workarounds.
- [ ] **ADR / Spec Compliance:** Adheres to `docs/WHATELS_MASTER_ARCHITECTURE.md` and `docs/adr/`.
- [ ] **Documentation Parity:** Corresponding `docs/protocol/`, `docs/modes/`, or `docs/clients/` files updated.
- [ ] **Asset Parity:** `scripts/generate_presets.py` / `scripts/embed_controller_assets.py` executed if `data/` or embedded UI changed.

## Verification Evidence
- **PlatformIO Build (`firmware-build-verify`):** RAM / Flash %:
- **Python HIL Suite (`tests/hil/run_tests.py`):** Pass / Fail summary:
- **Web UI Live Preview (`web-ui-preview`):** Verified against `lathe_dro_whatels_controller.html`
