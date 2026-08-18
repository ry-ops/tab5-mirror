# ADR 0048 — Fork for M5Stack Tab5, and what actually has to change

**Status:** Proposed
**Deciders:** firmware owner
**Related:** forks cardputer-adv-mirror at commit `a7b4b33` (no shared git
history by choice — see repo README)

## Context

This repo (`tab5-stack`) is a fresh-history fork of `cardputer-adv-mirror`,
adapting the browser mirror + remote-control library from the M5Stack
Cardputer ADV to the M5Stack Tab5. The two boards differ far more than the
Cardputer/Launcher pair this library was already made adapter-driven for
(ADR 0038): different SoC family, different display technology, no built-in
radio, ~28x the pixel count.

All facts below were verified against real cloned `m5stack/M5GFX` source
(`master`, fetched today) and the M5Stack Tab5 keyboard I2C protocol doc, not
inferred from blog posts, per this project's established working style.

### Hardware facts

| Property | Value | Source |
|---|---|---|
| Main SoC | ESP32-P4 (RISC-V, dual-core 360 MHz HP + LP core), **no built-in WiFi/BT** | M5Stack docs |
| Radio co-processor | ESP32-C6-MINI-1U over internal SDIO (hosted-network bridge) | M5Stack docs |
| SDIO pins (Arduino) | Must be set explicitly: `WiFi.setPins(12,13,11,10,9,8,15)` (CLK,CMD,D0,D1,D2,D3,RST). arduino-esp32's default SDIO pins target the generic ESP32-P4 EvalBoard, **not** Tab5's wiring — without the override, `WiFi.begin()` fails with `H_SDIO_DRV: card init failed`. | M5Stack community, M5Dashboard reference doc |
| Board enum | `board_M5Tab5 = 22` | `M5GFX/src/lgfx/boards.hpp:33` |
| Display panel class | `Panel_DSI : public Panel_FrameBufferBase` (`platforms/esp32p4/Panel_DSI.hpp`) — autodetects ST7121, ST7123 (post Oct-2025 units, integrated touch), or ILI9881C+GT911 (pre Oct-2025 units) | `M5GFX.cpp:2924-3130` |
| Native panel geometry | `memory_width=720, memory_height=1280` (portrait); rotation=1 → **1280x720 logical (landscape)**, same rotation-1 convention the Cardputer ADV code already uses | `M5GFX.cpp:3120-3123, 3812-3813` |
| **Readback path** | `Panel_FrameBufferBase::readRect()`/`copyRect()` read directly from a RAM line-buffer (`_lines_buffer`) — **no SPI bus, no 3-wire SIO risk**. `ReadbackFrameSource::fetchTile()` (`lib/CardputerMirror/ReadbackFrameSource.cpp`) already calls only `M5.Display.readRect(...)`, nothing Cardputer-specific — this should work unmodified on Tab5, likely *more* reliably than ADR 0002's SPI path. | `Panel_FrameBufferBase.hpp:49-63`, existing `ReadbackFrameSource.cpp` |
| System I2C | SDA=GPIO31, SCL=GPIO32 (100 kHz) — display/touch/audio/IMU/RTC/power monitor | `M5GFX.cpp:2926-2932`, M5Dashboard reference doc |
| PSRAM | 32 MB Octal. **Must run at 200 MHz** — M5GFX logs `"M5Tab5 need PSRAM SPEED 200MHz"` if slower. arduino-esp32 3.3.0 has a known PSRAM-at-200MHz bug (use 3.2.x or ≥3.3.1). | `M5GFX.cpp:2971-2981` |
| Flash | 16 MB | M5Stack docs |
| Keyboard accessory | Separate STM32F030-based module, 70-key, connects via Ext.Port1. **I2C addr 0x6D**, SDA=GPIO0, SCL=GPIO1, INT=GPIO50 (own bus, distinct from system I2C above). Normal/HID/Character report modes. | M5Stack Tab5 Keyboard I2C Protocol doc |
| PlatformIO board support | **No dedicated Tab5 board profile exists yet.** Community workaround: `pioarduino` platform fork + `board = esp32-p4-evboard` + manual SDIO pin override in code. Standard PlatformIO `espressif32` may lag P4 support entirely — verify current state before committing to a platform line. | M5Stack community thread |

### What this means for the port

The `IFrameSource`/`IHostAdapter` seam from ADR 0038 already isolates the one
piece that's genuinely Cardputer-specific (`kTileW`/`kTileH`/`kScreenW`/
`kScreenH` and the CRC/dirty-tile arrays sized from them in
`lib/CardputerMirror/CardputerMirror.h`) from the parts that are already
board-agnostic (`ReadbackFrameSource`'s calls into `M5.Display`, the wire
protocol, the browser UI). Concretely:

- **Frame source:** `ReadbackFrameSource` likely needs no code changes, just
  confidence it's reliable here (Tab5 has no 3-wire SIO risk to self-test
  for — `selfTest()` may be unnecessary or trivially 100%).
- **Geometry constants:** `kScreenW=240`, `kScreenH=135`, `kTileCols=4`,
  `kTileRows=3` are compile-time and hardcoded for the Cardputer's screen.
  Tab5 at 1280x720 is **~28.4x more pixels** (921,600 vs 32,400). This is
  the actual porting work, not a rename:
  - Tile grid, `_crc[kNumTiles]`, `_force[kNumTiles]`, `_tile` scratch buffer
    all need re-deriving for 1280x720 (or a coarser/scaled mirror — full-res
    real-time WiFi mirroring at this pixel count is a real bandwidth
    question this project hasn't faced yet).
  - `budgetUs` (currently tuned "4500us ~= 1 tile (4.05ms)" for a 60x45
    Cardputer tile) needs new numbers for whatever tile size Tab5 ends up
    with.
- **WiFi bring-up:** the existing `wifi_manager`/`wifi_manager_rt` code talks
  to the Arduino `WiFi` class generically, but Tab5 needs the explicit
  `WiFi.setPins(...)` SDIO override called before any of it runs, or
  `WiFi.begin()` fails outright regardless of credentials.
- **Keyboard input:** Cardputer ADV's TCA8418 matrix keyboard
  (`docs/adr/README.md`'s hardware table, INT GPIO11) has no Tab5 analogue —
  Tab5's keyboard is an external I2C accessory on a completely different bus
  and protocol (addr 0x6D, own INT line, three report modes). `IInputSink`
  needs a new concrete implementation, not a port of the existing one.
- **PlatformIO platform line:** decide `pioarduino` fork vs. mainline
  `espressif32` before writing `platformio.ini` — this determines the board
  ID and available `board_build.*` options, and community reports suggest
  mainline PlatformIO support for ESP32-P4 may still be immature.

## Decision

Not yet made — this ADR exists to record verified facts and scope before any
renaming/adaptation work, per this project's ADR-first working style. Next
ADR(s) should decide: (1) PlatformIO platform line, (2) target mirror
resolution/tile strategy given the 28x pixel increase, (3) `IInputSink`
design for the Tab5 keyboard accessory.

## Consequences

**Positive**
- The adapter architecture (ADR 0038) already did the hard work of
  separating board-specific code from shared logic — this fork is not
  starting from zero despite the hardware being very different.
- Readback-based mirroring should be *more* trustworthy on Tab5 than on
  Cardputer ADV (no SPI 3-wire risk), if it fits the bandwidth budget.

**Negative**
- No PlatformIO board profile exists yet for Tab5; the build config itself
  is unproven ground, separate from the application code.
- The pixel-count increase is large enough that naively reusing the
  Cardputer's tile/budget numbers will not work — this needs its own
  measurement pass on real hardware, not just constant substitution.
- `IInputSink` for the Tab5 keyboard accessory is new code, not a port.

## Alternatives considered

None yet — this ADR is fact-finding, not a design decision.
