# Tab5 Mirror

![Version](https://img.shields.io/badge/Version-0.1.0-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Platform](https://img.shields.io/badge/Platform-ESP32--P4-orange)
![Cost](https://img.shields.io/badge/Cost-Free-brightgreen)

**View an M5Stack Tab5's display from a web browser, over WiFi** — no app
hooks, no patched firmware. Pixels come straight off the panel's own
framebuffer via `readRect()`.

This is a fresh-history fork of
[`cardputer-adv-mirror`](https://github.com/ry-ops/cardputer-adv-mirror),
retargeted from the M5Stack Cardputer ADV (ESP32-S3, SPI ST7789) to the
M5Stack Tab5 (ESP32-P4, DSI panel). See [ADR 0048](docs/adr/0048-fork-for-tab5.md)
for why a fork rather than another board branch of the original project.

## Status

| Milestone | State |
|---|---|
| Verified pixel-correct `readRect()` on real Tab5 hardware | [ADR 0049](docs/adr/0049-screenshot-before-mirror.md) — done |
| Continuous mirror: dirty-tile scan + RLE codec + WebSocket, reused unchanged from the Cardputer ADV library | [ADR 0050](docs/adr/0050-continuous-mirror.md) — done, live on hardware |
| Browser dashboard mockup redrawn for Tab5 (CSS only, no photo) | [ADR 0051](docs/adr/0051-tab5-dashboard-mockup.md) — done |
| Remote keyboard input via the Tab5's I2C keyboard accessory | not started — no `IInputSink` yet |

**What works today:** flash `tab5-mirror`, and the Tab5's live display streams
to any browser on the same network — view only. The dashboard page renders a
CSS-drawn mockup of the Tab5 docked with its keyboard accessory, but the
keyboard half is decorative (`aria-hidden`, no click-to-type wiring) until the
input milestone lands.

## Why the mirror itself needed no new code

The frame source sits behind `IFrameSource` ([ADR 0038](docs/adr/0038-adapter-driven-begin.md)).
Tab5's `Panel_DSI` reads pixels from a RAM line-buffer, not over a fragile
3-wire SPI bus like the Cardputer ADV's ST7789 — so `ReadbackFrameSource`,
the dirty-tile scheduler, the RLE wire codec, and the browser UI all carried
over verbatim. The only things that changed for this board are:

- **Geometry** — `CardputerMirror.h`'s `kScreenW/kScreenH/kTileCols/kTileRows`
  went from the Cardputer's 240x135 to Tab5's 1280x720 (16x9 grid of 80x80
  tiles, ~28x the pixel count).
- **Bring-up** — Tab5 has no built-in radio; WiFi rides an ESP32-C6
  co-processor over SDIO, which needs an explicit `WiFi.setPins(...)` call
  (see `src/main_tab5_mirror.cpp`) before `WiFi.begin()` will work at all.
- **Dashboard chrome** — the browser page's case mockup was redrawn in CSS
  for Tab5's tablet-plus-keyboard shape ([ADR 0051](docs/adr/0051-tab5-dashboard-mockup.md)).

Three real bugs surfaced getting the screenshot milestone onto real
hardware — a board-autodetect hang, an empty-password WiFi join that needs
`nullptr` not `""`, and a watchdog reset from an unyielded full-frame read at
28x the old pixel count. All three, and how they were diagnosed, are in
[ADR 0049](docs/adr/0049-screenshot-before-mirror.md)'s addenda.

## Hardware facts (read from M5GFX/M5Unified sources, not datasheets)

| Property | Value |
|---|---|
| Board enum | `board_M5Tab5 = 22` |
| Main SoC | ESP32-P4 (RISC-V, dual-core 360 MHz HP + LP core) — **no built-in WiFi/BT** |
| Radio | ESP32-C6-MINI-1U co-processor over internal SDIO |
| Panel | `Panel_DSI : Panel_FrameBufferBase`, native 720x1280, rotation 1 -> **1280x720 logical** |
| Readback | `readRect()`/`copyRect()` read a RAM line-buffer directly — no SPI bus, no 3-wire SIO risk |
| PSRAM | 32 MB Octal, **must run at 200 MHz** |
| Flash | 16 MB |
| System I2C | SDA GPIO31, SCL GPIO32 (display/touch/audio/IMU/RTC/power) |
| SDIO pins (override required) | CLK 12, CMD 13, D0 11, D1 10, D2 9, D3 8, RST 15 |
| Keyboard accessory | Separate STM32F030 module, 70-key, I2C addr `0x6D`, own bus (SDA GPIO0, SCL GPIO1, INT GPIO50) — not yet wired into this firmware |

Full sourcing and the Cardputer ADV's own hardware table are in
[ADR 0048](docs/adr/0048-fork-for-tab5.md) and [`docs/adr/README.md`](docs/adr/README.md).

## Build

No dedicated Tab5 board profile exists upstream in PlatformIO yet, so
`platformio.ini` targets `esp32-p4-evboard` via the `pioarduino` platform
fork with a forced board override (`-DM5GFX_BOARD=22`) — see
[ADR 0049](docs/adr/0049-screenshot-before-mirror.md) for why.

```bash
cp include/wifi_credentials.example.h include/wifi_credentials.h  # fill in WIFI_PROFILES; gitignored
pio run -e tab5-mirror -t upload   # continuous mirror, the working milestone
```

Other environments in `platformio.ini`:

| Env | Purpose |
|---|---|
| `tab5` | Base config (platform, board, lib_deps); not built directly |
| `tab5-serialtest` | Diagnostic: bare `Serial.begin()` + heartbeat, no M5Unified/WiFi |
| `tab5-mirror` | Continuous display mirror — the current working firmware |

Each environment's `build_src_filter` excludes the Cardputer ADV's own
`main.cpp`/`menu.cpp`/`keyinject.cpp`/`wifi_manager*.cpp` — those are built
around the Cardputer's TCA8418 matrix keyboard and SoftAP-capable WiFi
manager, inherited from the fork but not yet ported to Tab5. They stay in the
tree as reference for the eventual keyboard-input milestone.

Browse to the IP printed on the device's serial log (115200 baud) or its own
display once connected.

## Layout

```
docs/adr/                  ADR 0001-0047 inherited from cardputer-adv-mirror;
                            0048-0051 are this fork's own Tab5 decisions
lib/CardputerMirror/       Mirror, IFrameSource, ReadbackFrameSource, RLE codec
                            — board-agnostic, reused unchanged from the fork
web/index.html             Browser client — Tab5 dashboard mockup (ADR 0051)
src/main_tab5_screenshot.cpp  One-shot BMP-over-HTTP proof (ADR 0049)
src/main_tab5_mirror.cpp   Continuous mirror entry point — current firmware
src/main_serial_test.cpp   Diagnostic: isolates Serial/HWCDC from M5Unified
src/main.cpp, menu.cpp,    Cardputer ADV-specific; excluded from every Tab5
  keyinject.cpp,            build_src_filter; kept for reference until the
  wifi_manager*.cpp         Tab5 keyboard-accessory milestone
include/                   wifi_credentials.h (gitignored) + its example
tools/                     Asset generator, MCP dev server, test suite, codec fuzzer
```

## Known limits

- **No remote input yet.** The Tab5's I2C keyboard accessory has no
  `IInputSink` implementation — the dashboard's keyboard mockup is purely
  decorative. Mirroring is view-only for now.
- **`budgetUs` is an unmeasured guess.** `main_tab5_mirror.cpp` starts at
  `8000us`; real per-tile `readRect()` cost at 6,400 px/tile (vs. the
  Cardputer's 2,700) hasn't been isolated from WiFi/encode overhead yet.
- **No dedicated PlatformIO board profile.** Builds target
  `esp32-p4-evboard` with pin/board overrides, not a real `m5stack-tab5`
  profile — see [ADR 0049](docs/adr/0049-screenshot-before-mirror.md).
- **Tearing / missed changes** — inherited limits of tile-based CRC readback
  from [ADR 0002](docs/adr/0002-gram-readback-mirror.md): a tile can be read
  mid-draw, and a change reverted between two scans of the same tile is
  never seen.

## Test suite

Inherited from the original project and still applicable to the shared
`lib/CardputerMirror` code (codec, keymap generation tooling):

```bash
c++ -std=c++17 -O2 -o verify_codec tools/verify_codec.cpp && ./verify_codec
for t in tools/test_*.mjs tools/test_*.cjs; do echo "--- $t"; node "$t"; done
```

The keymap-specific tests (`test_keymap.mjs`, `test_coverage.mjs`,
`test_dual_legend.mjs`) check the Cardputer ADV's key layout and don't apply
to Tab5 until the keyboard-accessory milestone defines its own layout.

## License

MIT — see [LICENSE](LICENSE).
