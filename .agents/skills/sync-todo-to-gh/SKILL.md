---
name: sync-todo-to-gh
description: Audit todo/TODO.md and todo/hil_rig/README.md, interactive-grill any ambiguous items, and migrate actionable items into structured GitHub Issues.
disable-model-invocation: true
---

# Sync Local TODOs to GitHub Issues

## Workflow
1. Read `todo/TODO.md` and `todo/hil_rig/README.md`, cross-referencing existing open issues via `gh issue list --limit 100`.
2. Present a numbered table of unmigrated items with proposed `type:*` and `scope:*` labels.
3. Ask the user which items to promote to GitHub Issues now vs. archive.
4. Create the approved GitHub Issues via `gh issue create` and update `todo/TODO.md` with the `#<issue-number>` links (or remove completed items).
