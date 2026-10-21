---
name: grill-with-docs
description: Relentless interview to sharpen a plan or design, updating ADRs and glossary in real time.
disable-model-invocation: true
---

# Grill With Docs

Read and execute the interview and domain-modeling procedures from:
- [grilling](../grilling/SKILL.md) (design tree, question structure)
- [domain-modeling](../domain-modeling/SKILL.md) (glossary and ADR rules)

## whatELS Cadence & Mandatory Reads
- **Frontier Batch Rounds:** Ask 3–5 independent numbered questions (`❓`) per round with recommended options (`➡️`). Word each so "yes" accepts the recommendation.
- **Fact-Finding First:** Inspect code and active docs (`WHATELS_MASTER_ARCHITECTURE.md`, `docs/adr/`, `CANONICAL_COMMAND_REFERENCE.md`) before asking. Never ask what the repo can answer.
- **Real-Time Doc Persistence:** Record domain terms in `docs/GLOSSARY.md` and irreversible architectural trade-offs in `docs/adr/NNNN-<slug>.md`.
- **Live Preview Context:** Changes to `clients/web_ui/` target 800×480 Touch DRO tested via `web-ui-preview`.
- **Next Stage Gate:** Once the user confirms alignment, proceed to `/to-spec` to publish the specification with explicit HIL test seams.
