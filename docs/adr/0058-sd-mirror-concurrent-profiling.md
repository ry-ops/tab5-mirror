# ADR 0058 — SD + mirror concurrent-load profiling harness

**Status:** Proposed — instrumentation and bench firmware written, not yet
built or flashed (no PlatformIO toolchain / physical Tab5 available in the
session that wrote this). Numbers below are architectural reasoning, not
measurements. Flashing `env:tab5-mirror-sdbench` (and its `-task` sibling)
and pasting the heartbeat log back is the next step, not optional follow-up.

**Deciders:** firmware owner (reported symptom), this session (harness)
**Related:** ADR 0055 (microSD proof of life, SPI config this harness
reuses verbatim), ADR 0050 (continuous mirror), ADR 0002/0038/0039
(mirror library architecture)

## Context

Reported symptom: SD card read/write and the web mirror/streaming
"don't perform well running concurrently." The reporting context already
ruled out display/SD bus contention (Tab5's DSI display and SPI-mode SD
card are electrically independent — see CardputerMirror.h's header
comment and `Tab5Adapter::busLock()` returning `nullptr`) and pointed at
shared CPU/PSRAM bandwidth plus WiFi throughput via the ESP32-C6 as the
likely real bottleneck.

**That symptom has no code path to reproduce in this repo as it stood.**
SD (ADR 0055, `env:tab5-sdtest`) and the mirror (ADR 0050,
`env:tab5-mirror`) have always been separate firmware images —
`platformio.ini` builds each as its own env, and `main_tab5_mirror.cpp`
never touches `SD.h`. ADR 0055 says this explicitly: "This repo's own
`ReadbackFrameSource`/dashboard mirror doesn't touch the SD card at all."
So before any of this ADR's four tasks (instrument, evaluate async I/O,
evaluate resolution/compression/clock, judge whether external hardware is
warranted) can be answered with real data, something has to actually run
SD I/O and the mirror at the same time. That is what this ADR adds.

## Decision

### 1. Instrumentation

**`CardputerMirror.h`/`.cpp`**: opt-in per-stage timing behind
`CMIRROR_PROFILE` (undefined by default, so every existing env — including
this repo's and any downstream fork's, per the file's own "shared
verbatim with upstream cardputer-adv-mirror" comment — is byte-for-byte
unaffected). When defined, `Mirror::scanOneTile()`/`publishTile()`
accumulate `ProfileStats`: capture (`IFrameSource::fetchTile()`), encode
(`encodeTile()`), and publish (the `binaryAll()` enqueue) — count, total,
and max microseconds each. `profileStats()`/`profileReset()` expose it.

**One finding fell out of just adding this, before any hardware run**: the
"publish" stage is misleadingly named if you assume it's the WiFi send.
`s_ws->binaryAll()` copies the tile into ESPAsyncWebServer's
`makeSharedBuffer()` and hands it to AsyncTCP — the actual over-the-air
transmission happens later, on AsyncTCP's own FreeRTOS task, off
`loop()`'s call stack entirely. **The mirror's network path is already
non-blocking.** This matters directly for task item 2 ("making SD I/O
async/DMA-driven"): the asymmetry isn't mirror-vs-SD, it's specifically
*synchronous SD I/O* (Arduino's `SD`/`SPI` library blocks the calling task
for the full SPI transaction, confirmed by ADR 0055/0057's own
experience) next to an *already-async* WiFi send. Fixing that asymmetry
only requires moving SD off `loop()`'s stack — see harness mode 2 below —
not touching the mirror's network path at all.

**`src/main_tab5_mirror_sdbench.cpp`** (new `env:tab5-mirror-sdbench` /
`env:tab5-mirror-sdbench-task`): the actual concurrent-load harness.
Same WiFi/mirror bring-up as `main_tab5_mirror.cpp`, plus ADR 0055's exact
SD SPI config (pins 42/43/44/39, 25 MHz), running a continuous 64 KB
write+read benchmark timed with `micros()`. The SD workload toggles
OFF/ON every 15 s (`kSdPhaseMs`) so one boot captures "mirror alone" and
"mirror + SD" back to back, comparable in the same log without a
reflash — CardputerMirror's profile stats and the SD stats both reset at
each phase edge.

Two SD I/O modes, one file, chosen by `-DSD_BENCH_TASK`:
- **inline** (`env:tab5-mirror-sdbench`, default): SD write+read run
  directly in `loop()`, so a blocking SD SPI transaction delays the very
  next `CardputerMirror.update()` call by its own duration. Worst case,
  and what a naive integration would do.
- **task** (`env:tab5-mirror-sdbench-task`): SD write+read run on a
  dedicated FreeRTOS task pinned to the core `loop()` does *not* run on
  (`xTaskCreatePinnedToCore(..., 0)`; arduino-esp32 pins `loop()` to core
  1). No lock between the two — same reasoning as `busLock()` returning
  `nullptr` for the display: SD's SPI peripheral and the mirror's tile
  scan/encode/WS-enqueue touch no shared hardware register, only shared
  CPU time and memory-bus bandwidth, which the scheduler and cache
  coherency already handle. Comparing this env against the inline one
  directly answers "does decoupling SD off the main loop help" without
  needing the Arduino SD library to support real DMA/async I/O, which it
  doesn't.

Deliberately **not** a bus-arbitration mechanism, per the task's own
instruction — there is no lock to design because there is no shared bus.

### 2. Evaluating the three options (task item 2), pending real numbers

- **Async/DMA-driven SD I/O**: the *task* mode above is the practical
  version of this available today — genuine DMA-driven SD I/O would mean
  bypassing Arduino's synchronous `SD`/`SPI` wrapper for ESP-IDF's
  `spi_master` queued/async transaction API directly (same layer
  `SpiReadbackFrameSource` already uses for display readback, so the
  pattern exists in this codebase). Worth the extra complexity only if
  the inline-vs-task comparison shows the task mode *doesn't* fully
  recover mirror throughput during the SD-active phase — i.e., only if
  CPU contention (not just call-stack blocking) is the real limiter.
  That's exactly what the two envs are built to distinguish.
- **Reducing resolution/framerate or increasing compression during
  SD-heavy ops**: the codec (`Codec.h`) already picks RLE vs. RAW
  per-tile automatically; there's no JPEG stage to increase compression
  on (see "no external chipset" below — this is a tile/RLE pipeline, not
  a JPEG frame-capture pipeline, unlike the task description's framing).
  A real lever that *does* exist: `Config::budgetUs` (currently 8000,
  `main_tab5_mirror.cpp`) caps how much of each `loop()` iteration the
  mirror spends scanning tiles — lowering it during the SD-active phase
  would trade mirror throughput for SD headroom without touching
  resolution. Whether that's needed depends on whether task-mode SD
  already removes the contention.
- **Raising the 25 MHz SD SPI clock**: `main_tab5_mirror_sdbench.cpp`
  makes this a single named constant (`kSdClockHz`) specifically so it's
  a one-line change to test once real hardware is available. ADR 0055
  never established 25 MHz as a ceiling, only as M5Stack's own documented
  value that's *known* to work — "not investigated further since 25MHz
  ... is sufficient" (ADR 0055's own "Negative/open" section). ESP32-P4's
  SPI peripheral and most SDHC cards support well above that; 40 MHz is a
  reasonable next data point. This can't be answered without flashing.

### 3. External chipset — not warranted, on current evidence

The task asks explicitly whether offloading encode or storage I/O to
dedicated hardware is warranted. Answering that *without* profiling data
would be exactly the mistake task item 4 warns against, but the
architecture gives a strong prior against it:

- There is no image/video encode stage here to offload. The mirror sends
  RLE-over-RGB565 tiles (`Codec.h`), not JPEG frames — "encode" in this
  codebase is a per-tile run-length pass over at most 6,400 pixels
  (80x80), not a codec workload a P4-class dual-core chip should struggle
  with.
- SD at 25 MHz SPI is bit-rate-capped around 3 MB/s even before overhead
  — well under what a single SPI peripheral and DMA-capable ESP32-P4
  should sustain without external help, and 40 MHz+ (untested here, see
  above) raises that ceiling further before any hardware-offload
  discussion is relevant.
- WiFi 6 via the ESP32-C6 is a separate SDIO-linked coprocessor already
  — it *is* the "dedicated hardware" for network offload, and the mirror
  already hands it work asynchronously (finding above).

None of that is a substitute for the actual heartbeat numbers. If, once
flashed, the task-mode SD env still shows mirror tiles/sec collapsing
during the SD-active phase (not just the inline env), that would be the
first real signal worth revisiting this answer over — not before.

## Consequences

**Positive**
- The reported symptom now has a concrete, flashable reproduction with
  per-stage timing on both sides, instead of remaining a hypothesis.
- `CMIRROR_PROFILE` is available to any future milestone in this repo (or
  upstream) with zero cost when unused.
- `kSdClockHz` and `Config::budgetUs` are both already one-line levers for
  the two tunables task item 2 asks about; no further plumbing needed to
  try them once real numbers justify it.

**Negative / open**
- **Not built, not flashed, no measurements.** This session had no
  PlatformIO toolchain and no physical Tab5. A `pio run -e
  tab5-mirror-sdbench` was attempted for build verification; it got as
  far as fetching the Espressif toolchain and framework from GitHub, then
  blocked on `registry.platformio.org` / `api.registry.platformio.org`
  being denied by this sandbox's network policy (confirmed via the proxy
  status endpoint as a deliberate 403, not a transient failure) — a
  sandbox limitation, not a defect in this code. Neither new env has been
  compiler-verified; both were written to closely mirror
  `main_tab5_mirror.cpp` and `main_tab5_sdtest.cpp`'s already-proven
  boilerplate, with only the bench-specific logic genuinely new.
- Every conclusion in "Evaluating the three options" and "External
  chipset" above is reasoning from the architecture, explicitly not
  measurement. Flashing both `tab5-mirror-sdbench` and
  `tab5-mirror-sdbench-task` and reading back the heartbeat log (SD
  write/read ms and KB/s, mirror capture/encode/publish avg+max us,
  tiles/sec, each phase-labeled) is what turns this from Proposed to
  Accepted.
- The 64 KB write+read benchmark is a synthetic, repeated-file workload
  (`/bench.bin`, `SD.remove()` before every write) — representative of
  "save a captured frame / log chunk periodically," not a full
  sequential-throughput card benchmark or a many-small-files workload.
  If the real usage pattern differs, the chunk size/pattern in
  `main_tab5_mirror_sdbench.cpp` is where to change it.
