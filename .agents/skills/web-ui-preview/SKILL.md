---
name: web-ui-preview
description: >-
  Previews, serves, and verifies the NanoELS Touch DRO Web Client (clients/web_ui/).
  Use this skill when modifying HTML, CSS, or JS in clients/web_ui/ to inspect layout,
  verify console errors, check 800x480 touch ergonomics, or validate telemetry against live hardware or the emulator.
---

# Web UI Preview & Verification Runbook

This skill outlines how to preview and verify the NanoELS Web UI client application residing in `clients/web_ui/`.

---

## 1. Choosing the Verification Mode

Always determine whether testing is targeting **Live Hardware (Preferred)** or the **Offline Emulator (Auxiliary)**:

Mode | When to Use | Target URL | Verification Scope
:--- | :--- | :--- | :---
**Live Hardware** *(Preferred)* | Validating live machine telemetry, WebSocket streams, settings sync, or mode transitions. | `http://<lathe-ip>/` (or proxy to live ESP32-S3) | Full end-to-end integration with physical controller.
**Emulator** *(Auxiliary)* | Rapid layout checks, CSS styling, keypad interaction, and static DOM geometry without lathe powered on. | `http://localhost:8080/lathe_dro_nanoels_controller.html?mock=true` | Client-side visual rendering and component events only.

---

## 2. Serving the Local Web Client (Emulator Mode)

If testing offline layout or mock behavior:

1. **Start HTTP Server:**
   ```bash
   python3 -m http.server 8080 --directory clients/web_ui
   ```
2. **Launch Browser Subagent with Strict Viewport:**
   * Viewport size: **800×480** (standard physical touch display resolution).
   * URL: `http://localhost:8080/lathe_dro_nanoels_controller.html`

---

## 3. Mandatory Console-First Check (Fail-Fast)

Per [`.agents/rules/browser_verification.md`](file:///home/emil/code/nanoels/.agents/rules/browser_verification.md):

1. **Step 1:** Immediately read browser console logs.
2. If ANY uncaught exception (`TypeError`, syntax error, missing resource) is detected:
   * **STOP IMMEDIATELY.**
   * Do not hunt for DOM elements or attempt clicks.
   * Return the exact error, file, and line number so the script can be repaired.
3. If console is clean:
   * Verify absence of window scrollbars (layout must strictly fit within 800×480).
   * Take a full-viewport screenshot.

---

## 4. Live Hardware Verification Workflow

When live hardware is available:

1. **Ensure Firmware Uploaded:**
   * Verify the operator has uploaded the latest firmware and the ESP32-S3 has connected to the network.
2. **Physical Stimulus Requests:**
   * If validating motion-dependent UI cards:
     * **Spindle RPM:** Ask the operator: *"Please rotate the lathe spindle chuck by hand to verify encoder pulse readout."*
     * **MPG Stepping:** Ask the operator: *"Please step the MPG handwheel in +Z / -X."*
     * **Limit Contact:** Ask the operator: *"Please verify asymmetric jog button lock styling when approaching a soft limit."*
3. **Capture Telemetry Confirmation:**
   * Verify real-time updates in the DRO readouts (#spindleBox, Z/X position indicators).

---

## 5. Emulator Synchronization Invariant

> [!WARNING]
> If any wire protocol message (`?get=state`, WebSocket frame), lathe mode ID, or machine profile schema is added or modified in firmware or client code:
> **You MUST update `clients/web_ui/nanoels-emulator.js` concurrently.**
> Never allow the emulator mock engine to return obsolete state packets.
