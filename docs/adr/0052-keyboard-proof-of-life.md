# ADR 0052 — Keyboard proof of life: hand-written driver, not vendored

**Status:** Accepted — implemented, verified on real hardware
**Deciders:** firmware owner
**Related:** the dashboard's keyboard mockup (ADR 0051) is purely decorative
until this lands; this is the "screenshot before mirror" milestone (ADR
0049) for keyboard input — smallest falsifiable proof before wiring into
`IInputSink`

## Context

The Tab5 keyboard accessory (I2C addr `0x6D`, SDA=GPIO0, SCL=GPIO1,
INT=GPIO50, own bus — verified in ADR 0048) needs a driver before any of
its presses can reach the browser or drive local UI. Rather than guess the
protocol, cloned and read M5Stack's own official source directly:
`github.com/m5stack/M5Tab5-Keyboard-UserDemo`, component
`m5_tab5_keyboard_component`.

**Register map (verified against real source, not the PDF spec sheet,
which failed to parse cleanly):**

| Reg | Name | Purpose |
|---|---|---|
| `0x00` | INT_CFG | interrupt config (write) |
| `0x01` | INT_STA | status bits; bit0=Normal-mode event pending. Write `0x00` to clear. |
| `0x02` | EVENT_NUM | pending event count (read); write `0x00` clears the queue |
| `0x03` | BRIGHTNESS | RGB LED brightness |
| `0x10` | KEYBOARD_MODE | write `0`=Normal, `1`=HID, `2`=String, `3`=BLE |
| `0x11` | RGB_MODE | LED binding vs custom |
| `0x20` | KEY_EVENT | Normal mode: read one byte per call, FIFO |
| `0x30` | HID_EVENT | HID mode: 2 bytes, modifier + keycode |
| `0x40`/`0x50` | CHAR_EVENT_LEN / BASE | String mode |
| `0x60` | RGB_COLOR_BASE | per-key RGB |
| `0xFE` | VERSION | firmware version (read) |
| `0xFF` | I2C_ADDR | change device address |

**Normal-mode key event byte** (register `0x20`), decoded straight from the
official driver's own `_handleInterrupt()`:
```
pressed = (event & 0x80) != 0
row     = (event >> 4) & 0x07     // 0-4 used (5 rows)
col     =  event       & 0x0F     // 0-13 used (14 cols)
```
`event == 0xFF` means no valid event. This matches the dashboard mockup's
14×5 grid (ADR 0051) exactly — good independent confirmation the grid is
really 14×5, from a completely different source than the product photos.

**Read/poll sequence** (also read straight off `_handleInterrupt()`):
read `INT_STA`; if bit0 set, read `EVENT_NUM` for a count, then read
`KEY_EVENT` that many times (each read pops one FIFO entry), then write
`0x00` to `INT_STA` to clear.

## Decision

**Hand-write a small driver against this verified register map, don't
vendor the official component.** The official library's Arduino code path
is real for `begin()`/register I/O (`_wire->begin(sda,scl,speed)` etc.) but
its polling/interrupt TASK methods (`_setupPollingArduino`,
`_pollTaskArduino`, `_setupHardwareInterruptArduino`) are unimplemented
stubs — literally `// TODO: Provide a timer-based polling implementation
if needed` with an empty body. Calling `begin()` with `M5_TAB5_KB_INT_MODE_POLLING`
under Arduino would silently do nothing further; the actual event-draining
logic in `_handleInterrupt()` is private and never gets called
automatically on this platform. Using the class at all would mean either
(a) driving its public register-access methods manually anyway — at which
point the class adds a dependency for no behavior we're not already
writing ourselves — or (b) patching a vendored copy to expose/call the
private logic, which fights the upstream library instead of using it.

The register protocol itself is small enough (a handful of single/multi-
byte reads and writes) that a ~100-line hand-written driver, in the same
style as every other piece of hardware integration in this repo
(`ReadbackFrameSource`, `wifi_manager`), is both less code and less risk
than vendoring an ESP-IDF-demo-shaped repo (no Arduino library manifest,
component buried inside a full application tree) through PlatformIO's
dependency resolution.

**Milestone scope:** standalone `src/main_tab5_keyboard.cpp` (own
`platformio.ini` env, matching the pattern every prior milestone used) —
bring up a second I2C bus (`Wire1`) on GPIO0/1, verify communication by
reading `VERSION`, set Normal mode, poll for events in `loop()` (via
`IInputSink::poll()`, since that hook already exists for exactly this),
log press/release + row/col to serial. Proves the physical keyboard
accessory works before touching `IInputSink::inject()` (remote-to-local) or
any browser-side wiring — the same order ADR 0049 used for the display.

## Consequences

**Positive**
- No new PlatformIO dependency, no vendored ESP-IDF-shaped code fighting
  the Arduino build.
- Register map and event format are independently confirmed against the
  dashboard mockup's 14×5 grid (ADR 0051), which came from product photos
  — two unrelated sources agreeing is a real cross-check, not a coincidence
  to paper over.

**Negative / open**
- HID mode, String mode, RGB LED control, and BLE mode are all left
  unimplemented — Normal mode's row/col events are all this milestone
  needs (matches `RemoteKey`'s own `{row, col, shift, fn}` shape already in
  `IInputSink.h`).
- `IInputSink::inject()` (remote presses arriving from the browser) isn't
  addressed yet — there's no local consumer for an injected press to drive
  (no menu system exists for Tab5 the way `menu.cpp` exists for the
  Cardputer ADV). That's real follow-on work, scoped out of this proof.

## Alternatives considered

- **Vendor `m5_tab5_keyboard_component` as-is.** Rejected — see Decision;
  its Arduino polling path doesn't work without additional code anyway.
- **Patch the vendored component to implement the Arduino polling stubs.**
  Rejected for this milestone: more code and an upstream fork to maintain,
  for a protocol simple enough to just implement directly.

## Addendum (2026-08-18) — two real bugs found on real hardware, full keymap confirmed

Flashed to the real keyboard accessory. First symptom: `begin()` failed
outright (`Wire.cpp: Bus already started in Master Mode`, then `NULL TX
buffer pointer` on every subsequent write). Root cause, visible directly in
the boot log: M5Unified's own system I2C bus (display/touch/audio/IMU,
SDA=31/SCL=32, ADR 0048) already claims **hardware I2C peripheral 1**
(`i2cInit(): ... num=1 sda=31 scl=32`) — a second `TwoWire(1)` instance for
the keyboard collided with it. Fix: use the **global `Wire`** (peripheral
0) instead of a second explicit instance — M5Unified deliberately avoided
peripheral 0, presumably so application code has it free. Confirmed by the
post-fix boot log: `i2cInit(): ... num=0 sda=0 scl=1`, no collision.

Second symptom, after the I2C fix: a continuous, well-formed stream of
press/release events at a single fixed `(row,col)` with nothing touching
the keyboard. Root cause: both this driver and the official
`m5_tab5_keyboard_component`'s `_handleInterrupt()` clear `INT_STA`
(register `0x01`) after draining the queue, but neither clears `EVENT_NUM`
(register `0x02`) itself — both assume reading `KEY_EVENT` auto-decrements
the device's own count. That assumption doesn't hold on this hardware: the
count never reaches zero, so the same queued entry gets re-read every
`poll()`. Fix: explicitly write `0x00` to `EVENT_NUM` too, not just
`INT_STA`, after draining. This is worth flagging back upstream — the
official demo likely has the same latent bug, just not yet hit by someone
running it standalone at high poll frequency.

**Full keymap, confirmed via a guided on-device wizard** (prompts through
all 70 keys in the dashboard mockup's own order, logs each prompt against
the real `(row,col)` the hardware reports): two complete, gap-free runs
through all 70 keys, zero exceptions. The mapping is exactly
`row = index ÷ 14, col = index % 14` against `web/index.html`'s own
`#tab5kb` key order (ADR 0051) — row 0 = number row (esc..del), row 1 =
symbol row, row 2 = tab/QWERTY row, row 3 = sym/home row, row 4 =
ctrl/bottom row. No translation table needed between the dashboard's
legend order and the real hardware coordinates; they're the same order.
This was worth verifying empirically rather than assumed, the same lesson
the Cardputer ADV project learned the hard way for its own keyboard (ADR
0017-0020) — but here the assumption turned out to be exactly right.
