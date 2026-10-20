---
name: web-ui-preview
description: >-
  Previews and verifies the whatELS Touch DRO Web Client (clients/web_ui/).
  Use when modifying HTML, CSS, or JS in clients/web_ui/ to inspect layout,
  verify console errors, or validate telemetry against live hardware or emulator.
---

# Web UI Preview & Verification Runbook

Pre-requisite: Review [docs/clients/web_ui/ERGONOMICS.md](../../../docs/clients/web_ui/ERGONOMICS.md) for 800×480 layout and touch invariants.

## 1. Serving & Backends (IDE Live Preview Port 3000)
- **Live Controller (Preferred):** `http://127.0.0.1:3000/clients/web_ui/lathe_dro_whatels_controller.html` (connects to `ws://whatels.local:81`).
- **Emulator (Auxiliary):** Append `?mock=true` to URL or toggle in Dev Console modal.
- **Availability Check:** If port 3000 is unreachable, prompt operator to start the Live Preview extension.

## 2. Firmware Flashing Gate
- If changes involve C++ code in `src/`, compile and flash first:
  ```bash
  pio run -e esp32s3 && pio run -e esp32s3 -t upload
  ```
- Wait 5–10s for controller reboot and Wi-Fi reconnection (`whatels.local`) before testing.

## 3. Browser Subagent Execution
- **Strict Viewport:** `800×480` (standard physical touch screen).
- **Console-First Fail-Fast:** Read console logs immediately. If ANY uncaught exception or fetch failure occurs, STOP immediately and report the error/file/line. Do not hunt for missing DOM elements.
- **Visual Check:** Verify zero window scrollbars and capture a full-viewport screenshot.

## 4. Emulator Sync Invariant
- Whenever wire protocol (`?get=state`, WebSocket packets) or state schemas change, update `clients/web_ui/whatels-emulator.js` concurrently to prevent emulator drift.
