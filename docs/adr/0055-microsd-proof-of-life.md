# ADR 0055 — microSD proof of life: needs an explicit SPI clock

**Status:** Accepted — implemented, verified on real hardware (`env:tab5-sdtest`):
`SD.begin(cs, SPI, 25000000) -> true`, `cardType=3 (SDHC)`,
`cardSize=30448MB`, reproducible across repeated boots.
**Deciders:** firmware owner
**Related:** written while debugging the SAME real hardware unit's microSD
card for [[tab5-launcher]] (a separate project, `~/Projects/tab5-launcher`,
a fork of `bmorcelli/Launcher`) — Launcher's own SD driver
(`SDM.begin(_cs, sdcardSPI)`, no clock argument) failed to detect the card
(`cardType()` read `CARD_NONE`) on a card independently confirmed healthy
FAT32/MBR via macOS Disk Utility. This ADR is the isolated, minimal proof
that pinned down the actual fix before applying it back in that other repo.

## Context

Card, format, seating, and pins were all ruled out first (see
tab5-launcher's own ADR 0002 for the full elimination process). What
remained untested in isolation: whether Launcher's SPI-mode `SD.begin()`
call was missing something Launcher itself never specified. M5Stack's own
official Tab5 microSD documentation (docs.m5stack.com/en/arduino/m5tab5/microsd)
uses the identical pins (`CS=42, SCK=43, MOSI=44, MISO=39`) but calls
`SD.begin(SD_SPI_CS_PIN, SPI, 25000000)` — an explicit 25MHz clock.
Launcher's own call never specifies a frequency at all.

## Decision

Standalone `src/main_tab5_sdtest.cpp` (own `env:tab5-sdtest`, matching this
project's established pattern — same isolation approach as
`main_serial_test.cpp` for USB-CDC and `main_tab5_keyboard.cpp` for the
keyboard accessory): reproduce M5Stack's own documented example exactly,
nothing else. `SPI.begin(sck, miso, mosi, cs)` then
`SD.begin(cs, SPI, 25000000)`, print result + `cardType()` + `cardSize()` +
a root directory listing, repeating every 3s in `loop()` (a one-shot
`setup()`-only print is a losing race against this hardware's native
USB-CDC re-enumerating on every reset — same lesson every serial-dependent
milestone on this project has hit).

Result, unambiguous and repeated: `true` / `SDHC` / `30448MB` — matching
the real 32GB card exactly. The explicit clock was the entire gap.

## Consequences

**Positive**
- Confirms the fix is genuinely just the missing frequency argument, not
  something specific to Launcher's own SPI/SPIClass setup — this sketch
  uses the plain Arduino `SD`/`SPI` objects directly, no Launcher code
  involved at all, and gets the identical result Launcher gets once the
  same fix is applied there.
- `env:tab5-sdtest` stays in this repo as a standing, minimal proof this
  project's own future work (or anyone else debugging Tab5 SD on real
  hardware) can reflash directly, without needing Launcher's much larger
  build.

**Negative / open**
- *Why* the unspecified default clock fails isn't explained from first
  principles here either — confirmed empirically that 25MHz specifically
  works, not that every unspecified-default fails or that there's a wider
  usable range. Not investigated further since 25MHz (M5Stack's own
  documented value) is sufficient.
- This repo's own `ReadbackFrameSource`/dashboard mirror doesn't touch the
  SD card at all — this milestone is purely a hardware proof, with no
  further integration into tab5-stack's own mirror/dashboard planned.
