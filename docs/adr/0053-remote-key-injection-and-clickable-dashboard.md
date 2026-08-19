# ADR 0053 — Remote key injection, clickable dashboard keyboard, mDNS

**Status:** Accepted — implemented, verified on real hardware
**Deciders:** firmware owner
**Related:** wires ADR 0052's driver into the `IInputSink` architecture ADR
0038 defined; makes ADR 0051's dashboard keyboard mockup functional instead
of decorative

## Context

ADR 0052 proved the I2C keyboard accessory works and confirmed the full
70-key map, but scoped out `IInputSink::inject()` (remote presses arriving
from the browser) since there was no local consumer to prove it against.
This milestone closes that gap: wire the driver into a real adapter, make
the dashboard's on-screen keyboard actually send key events, and expose the
device at `tab5.local` instead of only a DHCP IP.

## Decision

**`Tab5Adapter` / `Tab5InputSink` (`src/tab5_adapter.h/.cpp`)** implement
`IHostAdapter` / `IInputSink` (ADR 0038). `Tab5InputSink` owns a
`tab5kb::Tab5Keyboard` and exposes two independent single-slot queues:

- `inject(RemoteKey)` — called from the AsyncTCP task when a browser sends
  `{"t":"key",...}`. Per the `IInputSink` contract this only stores the
  value (`_lastRemote`/`_haveRemote`); it must not touch the display from
  that task.
- the physical-key trampoline (`_kb`'s `onEvent` callback) stores into a
  second slot (`_lastPhysical`/`_havePhysical`) the same way.

`loop()` in `main_tab5_mirror.cpp` drains both every iteration and calls a
shared `drawKeyFeedback(source, row, col, pressed)` that prints
`"<source>: <legend>\nrow=R col=C PRESSED/released"` into a fixed corner of
the Tab5's own display. Since that display is what's being mirrored, this
reaches every connected browser through the existing dirty-tile pipeline for
free — no new WebSocket message type needed to prove a remote click landed
or a physical press still works.

`CardputerMirror.begin(mc, gAdapter)` replaces the plain `begin(mc)` this
sketch used before — the plain path has no hook for a custom `IInputSink`.

**Dashboard keyboard click wiring** (`web/index.html`): row/col are read off
DOM position — which `.krow` a `.key` sits in, and its index within that
row — not a hardcoded lookup table. This works because ADR 0052 confirmed
real hardware's row/col numbering matches the dashboard's own markup order
1:1 (`row = index / 14, col = index % 14`): the grid *is* the keymap, so
there's nothing to keep in sync separately. Click sends
`{"t":"key",r,c,shift:false,fn:false}`, the same wire shape `RemoteKey`
already used.

**mDNS**: `MDNS.begin("tab5")` + `MDNS.addService("http","tcp",80)` after
WiFi connects, non-fatal on failure (IP still works, just not the hostname).

## A real bug found before flashing: 4-row bounds check

`CardputerMirror.cpp`'s WS key handler bounds-checked `r < 4` — the
Cardputer ADV's row count, inherited unchanged when this repo was forked
(ADR 0048). Tab5's matrix is 5 rows. Left as-is, every click on the bottom
row (`ctrl alt Z X C V B N M · ◀ ▼ ▶ space`, row 4) would have been silently
dropped by the library before ever reaching `_sink->inject()` — no error,
no log, just nothing happening. Caught by code review, not by testing;
fixed to `r < 5` with a comment explaining why the bound is fixed at the
real hardware's size rather than inherited (this repo is Tab5-only, ADR
0048, so there's no second board size to stay compatible with).

## Verification on real hardware

Both directions were confirmed live against the flashed device, not just
compiled:

**Remote → physical**, via Chrome automation reading the mirrored canvas's
actual pixel buffer (`getImageData`), not just eyeballing screenshots —
screenshot timing turned out to be unreliable here because `main_tab5_mirror
.cpp`'s existing bouncing-box animation intermittently overlaps the feedback
corner, so a plain screenshot could show stale or box-corrupted state even
when the feedback drew correctly. Instrumenting `WebSocket.prototype.send`
confirmed the exact frame the dashboard sent
(`{"t":"key","r":4,"c":8,"shift":false,"fn":false}`), and hashing the
feedback region's pixels before/after distinct clicks (row 4, two different
columns) produced two different hashes — proving distinct text rendered
each time, not a static leftover. This specifically exercised the row-4
bounds fix above, not just row 0.

**Physical → browser**: the first attempt (right after flashing) showed no
feedback update at all. Root cause turned out to be timing, not a bug: the
key was pressed while the sketch was still mid-WiFi-connect, before
`CardputerMirror.begin(mc, gAdapter)` (and therefore `Tab5InputSink::begin()`
/ `_kb.begin()`) had run — the keyboard accessory itself has no event
buffer that survives across a firmware-side "not polling yet" window the
way it survives a few `loop()` iterations, so a press before `begin()` is
simply missed. Confirmed by temporarily adding debug `Serial.printf`s to
`tab5_keyboard.cpp`'s `poll()` and `begin()` and to the physical-event
branch in `main_tab5_mirror.cpp`'s `loop()`, reflashing, and capturing
serial while coordinating a press *after* the boot log showed `mDNS up` /
`keyboard begin version=0x01`. That run logged
`status=0x01 count=1` → `row=2 col=8 pressed=1` → `pressed=0` (the "I" key,
press then release) — the physical path works correctly through the new
adapter wiring. Debug prints were removed and the firmware reflashed clean
afterward; they are not part of the shipped code.

## Consequences

**Positive**
- Remote key injection and the dashboard's on-screen keyboard are no longer
  decorative (ADR 0051) — they drive the real hardware path.
- Found and fixed a real latent bug (row bound) before it could silently
  eat every bottom-row remote press in the field.
- `tab5.local` works for both the dashboard and (implicitly) any future
  tooling that wants a stable hostname instead of a DHCP IP.
- The verification method (direct canvas pixel inspection + WS frame
  instrumentation via Chrome automation, rather than trusting screenshots)
  is worth reusing for future remote-input work on this project — it
  caught a false negative (screenshot looked wrong when the pipeline was
  actually fine) and a false positive risk (would have been easy to assume
  the physical path was broken from one bad-timing test) in the same
  session.

**Negative / open**
- `shift`/`fn` (the `Aa`/`sym` latch keys) aren't wired to anything real
  yet on the dashboard side — same gap ADR 0052 already flagged for the
  physical accessory's own latch keys.
- No local consumer exists yet for either the remote or physical key stream
  beyond the on-screen `row=/col=` feedback readout — there's no menu
  system on Tab5 the way `menu.cpp` exists for the Cardputer ADV. That
  remains real follow-on work.
- The keyboard accessory's missed-event-before-`begin()` behavior (no
  buffering across the firmware's not-yet-polling window) is now known but
  not defended against — a press during the ~10s WiFi-connect window at
  boot is silently lost. Not fixed here since it only affects the first
  few seconds after power-on and there's no user-facing symptom once the
  mirror server is up.
