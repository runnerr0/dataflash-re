# Dataflash (original) control protocol — reconstructed spec

Status: **draft v0.2** — reconstructed from three agreeing sources:
1. **US Patent 5,078,039** (Tulk & Belliveau, Lightwave Research, filed 1990) — semantics.
2. **Fixture firmware Rev 2.82** (8051, "Data Flash strobe head," Steve Tulk) — byte values + bit-level mechanism (disassembly in `firmware-analysis/`).
3. **Original schematics** (`assets/schematics/...pdf`) — hardware: CPU, baud, transceiver, pinout (see `firmware-analysis/03-schematic-findings.md`).

Legend: **[C]** firmware-confirmed · **[P]** patent · **[S]** schematic-confirmed · **[?]** inferred, wants live capture.

## Physical layer  [S]
- Topology: controller → up to **256 fixtures** [P], daisy-chained. Separate **Data In** and **Data Out** connectors per fixture.
- Connector: **3-pin** (XLR per manuals; earliest units ¼"). Pinout [S]:
  - **Pin 1 = Ground / shield**
  - **Pin 2 = Data−**  (24K fail-safe pull-down)
  - **Pin 3 = Data+**  (24K fail-safe pull-up)
  - Same convention as DMX512.
- Transceiver: **National DS8921** RS-422/485 differential driver+receiver. [S]
- Each fixture is an **active repeater** (receives → CPU, re-drives → Data Out). A **relay bypasses In→Out when unpowered**. [S]
- Bit rate: **375 kbaud** [P], from 8051 UART **Mode 2 (9-bit)**, **SMOD=1**, **12 MHz** crystal [C][S]. Not DMX's 250k; **no DMX break**.
- Frame: **9 data bits**, async. **9th bit = control/data flag** [C]: 1 → control byte, 0 → data byte (firmware tests RB8 at 0x0C87).

## Byte types

### Control bytes (9th bit = 1)
Firmware dispatcher at 0x0C06; handlers disassembled:

| Value | Role | Handler | Effect |
|---|---|---|---|
| `0x55` | **ARM** [C][P] | 0x0A9E | set arm/enable flags (RAM 0x7D–0x7F) |
| `0x7F` | **START** [C][P] | 0x0A87 | read 8 DIP switches (P1, strobed by P3.5/ADDRESS), complement (active-low) → addr in 0x33; position counter 0x34 = addr÷2; set "active" flag (0x40); addr bit0 = nibble select |
| `0x00` | **HEARTBEAT** [C][P] | 0x0B52 | decrement cooldown / max-intensity sums; service Timer1; ~120 Hz |
| `0xF7` | **STOP / FIRE** [C][P] | 0x0B86 | if active, latch max intensity and enable firing; trigger asserted on **P3.4 = FIRE** in the zero-cross-synced timer path |
| `0xFF` | **CLEAR / blackout** [C] | 0x0AAC | clear arm + intensity |
| `0x12` | **SPECIAL / diag** [C] | 0x0BC6 | expects `95 88 25 A8`; on match → jump 0x051D |

### Data bytes (9th bit = 0)  [C][P]
- Sent in sequence after START. Each fixture decrements its position counter (0x34) per data byte and **captures the byte when it reaches 0**.
- Each data byte = **two fixtures, 4-bit intensity each** (16 levels); even/odd address selects high vs low nibble (addr bit0).
- 256 fixtures ⇒ 128 data bytes; full refresh ~6 ms @375k. [P]

## Packet shape (typical)
```
[ARM 0x55] [START 0x7F] [data×N] ... [HEARTBEAT 0x00 ...] [STOP/FIRE 0xF7]
```
Exact ordering / heartbeat interleave cadence: confirm on live capture. [?]

## Fixture I/O (8051 Port 3)  [S]
RXD/P3.0 = serial in · P3.2 = WATCHDOG · P3.3 = #ARM · **P3.4 = FIRE** · P3.5 = ADDRESS (DIP strobe) · P3.6 = mode select · P3.7 = #LED.
(Correction from earlier: the P3.2 pulses are watchdog kicks; FIRE is P3.4.)

## Hardware (fixture)  [S]
8031/8051 @12 MHz · 27128 16 KB EPROM · 74373 latch · DS8921 line transceiver · H11L1 opto zero-cross detector · phase-controlled SCR flash trigger · onboard cooldown/thermal protection.

## What a new controller must emit
ESP32-PoE + RS-485 → fixture #1 Data In (Pin3=A/Data+, Pin2=B/Data−, Pin1=GND); chain self-repeats.
1. UART: **375000 baud, 9 data bits** (9th bit settable per byte).
2. Per refresh: `0x55`(ARM,9th=1), `0x7F`(START,9th=1), **128 data bytes (9th=0)** packing two 4-bit intensities each, `0xF7`(STOP/FIRE,9th=1).
3. `0x00` heartbeats (9th=1) ~120 Hz between refreshes.
4. Map DMX/Art-Net 8-bit channel → 4-bit nibble at the byte/nibble for each fixture's DIP address.
Cooldown/phase/thermal are handled locally by the fixtures — don't reproduce.

## Live OEM controller capture (2026-06-26) — empirical [C-wire]
Captured the original controller's output across **all 99 programs + 3 function previews** with a home-built 9-bit sniffer (ESP32 + MAX485 in RX, bit-bang RX that recovers the 9th bit; `bridge/` env `esp32-s3-sniff`, sampled via `tools/sniff_sampler.py`). This characterizes what THIS controller actually emits — which differs from the firmware-derived marker model above:
- **Frame (universal — every program):** `0x55 0x40` (data-plane, 9th=0) + **8 data bytes = 8 fixtures, ONE byte per fixture** (8-bit value). `0x55` = alternating-bit preamble, `0x40` = frame-start.
  - **Empirically verified (2026-06-25):** this is an **8-head controller** ("EIGHT FIXTURES LINEAR" on the chassis label). Decoding captures as 8 bytes vs 16 nibbles is decisive: in chase programs (e.g. prog-10) the single bright head walks **smoothly byte-by-byte across exactly 8 positions**; the 16-nibble model scatters it. Wash programs (prog-11) drive all 8 bytes **in unison through full-range values** (`E6 C4 B3 A2 91 …`) — values that span 0–255 and so cannot be 4-bit nibbles. ⇒ **fixture == byte.**
  - The byte is not pure intensity: a head's "on" value varies by sub-field (`E6` background vs `E0`/`86` highlight), so the two nibbles likely pack **intensity + flash-mode** per fixture. The *unit of addressing is the whole byte* regardless. (This is the **8-fixture controller variant**; the firmware's "2 fixtures × 4-bit per byte, 128 bytes" model is the **256-fixture** variant — different product, same `55 40` framing family. See `firmware-analysis/04`.)
- **Control bytes (9th=1) the controller uses:** `0x00` HEARTBEAT (the timebase) and **`0xC0`** (a frame/refresh marker, in every program). `0x80` also reads as control but is partly a 9th-bit timing artifact of its single-bit pulse.
- **NOT emitted as control:** ARM `0x55`, START `0x7F`, FIRE `0xF7` — **zero occurrences in 3.2M+ bytes across all 102 captures** (`0x55`/`0xF7` appear only as *data* values; `0x7F` never appears).
- Programs differ only in how they animate the 8 data bytes (wash / chase / ramp / multi-level).
- ⇒ For normal program output the controller broadcasts `55 40`-framed fixture data on a heartbeat timebase with `0xC0` control; it does **not** use the firmware's ARM/START/FIRE dispatch — likely a separate addressed/setup command path not exercised by these programs.

## Open items
- [x] Live-capture the OEM controller — done (above): `55 40`-framed broadcast + heartbeat/`0xC0`, not the firmware marker packet.
- [ ] Decode `0xC0` (and `0x80`) control semantics; determine how fixtures sync/address to the `55 40` frame without a START marker.
- [ ] Confirm `0xFF` and the `0x12`-sequence semantics empirically.
- [ ] Verify on a real fixture; reconcile the firmware's marker dispatch vs the controller's broadcast framing.
- [x] Transceiver / pinout / crystal — confirmed from schematic.
