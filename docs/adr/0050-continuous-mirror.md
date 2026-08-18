# ADR 0050 — Continuous mirror: reusing the library unchanged, verified live

**Status:** Accepted — implemented, verified on real hardware
**Deciders:** firmware owner
**Related:** builds on ADR 0049 (screenshot proof); confirms ADR 0048's
prediction that `IFrameSource`/`ReadbackFrameSource` need no Tab5-specific
code

## Context

ADR 0049 proved `readRect()` is pixel-correct on Tab5. The next step in the
user's stated sequence (replay the ADV work: pins → screenshot → mirror →
keyboard → dashboard) is continuous mirroring — the budgeted per-tile scan,
dirty-tile change detection, RLE codec, and WebSocket push that
`lib/CardputerMirror` (ADR 0002/0038) already implements, reused verbatim
rather than rebuilt.

## Decision

**Geometry (`CardputerMirror.h`):** `kScreenW=1280, kScreenH=720,
kTileCols=16, kTileRows=9` → 80×80 px tiles, 144 tiles total. Both divide
exactly (1280/16=80, 720/9=80), matching the header's existing convention.
This repo targets Tab5 only (per [[the fork decision, ADR 0048]]), so unlike
upstream cardputer-adv-mirror/launcher-adv-mirror there is no compile-time
board switch to preserve here — the constants were simply changed in place.

**No changes to `Mirror`, `ReadbackFrameSource`, `Codec.h`, or the wire
protocol.** `Mirror::begin(cfg)` with `cfg.manageWifi = false` (same
pattern the Cardputer's own `main.cpp` used) and `Mirror::update()` in
`loop()` is the full integration — exactly the two-line contract the
library's own header comment documents.

**Test content (`src/main_tab5_mirror.cpp`):** the same checkerboard from
ADR 0049, plus a small bouncing white box (24×24 px, redrawn ~30 fps) so the
milestone exercises *continuous* dirty-tile detection, not just the
one-time full-frame push `WS_EVT_CONNECT` triggers. The box's erase step
restores the checkerboard pixel-by-pixel via a shared `checkerColorAt(x,y)`
helper rather than painting flat black — 24 px doesn't align with the
checkerboard's 40 px cell, so a flat-color erase would have visibly
corrupted the background.

**Starting budget:** `cfg.budgetUs = 8000`, deliberately conservative and
unmeasured going in — Tab5's `readRect()` cost per tile was unknown (no
SPI bus to reason about, unlike the Cardputer ADV's 4.05 ms/tile over 3-wire
SIO). Real numbers are now in from hardware (see Consequences).

## Consequences

**Positive**

- Confirms ADR 0048's central claim: the `IFrameSource` abstraction really
  is board-agnostic. Zero code changes to the frame source, dirty-tile
  scheduler, codec, or WebSocket layer were needed for a completely
  different SoC/panel/bus architecture — only the geometry constants moved.
- `readback begin=1 self-test 100%` on real hardware — Tab5's RAM-backed
  readback is exactly as reliable as ADR 0048 predicted, no artifacts to
  chase the way ADR 0002/0006 had to for the Cardputer ADV's SPI path.
- Verified live and stable: visible simultaneously on the physical display
  and the browser dashboard, in sync, over several hours of uptime with no
  heap or PSRAM drift (`ESP.getFreeHeap()`/`getFreePsram()` flat in the
  heartbeat log).
- Observed 2-5 tiles/s pushed for a small moving object at this tile
  granularity — expected, not a red flag: an 80×80 tile only goes dirty
  when the 24 px box's *edge* crosses a tile boundary, so most of its
  motion doesn't touch a new tile. A denser tile grid would report a
  bigger number for the same visual motion; this isn't a bandwidth ceiling
  measurement yet, just confirmation the change-detection path fires
  correctly and infrequently for genuinely small deltas.

**Negative / open**

- `budgetUs=8000` is still a guess, not a measured-and-tuned value — real
  per-tile timing on Tab5 (readRect cost at 6,400 px/tile vs. the
  Cardputer's 2,700) hasn't been isolated from WiFi/encode overhead yet.
  Fine for this milestone's test content; will matter once real UI content
  with larger simultaneous dirty regions is mirrored (e.g. a full-screen
  redraw, which is common for menu transitions).
- The browser page is still the Cardputer ADV's own case-mockup chrome
  (CARDPUTER ADV label, device photo, etc.) around a correctly-sized
  canvas — cosmetically wrong for Tab5, explicitly out of scope for this
  milestone (dashboard-visuals work comes after the keyboard milestone).
- `main_tab5_mirror.cpp` duplicates the WiFi bring-up/placeholder-guard
  boilerplate from `main_tab5_screenshot.cpp` verbatim. Acceptable for now
  (each milestone sketch has so far been effectively disposable, per ADR
  0049's own framing) but worth factoring out if a third standalone sketch
  needs the same bring-up.

## Alternatives considered

None new here — ADR 0049 already settled the platform/build questions this
milestone depends on. The only real decision was the tile grid, and 80×80
was chosen for clean divisibility, not measured against alternatives; that
remains open for tuning once real UI content (not a test pattern) needs to
mirror well.
