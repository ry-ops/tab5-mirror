# ADR 0058 — M5StickS3 -> BLE OBD2 adapter proof of life

**Status:** Proposed — not yet built or verified on real hardware (no
StickS3 or OBD2 adapter in hand yet)
**Deciders:** firmware owner
**Related:** ADR 0056 (BLE scanner proof of life, Tab5) — same "smallest
possible proof before building anything on top of it" pattern, different
board and a real GATT peer instead of passive scanning

## Context

The goal driving this ADR: pair an **M5Stack StickS3** to a Bluetooth OBD2
adapter, read live vehicle data (RPM, speed, DTCs, etc.), and eventually
forward it to **DriveIQ** (`ry-ops/DriveIQ` — a separate repo/org, out of
scope here; this ADR covers only the StickS3-to-adapter link). Where a
Tab5 fits (e.g. via its Grove port) is undecided and also out of scope.

### Non-goals (asked and settled early, not a gap to fill later)

Standard OBD2 (SAE J1979/Mode 01-0A, what any ELM327-class adapter speaks)
is a **read-mostly diagnostic/emissions interface**: live PIDs, freeze
frame, DTC read/clear, VIN. It is not a control interface. In particular,
this project will never be able to adjust ECU calibration — variable valve
timing, fuel/spark maps, actuator control — through this stack, for two
independent reasons, either of which alone would already rule it out:

1. The protocol doesn't expose it. Mode 08 ("request control of onboard
   system") exists but manufacturers only ever wire it to a couple of
   standardized emissions self-tests (e.g. cycle the EVAP purge solenoid);
   there is no PID for cam-timing targets or any other tuning parameter.
   VVT is a closed-loop algorithm running inside the ECU's own firmware,
   not something exposed at the diagnostic port at all.
2. Even where a write path exists, reaching it requires manufacturer-
   specific **UDS (ISO 14229)** services behind a seed-key security-access
   handshake — a different tool category entirely (HP Tuners, EFI Live,
   Cobb Accessport, dealer tools), which no ELM327-class chip implements.

On a road vehicle this is also a regulatory boundary, not just a technical
one: altering emissions-relevant calibration (VVT included) outside the
factory map is EPA tampering regardless of what tool performs it. If
ECU tuning is ever wanted, it is a separate project with its own tooling
and its own legal review — not an extension of this telemetry pipeline.

### StickS3 hardware facts (verified against official M5Stack docs and the
### underlying ESP32-S3 datasheet, not blog posts)

| Property | Value | Source |
|---|---|---|
| SoC | ESP32-S3-PICO-1-N8R8, dual-core LX7 @ 240MHz | docs.m5stack.com/en/core/StickS3 |
| Flash / PSRAM | 8MB / 8MB Octal PSRAM | same |
| Display | `ST7789P3`, 135x240, 1.14" | same |
| Display SPI pins | MOSI 39, SCK 40, RS(DC) 45, CS 41, RST 21, BL 38 | same |
| Grove port (HY2.0-4P) | G9 (yellow), G10 (white), 5V, GND | same |
| System I2C (IMU/audio) | SCL 48, SDA 47 | same |
| Battery | 250mAh Li-ion | same |
| Radio | WiFi 2.4GHz **and BLE** — the M5Stack spec table only lists WiFi, but the underlying ESP32-S3-PICO-1 silicon integrates Bluetooth 5 (LE) on the same RF front end (no Classic BT/BR-EDR, matching every other ESP32-S3 board). The official Arduino example index for this board ships an `Examples > BLE > Scan` entry, confirming BLE is enabled/functional on this SKU, not just present in silicon. | ESP32-S3 datasheet; StickS3 Arduino example index |
| PlatformIO board id | No dedicated `m5stack-sticks3` profile found; community configs target the generic `esp32-s3-devkitc-1` under `espressif32` | community `platformio.ini` samples |

**Note:** unlike Tab5 (ADR 0048), the StickS3 has its own onboard ESP32-S3
radio — no ESP-Hosted SDIO co-processor bridge, no `WiFi.setPins()` dance.
`BLEDevice::init()` should work exactly like any other Arduino ESP32-S3
board. This is the one fact this ADR cannot yet confirm on real hardware.

### Why not talk to the FIXD sensor already owned

The user's current OBD2 device is a **FIXD**. Investigated and rejected for
this project: FIXD is a closed consumer product with no published API and
no known public reverse-engineering of its BLE GATT protocol (unlike
ELM327, an open, decades-old standard). Connecting to it would mean
reverse-engineering FIXD's characteristics from scratch (BLE sniff against
the official app) with no guarantee it exposes raw PID data rather than a
pre-digested "health score." Decision: buy a standard ELM327-protocol BLE
(or WiFi) adapter for this project instead; keep FIXD for its own app.

### BLE ELM327 clone landscape (from prior art, not this project's own
### testing yet)

No single standard GATT layout exists for BLE OBD adapters, but two
patterns cover the great majority of what's actually sold, per existing
open-source ELM327-over-BLE projects:

1. **Generic UART clone** (most cheap Chinese BLE ELM327 dongles, and
   OBDLink CX per `vdvornichenko/obd-ble-serial`): service `FFF0`, notify
   characteristic `FFF1` (adapter -> host), write characteristic `FFF2`
   (host -> adapter).
2. **Nordic UART Service (NUS)**: service
   `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, TX/RX characteristics
   `...002`/`...003` — used by some other BLE-serial-wrapper adapters.

Either way, once connected the wire content is plain ELM327 AT/OBD ASCII
(`ATZ`, `ATE0`, `ATSP0`, `010C\r` for RPM, etc.) — the same protocol
`ELMduino` and every ELM327 tutorial already documents; only the BLE
transport underneath differs from Classic Bluetooth SPP.

**Adapter recommendation:** OBDLink CX (or MX+) — commercial-grade,
BLE-native, confirmed-working GATT layout above, no gamble on an unlabeled
clone. A cheap FFF0/FFF1/FFF2 clone will very likely also work but is
unverified per-unit.

## Decision

Two-phase proof of life, smallest-first, same philosophy as ADR 0056:

**Phase A — BLE GATT explorer (buildable and useful today, before any
adapter purchase).** `src/main_sticks3_obd2.cpp` (`env:sticks3-obd2`):
scans for nearby BLE devices, connects to one matching a configurable name
filter (or the first strong-signal peripheral), enumerates every service
and characteristic it exposes, and prints the result to the StickS3's
135x240 display plus Serial. This works against *any* BLE peripheral —
it's how the real adapter's actual UUIDs get confirmed once bought, rather
than assumed from Phase B's guess.

**Phase B — ELM327 client**, built into the same sketch behind the same
FFF0/FFF1/FFF2-or-NUS detection: once a matching service is found, enable
notify on the RX characteristic, write `ATZ\r`, `ATE0\r`, `ATSP0\r`, then
poll `010C\r` (RPM) once per second and print the raw decoded value. No
DriveIQ upload here — that is explicitly out of scope for this sketch; it
stops at proving the link and getting one real PID value on-device.

Follows this repo's established per-board isolation pattern: standalone
`main_*.cpp`, its own `platformio.ini` env, nothing shared with the
Tab5/Cardputer mirror code beyond the repo shell.

## Consequences

**Positive**
- Phase A has zero dependency on which adapter gets bought — it's useful
  immediately and de-risks Phase B's protocol guess before hardware
  arrives.
- Reuses a proven pattern (ADR 0056) for BLE bring-up isolation.
- Keeps DriveIQ (a separate repo) fully decoupled — this firmware's only
  job is "get one real PID value off a real adapter onto the StickS3
  screen."

**Negative / open**
- **Nothing in this ADR is hardware-verified.** No StickS3, no OBD2 BLE
  adapter, no vehicle test yet. Status stays Proposed until at least Phase
  A runs on real hardware.
- PlatformIO board support for StickS3 is unconfirmed beyond community
  reports of `esp32-s3-devkitc-1` working — board_build flags in
  `platformio.ini` are a best-effort starting point, not a verified config.
- FFF0/FFF1/FFF2 vs. NUS vs. something else entirely is unknown until
  Phase A runs against the actual purchased adapter.
- DriveIQ upload (HTTP/MQTT/whatever that repo expects) is intentionally
  not designed here — needs its own ADR once this link is proven and
  DriveIQ's ingestion contract is known.

## Alternatives considered

- **WiFi ELM327 adapter instead of BLE** — sidesteps all BLE-GATT-guessing
  above (TCP socket, well-documented `AT`/OBD ASCII over a socket). Still
  viable as a fallback if BLE proves unreliable; not chosen first because
  the user specifically asked for a Bluetooth pairing.
- **Reverse-engineer FIXD** — rejected, see above.
