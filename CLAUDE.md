# CLAUDE.md — tab5-mirror

> No vibe coding here. Hardcore only.

This file is a **draft for review**, not yet wired to any hook — nothing
below is mechanically enforced yet. It's the discipline written down so it
can be turned into real gates later. Treat every rule as binding on
yourself (the agent) even before enforcement exists; the point of writing
it down is to stop relying on remembering to be disciplined.

## Why this exists

Two things converged: an outside post on banning agents from writing code
"from memory," and this repo's own `docs/adr/0054-physical-panel-180-rotation-fix.md`,
which caught the same failure mode live — a wrong conclusion reached by
*interpreting* evidence (photos, color descriptions, rotation math) instead
of reading unambiguous ground truth. That ADR's own closing lesson is the
seed of this file: interpretation is not verification, and that should be
the first move next time, not a later resort.

## The five failure modes ("vibe coding," defined)

Not a vibe — five specific, checkable behaviors. Each has a gate.

1. **Confident claim, no evidence.** Saying "done," "fixed," or "verified"
   over code that wasn't actually built, flashed, or run this session.
2. **Reasoning from memory instead of source.** Proposing an API, pattern,
   or hardware fact because it feels right, not because something was
   actually opened and read this session.
3. **Verifying by interpretation instead of ground truth.** The subtle one
   — it *feels* like verification. ADR 0054: measuring pixel colors in a
   photo and doing rotation math concluded "correct," when the real answer
   was one unambiguous check away (print `TOP-LEFT`, read `TOP-LEFT` on
   the actual device).
4. **Silent scope creep.** Touching files or layers beyond what the task
   asked for, especially "while I was in there anyway."
5. **Overclaiming what a passing check actually proved.** A green check
   that doesn't test the real claim is still vibe coding. `selfTest()` in
   this repo has only ever proven the framebuffer is internally
   consistent — never that the physical panel is correctly oriented.
   State what a check does and doesn't cover every time it's cited.

## The gates

| # | Kernel question (ask before acting) | This project's answer | Status |
|---|---|---|---|
| 1 | What command/artifact proves this claim, and did it run this session? | `pio run` (build) + flash to the real device + observe the actual screen/serial output | Written rule only — no hook yet |
| 2 | Where did this API/fact come from? | Ladder: `docs/adr/README.md` hardware-facts table → vendored library source under `.pio/libdeps/*` → upstream project docs as last resort | Written rule only |
| 3 | Is this evidence ground-truth or inferred? | Printed literal text / direct serial/log output beats photos, color descriptions, or mental rotation/geometry math — always. If verification would rely on interpreting an image or a verbal description, stop and find an unambiguous check instead (cf. ADR 0054). | Written rule only |
| 4 | What files/layers did this touch vs. what was asked? | Diff should match the stated task; anything extra gets called out explicitly, not folded in silently | Written rule only |
| 5 | What does this check actually prove, and what does it not? | State it inline — e.g. "self-test passed (proves buffer consistency only, not physical orientation)" | Written rule only |

## Decision log discipline

Before proposing an approach, check `docs/adr/README.md` for a prior
decision that already covers it. Cite the ADR (accepted, proposed, or
superseded) or explicitly note none applies. Don't re-propose something
already tried and superseded (e.g. the CSS-label vs. photo-label history
in ADR 0034/0035) without saying so.

When a change is significant enough to warrant one, write a new ADR
following the existing numbering and format (Context / Decision /
Consequences, positive and negative/open). If it corrects or supersedes an
earlier one, say so in both files and update the README index/status
column.

## Source ladder for external facts (hardware, libraries)

1. This repo's own `docs/adr/README.md` "Hardware facts" table (already
   verified, cited to `file:line` — check here first, it's usually
   cheapest and already correct).
2. The actual vendored source in `.pio/libdeps/*` for the board/library
   version this project uses — not general Arduino/ESP32 knowledge.
3. Upstream docs/datasheets only if 1 and 2 don't resolve it, and note
   that you went this far down the ladder.

"This is usually how it's done" / "I recall the API being X" is never an
acceptable citation.

## Proving "done"

Nothing is done on the strength of a diff reading correctly. Minimum bar:

- It builds (`pio run`), and the build log is clean, not just non-fatal.
- It's flashed to real hardware when the change touches anything a
  simulator/self-test can't fully validate (display orientation, I2C
  keyboard behavior, WiFi, SPI timing) — see ADR 0054 and 0052 for why
  this repo specifically cannot trust self-test alone for physical
  correctness.
- The verification method is ground-truth (rule 3 above), and what it
  does/doesn't prove is stated alongside the claim (rule 5).

## Scope discipline

State the files a change is expected to touch before making it, when the
task is non-trivial. If something outside that set turns out to need a
change, say so explicitly rather than including it silently in the diff.

## Open item — mechanism layer

This file currently only encodes discipline as prose (a "kernel + adapter"
without a "mechanism"). The next step, not yet done: a `settings.json`
Stop-hook (via the `update-config` skill) that blocks a turn from ending
on "done"/"fixed"/"verified" language unless a build/flash command
actually ran this session. That hook can check *that* something ran; it
can't judge *whether the verification was ground-truth or interpreted* —
that half stays on the rules above and on honest self-report.
