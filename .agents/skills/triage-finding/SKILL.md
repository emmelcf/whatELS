---
name: triage-finding
description: Capture out-of-scope bugs, tech debt, or feature ideas as structured GitHub Issues without derailing active work.
disable-model-invocation: true
---

# Triage Finding Skill (Zero-Distraction Issue Capture)

## Purpose
Prevent scope creep and drive-by hacks. When an out-of-scope bug, hardware quirk, protocol gap, or future feature is identified during a session, capture it rigorously as a GitHub Issue and return immediately to the active task.

## Workflow
1. **Classify the Finding:**
   - `type:bug` — Verified or suspected defect in `src/`, `clients/web_ui/`, or `tests/hil/`.
   - `type:tech-debt` — Architectural violation, coupling issue, or cleanup deferred to keep current diff atomic.
   - `type:feature-spec` — Future enhancement or mode capability requiring a `/grill-with-docs` session later.

2. **Select Scope Label:**
   - `scope:motion-kernel` — `src/motion/kernel` & ISR pulse/encoder timing
   - `scope:modes` — `src/motion/modes/*` (Gearbox, Threading, Turning, etc.)
   - `scope:webui` — `clients/web_ui/` & `data/www/`
   - `scope:hil-harness` — `tests/hil/*` Python suite

3. **Gather Evidence (Read-Only, < 30 seconds):**
   - Identify exact file paths and line numbers (e.g., `src/motion/modes/composite/UnifiedGearboxMode.cpp:L45`).
   - Identify the relevant architecture doc or ADR (`docs/...`).
   - Do **NOT** modify code to test a fix.

4. **Create the GitHub Issue via `gh` CLI:**
   Run `gh issue create` with structured Markdown:
   ```bash
   gh issue create \
     --title "<type>(<scope>): <concise technical summary>" \
     --label "<type-label>,<scope-label>" \
     --body "$(cat <<'EOF'
   ## Context & Discovery
   Discovered while working on active task.

   ## Affected Components
   - **Files:** `<path:line>`
   - **Architecture / Spec Reference:** `<docs/...>`

   ## Technical Description / Root-Cause Hypothesis
   <Detailed architectural explanation of the symptom, edge case, or state-machine gap>

   ## Proposed Verification Seam
   - **Target Suite:** `tests/hil/suites/test_<suite>.py` (or `test/test_motion_e2e.cpp`)
   - **Expected Invariant:** <What deterministic assertion should pass once resolved>
   EOF
   )"
   ```

5. **Resume Active Work:**
   Report the created GitHub Issue URL/number to the user in one sentence and immediately continue the original task.
