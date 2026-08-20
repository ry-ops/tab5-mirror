# ADR 0054 — Physical panel was 180° rotated from the mirror the whole time

**Status:** Accepted — implemented, verified on real hardware
**Deciders:** firmware owner
**Related:** corrects the `setRotation(1)` value every prior milestone
(ADR 0049-0053) shipped with; the self-test those ADRs relied on could not
have caught this — see Consequences

## Context

The user reported the physical Tab5 screen looked wrong relative to the
browser mirror — colors and the checkerboard's diagonal line didn't line up
the way the web view did. Several rounds of verbal color-sequence
descriptions ("red white blue" instead of the expected "red green blue")
were internally inconsistent between messages and didn't cleanly match any
transform (rotation, mirror, channel swap) reasoned through by hand.

A photo the user sent was also genuinely misleading in one specific way:
the keyboard accessory appeared to the *right* of the screen in the photo,
but it physically docks *below* the screen — meaning the photo itself was
taken with the whole device rotated 90° in the photographer's hand
(confirmed independently by the keyboard's own printed, unrotatable key
legends appearing sideways in the same photo). That's a real, separate fact
about the photo, and it led to an incorrect conclusion: that accounting for
the photo's rotation and measuring corner pixel colors mathematically
showed the physical display was correct. **That conclusion was wrong.** The
photo being rotated 90° for the shot and the firmware having a real
orientation bug are two independent facts; confirming one doesn't rule out
the other, and this ADR's author (the assistant) conflated them.

## Decision

**Stop interpreting colors, photos, or verbal descriptions. Print literal,
unambiguous text.** `drawTestPattern()` in `main_tab5_mirror.cpp` now draws
`TOP-LEFT`, `TOP-RIGHT`, `BOTTOM-LEFT`, `BOTTOM-RIGHT` directly into their
named corners, on a solid black background box for contrast against the
checkerboard. This removes every source of ambiguity that had accumulated
across several failed verification attempts: no color-cycle counting, no
mental rotation math, no photo-angle correction — just read the word
printed in each corner.

With that on real hardware, the result was immediate and unambiguous: the
text labeled `TOP-LEFT` (drawn at buffer position near `(0,0)`) physically
rendered in the device's **bottom-right** corner. That's a genuine 180°
mismatch between the framebuffer and the panel's physical scanout — not a
photo artifact, not a misreading, a real bug.

**Root cause:** `M5.Display.setRotation(1)`, used unchanged since ADR 0049,
is one of the two rotation values that produce Tab5's landscape 1280×720
logical space (the other being `3`). Nothing about the *browser mirror*
was ever wrong — `ReadbackFrameSource`'s `readRect()` and the drawing calls
both route through M5GFX's `Panel_FrameBufferBase::_internal_rotation`
indexing, so the mirror was always self-consistent with whatever the
software *intended* to draw. What that indexing does **not** account for is
whether the physical DSI panel is actually mounted to match the
`cfg.offset_rotation` value M5GFX's `board_M5Tab5` autodetect hardcodes to
`0` (`M5GFX.cpp`, Tab5 panel config block). On this real unit, that
assumption is off by 180° — the fixed offset needed is effectively baked
into choosing `rotation=3` instead of `rotation=1` at the application
level, since `1` and `3` are Tab5's two landscape options and are exactly
180° apart.

**Fix:** `M5.Display.setRotation(3)`, one line, in `main_tab5_mirror.cpp`.
Confirmed via the corner-label test that the physical device now shows
`TOP-LEFT` in the actual top-left corner, with the web mirror unaffected
(still correct, as expected — it was never the problem).

## Consequences

**Positive**
- The physical Tab5 display is now genuinely correctly oriented, not just
  self-consistent with its own software assumptions.
- The corner-label test pattern is cheap (four small text draws) and stays
  in `drawTestPattern()` permanently — any future board/library/rotation
  change gets an immediate, unambiguous physical-vs-mirror sanity check for
  free, without needing a repeat of this investigation.

**Negative / open**
- Every prior milestone's "verified on real hardware" claim (ADR 0049-0053)
  was verified against the mirror and/or `selfTest()`, **not** against the
  panel's true physical orientation — because `selfTest()` reads back
  through the exact same `_internal_rotation` indexing used to write, it is
  structurally incapable of catching a physical-mounting-vs-buffer mismatch
  like this one. It's worth being explicit that 100% self-test has never
  meant "verified against a human looking at the device" for this project;
  it only ever meant "the buffer is internally consistent."
- Root cause is stated in terms of what fixes it (`rotation=3` vs `1`), not
  a fully traced explanation of *why* `board_M5Tab5`'s `offset_rotation=0`
  is wrong for this unit at the M5GFX level — that's vendored library code
  (`.pio/libdeps/*/M5GFX`), not this repo's, and out of scope to patch
  upstream here. Worth flagging back to the M5GFX project if this
  reproduces on other Tab5 units, not just this one.

## Lesson for this project's own verification process

The path to the real bug went through a wrong conclusion first. Verbal
color/rotation descriptions and photo interpretation — even careful,
pixel-measured photo interpretation — were not reliable enough to settle an
orientation question and led directly to telling the user they were wrong
when they were right. What actually worked was removing interpretation
entirely: print words, read words. That should be the **first** move next
time an orientation/mirroring question comes up on this project, not a
later resort after several rounds of trying to reason about colors or
photos.
