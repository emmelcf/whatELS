---
trigger: always_on
description: Physical lathe operator safety, coordinate frames of reference (Z/X), limit registers, and anti-trap jog recovery.
---

# Lathe Operator Safety & Motion Invariants

- **Operator Frame of Reference:**
  - $Z$ Carriage: `← Z CHUCK` ($Z−$) toward headstock; `→ Z TAIL` ($Z+$) toward tailstock.
  - $X$ Cross-Slide: `↑ TO CENTER` ($X−$) into cut / spindle centerline; `↓ RETRACT` ($X+$) away from cut.
  - Invariant: Up arrow (`↑`) ALWAYS means cutting into workpiece. Never invert.
- **Hardware Transparency & Limits (ADR 15):**
  - Exactly one hardware limit pair per axis (`z_min`/`z_max`, `x_min`/`x_max`). Zero software ghost limits.
  - Canned cycle shoulder limits persist in hardware registers until explicitly altered or cleared by operator.
- **Anti-Trap Jog Recovery:**
  - Controller silently clamps step pulses at limits (`delta = 0`).
  - Client infers contact (`distanceToStop == 0` or `pos == limit`).
  - Opposing jog direction (`→ Z TAIL` or `↓ RETRACT`) must NEVER be disabled.
- **Presentation Independence:**
  - Firmware canonical unit is metric radius deci-microns (`du` = 0.0001 mm).
  - Imperial ($25.4$) and diameter ($2\times$) conversions are strictly client presentation.
- **Spindle Watchdog:**
  - Slaved motion requires spindle pulses. Warn if active but RPM < 10; pause infeed on stall.
