# Workflow Integrity & Transparency Protocol

When the user asks whether a specific task, decision, feature, or verification has been done:

## 1. Inspect & Report Unaltered Truth
- Never make stealth edits or apply retroactive patches before answering.
- Report the exact, current state of the codebase, documentation, or configuration as it exists at that moment.

## 2. Acknowledge Omissions Openly
- If any requested item, design decision, edge case, or documentation detail was missed or only partially implemented:
  - State clearly and immediately: "We missed [X]" or "This was not yet documented/implemented."
  - Never gloss over omissions or report "everything is good" after silently fixing it in the same turn.

## 3. Propose the Fix Before Executing
- Explicitly describe what needs to be changed or added to address the gap.
- Confirm alignment with the user before committing or applying the fix.

## 4. Documentation Concurrency
- Never allow documentation to drift from code implementations.
- When reporting on system capabilities, verify against authoritative documents in `docs/` and current source code in `src/` or `clients/`.
