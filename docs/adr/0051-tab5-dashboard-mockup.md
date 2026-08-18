# ADR 0051 — Tab5 dashboard mockup: CSS-drawn, measured from real hardware

**Status:** Accepted — implemented, verified live in a real browser against real hardware
**Deciders:** firmware owner
**Related:** replaces the Cardputer ADV case mockup (ADR 0028/0030/0033/0035)
this repo inherited from the fork; builds on ADR 0050's mirror

## Context

With mirroring working (ADR 0050), the dashboard's browser page still wore
the Cardputer ADV's own case mockup — a photo-and-hand-measured-CSS
recreation of a completely different physical device. This ADR replaces it
with a Tab5-appropriate mockup: the tablet (screen mirror) docked above a
keyboard accessory, both CSS-drawn.

## Decision

**No product photo.** A first attempt used a real Tab5 product photo as the
`#dev` background, rotated so the camera sits on the right per the user's
explicit request. It didn't survive contact with the actual mirror canvas:
the measured screen rectangle (from busy on-screen diagram content in the
reference photo, not a clean bezel edge) didn't match the canvas's real
1280×720, so the canvas visibly overflowed the bezel. Fully CSS-drawn shapes
sidestep this failure mode by construction — no image to misalign against —
and dropped ~79KB of flash a device already tight on it (86% used).

**Structure, settled after several corrections:**
- `#tab5unit` — column layout (mobile-first default), `#dev` (tablet) then
  `#tab5kb` (keyboard) stacked, near-zero gap so they read as one docked
  unit rather than two floating panels. An earlier side-by-side attempt was
  wrong — traced to a hand-held photo shot in the wrong camera orientation
  (`A164_operate_01.jpg`, portrait shot of a landscape object); a properly
  oriented top-down photo of the real assembled unit confirmed stacked was
  right.
- `#dev` — white case, **three real layers** (not two): white case body →
  black bezel ring (`#devbezel`, where the camera lives, widened on the
  right for it) → screen (`#devscreen`, centred, symmetric margins). The
  first pass conflated bezel-color and screen-content into one element,
  leaving no distinct ring for the camera to sit in.
- `#tab5kb` — reuses the Cardputer ADV's own `.krow`/`.key`/`.lgd`/`.dome`
  two-part legend-strip-over-dome key system verbatim (only added 3 new
  accent-color classes: yellow, pink, plus reusing existing blue/orange/
  grey/dark). The Tab5 keyboard accessory's real physical key design turned
  out to be the *same* two-part legend+dome construction, just a different
  layout — a genuine reuse, not a coincidental fit.
- Strict 14×5 uniform grid, every key the same cell size — no wide space
  key, no odd 15-column row. An earlier attempt gave the space bar a
  double-width span because it looked wider in a photo; the explicit
  correction (and the real hardware photos) settled it: uniform 14×5.
- Corner radii are **asymmetric on purpose**: `#dev`'s top corners are
  rounded more than its bottom, `#tab5kb`'s bottom more than its top —
  matching a real dock seam (flat where two pieces mate, round on the
  outer edges). `#devbezel`'s radius is `#dev`'s radius minus its own inset
  margin at each corner, not an independently-chosen value — nesting
  rounded rects incorrectly is what caused the white margin to visibly
  "roll off" at the corners in an earlier pass.

**Measurements, all from real photos/hardware, not eyeballed:**
- Screen module aspect: **1.661:1** — row-brightness-profiled from a real
  top-down photo of the actual device, isolating the screen-only region
  (excludes the keyboard below it). Supersedes an earlier 1.6 from a studio
  photo's full silhouette (likely included part of the dock connector nub).
- Camera position: toward the top of the right-edge bezel band, not
  vertically centred — read directly off real photos of the hardware, not
  guessed.
- White case margin: 0.6%/1.0% (width/height) — cut down twice from an
  initial 7.14% (solved-for-exact-16:9, from the first, wrong approach) via
  explicit "too big" feedback each time. In the real photo this margin is
  barely distinguishable from the drop-shadow at the case edge.
- Missing key caught by the user, not by any measurement: the home row was
  short one key (`_=`, between `▲` and the enter icon) — the row comment
  documenting the intended layout had it right, the actual markup didn't.

**Workflow note:** this machine's own shell can't reach the device's IP
(confirmed earlier, ADR 0049 — different subnet, no route). Browser
automation (`mcp__claude-in-chrome__*`) turned out to reach it fine, since
it drives the user's actual Chrome instance rather than this shell's own
network stack — once discovered, this let verification happen directly
(navigate, screenshot, zoom into specific regions, read computed CSS via
`javascript_tool`) instead of relying on the user to describe every
iteration, which meaningfully sped up the later correction rounds.

## Consequences

**Positive**
- No image assets at all for the dashboard now — pure CSS, crisp at any
  zoom level, and flash usage dropped back from 92% to 86%.
- The keyboard mockup's visual system is a genuine reuse of existing,
  proven CSS (not a parallel implementation to maintain).
- Every non-obvious number in the mockup traces to either a real
  measurement or explicit user correction — nothing was eyeballed and left
  that way.

**Negative / open**
- The keyboard mockup is `aria-hidden` and has zero click-to-type wiring —
  purely decorative until the real Tab5 keyboard milestone (new
  `IInputSink` for the I2C accessory, addr `0x6D`) lands. At that point
  this markup will need real per-key coordinates/values, which don't exist
  yet.
- Bezel/margin numbers are tuned to *this* photo set and this specific
  screen content (a checkerboard test pattern) — worth a fresh look once
  real dashboard UI (not a test pattern) is the thing being mirrored.

## Alternatives considered

- **Photo-based mockup.** Tried first, rejected — see Decision above.
- **Side-by-side docking layout.** Tried based on a real photo, rejected
  once a better-oriented photo of the same hardware showed the real dock
  orientation is stacked, not side-by-side.
