# ADR 0049 — Screenshot before mirror: platform line resolved, smallest first proof

**Status:** Accepted — implemented, verified on real hardware
**Deciders:** firmware owner
**Related:** resolves the platform-line question ADR 0048 left open; sets up
the mirroring milestone (ADR 0002 equivalent) that follows

## Context

Same sequencing as the original Cardputer ADV work: identify real pins/panel
facts (ADR 0048), get a single verified screenshot off the hardware, *then*
build continuous mirroring, *then* input, *then* the dashboard site. A
screenshot is the smallest possible proof that `readRect()` on this SoC
produces correct pixels — the Tab5 equivalent of ADR 0002's boot self-test,
except here it's a real captured image, not a percent-match number, because
unlike the Cardputer ADV there's no 3-wire SIO uncertainty to quantify (see
ADR 0048 — `Panel_DSI` reads from RAM, not SPI).

Two things had to be verified against live sources before writing any code,
not assumed:

**1. PlatformIO platform line.** No dedicated M5Stack Tab5 board profile
exists in `pioarduino/platform-espressif32` as of this check (`boards/`
directory contains only `esp32-p4-evboard.json`, `esp32-p4.json`, and their
`_r3` revision-3 variants — no `tab5`/`m5tab5` file). `esp32-p4-evboard.json`
already carries the right defaults for our purposes: `mcu=esp32p4`,
`flash=16MB`, `flash_mode=qio`, `f_cpu=360MHz`, and a PSRAM frequency of
200MHz baked in via `-DBOARD_HAS_PSRAM` — matching ADR 0048's finding that
Tab5 *requires* 200MHz PSRAM. Latest platform release is `55.03.311`
("fix p4 rev3"), built on arduino-esp32 3.3.9 — comfortably clear of the
3.3.0 PSRAM-at-200MHz bug ADR 0048 flagged, and clear of the ≥3.2.2 floor
M5GFX's ST7123 support needs.

**2. Library versions.** `M5Unified` 0.2.20 and `M5GFX` 0.2.27 are the latest
tagged releases (both published today) — both comfortably above the ≥0.2.8 /
≥0.2.11 floor for ST7123 (post-Oct-2025 Tab5 units) support found in ADR
0048's research. Pinning to these tags, not floating on `master`, matching
this repo's existing convention (`ESPAsyncWebServer@^3.12.0`).

## Decision

**Platform (`platformio.ini`):**
```ini
[env:tab5]
platform  = https://github.com/pioarduino/platform-espressif32.git#55.03.311
board     = esp32-p4-evboard
framework = arduino
board_build.mcu = esp32p4
board_build.flash_mode = qio
board_upload.flash_size = 16MB
monitor_speed = 115200
upload_speed  = 1500000

build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1

lib_deps =
    m5stack/M5Unified@^0.2.20
    m5stack/M5GFX@^0.2.27
    esp32async/ESPAsyncWebServer@^3.12.0
```
No `m5stack/M5Cardputer` — that library targets the Cardputer specifically
and has no reason to be on the Tab5 build at all.

**Milestone (`src/main_tab5_screenshot.cpp`, standalone):** boot M5Unified
generically (autodetects `board_M5Tab5`), call
`WiFi.setPins(12,13,11,10,9,8,15)` (ADR 0048's verified SDIO override)
*before* `WiFi.begin()`, draw a known test pattern across the full
1280x720 logical screen, and serve exactly one full-resolution screenshot as
a BMP over HTTP on request — `GET /screenshot.bmp`. RGB565→RGB888 conversion
happens once, on request, not per-frame, so a naive per-pixel loop is fine
here even though it would never survive the mirroring milestone's frame-rate
budget.

Deliberately **does not** touch `lib/CardputerMirror`, `menu.cpp`,
`keyinject.cpp`, or `wifi_manager*.cpp`:
- `menu.cpp`/`keyinject.cpp` are built around the Cardputer ADV's TCA8418
  matrix keyboard (`M5Cardputer.Keyboard.*`) — no Tab5 analogue exists yet
  (that's the keyboard-accessory milestone, after mirroring).
- `wifi_manager*.cpp`'s multi-profile/SoftAP-fallback logic is real,
  reusable work, but pulling it in now would make a WiFi failure and a
  readback failure indistinguishable in this first test. The screenshot
  milestone uses one profile, directly, with no fallback — if it fails, the
  cause is obvious.
- `lib/CardputerMirror`'s `ReadbackFrameSource`/`Mirror` classes are
  confirmed board-agnostic (ADR 0048) and will be wired in for the actual
  mirroring milestone next, once the screenshot proves `readRect()` is
  trustworthy here. No point routing this proof through the tile/CRC/budget
  machinery that ADR 0048 already flagged as needing rework for 1280x720 —
  that rework is validated *by* this screenshot, not a prerequisite to it.

`platformio.ini`'s old `[env:cardputer-adv]` section is replaced outright,
not kept alongside — this repo now targets Tab5 only; the Cardputer
`platformio.ini` remains available in the upstream repo if ever needed for
comparison.

## Consequences

**Positive**
- Smallest possible falsifiable claim: either the BMP a browser downloads
  matches the pattern drawn on-device, or it doesn't. No percentage to
  interpret, no tile math to get wrong first.
- Fully isolated from every Cardputer-specific file, so a build failure here
  can only be the Tab5 bring-up itself, not a dependency dragged in from the
  old keyboard/menu code.
- `lib/CardputerMirror` stays untouched, so ADR 0048's "should need zero code
  changes" claim about `ReadbackFrameSource` stays testable in isolation
  later, uncontaminated by this milestone's changes.

**Negative**
- Throwaway-ish: `main_tab5_screenshot.cpp` and its inline BMP writer get
  replaced once mirroring starts. Acceptable — the goal is proof, not
  reusable code.
- The naive per-pixel RGB565→RGB888 conversion is only viable because this
  runs once per HTTP request, not per frame; a reminder not to assume it
  will still work once mirroring's throughput mattes.
- `board=esp32-p4-evboard` is a stand-in for a real Tab5 profile. Any pin
  default the evboard.json carries that Tab5 doesn't share (beyond the SDIO
  pins already known to differ) is an open risk until the first real boot.

## Addendum (2026-08-17) — build succeeded on first real attempt

`pio run -e tab5` failed initially: pioarduino `55.03.311` requires Python
3.10+, and the host's PlatformIO install (also used by `tools/pio.sh` for
the Cardputer build) runs on Python 3.9.6. Rather than touch that shared
install, built an isolated venv:

```
/opt/homebrew/opt/python@3.12/bin/python3.12 -m venv ~/.platformio-tab5-venv
~/.platformio-tab5-venv/bin/pip install platformio
~/.platformio-tab5-venv/bin/pio run -e tab5
```

(Note: `python3.12` on `PATH` resolves to a `uv`-managed interpreter at
`~/.local/share/uv/...` whose `venv` creation is broken — hardcoded
`/install` prefix paths cause `ModuleNotFoundError: No module named
'encodings'`. Had to invoke Homebrew's `python@3.12` by its full path
instead.)

With that venv, the build succeeded clean:

```
RAM:   7.2%  (36,740 / 512,000 B)
Flash: 82.3% (1,078,890 / 1,310,720 B)
Took 369.09 seconds
```

Notably: `WiFi.setPins()` linked with no missing-symbol error, confirming
`CONFIG_ESP_HOSTED_ENABLED` is on by default for the `esp32p4`/
`esp32-p4-evboard` target — ADR 0049's assumption about this held without
needing to trace sdkconfig defaults further. `lgfx::rgb565_t::R8()/G8()/B8()`
(capitalized — an editor lint pass caught the lowercase guess before this
build) also compiled clean.

Not yet flashed to real hardware — that's the next step, pending
confirmation before writing to the device.

## Addendum (2026-08-17, continued) — three real bugs found flashing to real hardware, milestone verified

Flashed to a real Tab5. Confirmed the pixel path works: a browser on the
`MDC(IoT)` network loaded `/screenshot.bmp` and it matched the physical
display exactly (checkerboard pattern + IP-address text overlay), proving
`readRect()` on `Panel_DSI`/`Panel_FrameBufferBase` is pixel-correct on real
Tab5 hardware, end to end (panel -> RAM read -> BMP encode -> HTTP -> image
a browser renders). Three real, distinct bugs surfaced getting there, each
diagnosed from first principles against real source rather than guessed:

**1. `M5.begin()` hung before ever reaching `Serial.begin()`.** Symptom: the
display drew the test pattern (proving boot got that far) but *zero* serial
output ever appeared, across multiple capture methods, including through a
20-second window that should have caught the WiFi-connect loop's `.` dots.
Isolated by building `main_serial_test.cpp` — a bare `Serial.begin()` +
heartbeat sketch with no M5Unified/WiFi at all — which worked cleanly,
proving USB-CDC itself was fine and the hang was specifically inside
`M5.begin()`. Root cause: without a forced board hint, M5GFX's `autodetect()`
probes *every* board type it supports in sequence over I2C/SPI before
reaching Tab5's own block, and on real (non-evboard) Tab5 wiring one of
those earlier probes stalled. Fix: `-DM5GFX_BOARD=22` in `build_flags`
(`board_M5Tab5`, `boards.hpp`) — a documented compile-time override
(`M5GFX.cpp`'s NVS/board-detection block, `#if defined(M5GFX_BOARD)`) that
skips straight to the known board's init path. Confirmed by the post-fix
boot log: `[Autodetect] board_M5Tab5` fires immediately, no other board's
probe code runs.

**2. `WiFi.begin(ssid, "")` doesn't select open association the way
`WiFi.begin(ssid, nullptr)` does.** Symptom: `AUTH_FAIL` (reason 202) against
`MDC(IoT)`, a network confirmed open (no passphrase) with the device's MAC
already allow-listed — ruling out both a bad password and MAC filtering.
This project's own `wifi_credentials.h` already documents the fix (comment:
*"The empty string is converted to a NULL passphrase at the WiFi.begin()
call site... Do not use a placeholder string"*) and the original Cardputer
`wifi_manager.cpp:200` already implements it
(`(prof.password && *prof.password) ? prof.password : nullptr`) — this
sketch's first version simply didn't carry that conversion over, passing the
profile's raw (empty) `password` pointer straight through. Fixed by
replicating the same ternary. Confirmed: post-fix boot connected to
`MDC(IoT)` on the very first attempt, no `AUTH_FAIL` at all. The
`Host firmware version 2.12.11 / Slave firmware version 1.4.1` mismatch
warning is real (and worth fixing separately via `ESP_HostedOTA` — see
below) but was **not** the cause of the auth failure, as first suspected.

**3. A single full-frame `readRect()` + per-pixel conversion pass, run
synchronously inside an `AsyncWebServer` request handler with no yields,
starved the hardware watchdog and reset the whole chip.** Symptom: the
device answered `/screenshot.bmp` once, then went completely dark — no
ping, no HTTP, no serial output — and a fresh boot's log opened with
`rst:0x7 (HP_SYS_HP_WDT_RESET)` (plus a stale, checksum-invalid core dump
left over from the crash). At 1280x720 (921,600 pixels, ~28x the Cardputer
ADV's 240x135 — exactly the bandwidth/compute concern ADR 0048 flagged),
one uninterrupted `readRect(0,0,w,h,...)` plus one uninterrupted
BGR-conversion loop ran long enough with no `vTaskDelay`/yield to starve the
watchdog. Fixed by reading and converting in 32-row bands with
`vTaskDelay(1)` between bands, bounding any single blocking stretch
regardless of total image size — the same category of fix ADR 0049's
"one-shot, not per-frame" framing assumed wouldn't be necessary, but a
watchdog doesn't care that something is one-shot if it still blocks for
seconds. **This is a preview of the real problem the mirroring milestone
(continuous, not one-shot) will have to solve properly** — a per-tile
budget/yield discipline, not a one-off band chunk.

**Separately confirmed, not yet acted on:** the ESP32-C6 co-processor's own
firmware (1.4.1) is genuinely older than what the Arduino host driver
(2.12.11) expects. `ESP_HostedOTA`'s `updateEspHostedSlave()` is the
official fix path (staged `begin/write/end/activate`, part of arduino-esp32
core, not third-party) but requires `Network.isOnline()` first — a circular
dependency against `MDC(IoT)` specifically until/unless a WiFi connection
already works, which it now does. Worth doing at some point since a stale
co-processor firmware is a standing risk, but not blocking anything right
now.

**Test-environment gotcha, not a device bug:** repeated `curl` failures
against the device's IP from the machine running these tools turned out to
be because that machine was never on the `MDC(IoT)` network at all (it was
on an unrelated hotspot subnet, routing the target IP through an unrelated
VPN tunnel interface). Verify the tooling machine's actual network path
before concluding a reachability failure is the device's fault.

## Alternatives considered

- **Wait for a real `m5stack-tab5` PlatformIO board profile.** Rejected: none
  exists yet upstream and there's no timeline for one; blocking on it stalls
  the whole project.
- **Reuse `wifi_manager`/`CardputerMirror` immediately instead of a minimal
  standalone sketch.** Rejected for this milestone specifically: bundling
  unproven Tab5 bring-up with several thousand lines of Cardputer-tuned logic
  makes a failure impossible to localize. Revisit once the screenshot works.
