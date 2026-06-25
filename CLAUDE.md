# CLAUDE.md — dataflash-re

Guidance for Claude Code working in this repo. Read `protocol/dataflash-protocol-spec.md`, `firmware-analysis/*.md`, `bridge/README.md`, `bridge/PATTERNS.md`, and `bridge/CONTROL-TOUCHOSC.md` before doing work — don't re-derive what's documented.

## What this project is
Reverse-engineering the **original Lightwave Research / High End Systems "Dataflash"** xenon-strobe control link (proprietary "LWR" protocol, *not* the later DMX-native AF1000), and building a bridge so the fixtures can be driven from modern controllers (DMX / Art-Net / sACN / **TouchOSC**) via an **ESP32-PoE-ISO + MAX485**.

## Current status
- ✅ Assets organized & identified (`assets/`, see `assets/MANIFEST.md`)
- ✅ Firmware disassembled (8051, Rev 2.82) — `firmware-analysis/01`,`02`; tool `dis51.py`
- ✅ Prior art: **US Patent 5,078,039** (Tulk & Belliveau), public-domain
- ✅ Protocol decoded → **`protocol/dataflash-protocol-spec.md`** (draft v0.2)
- ✅ Schematic read (CPU, baud, transceiver, pinout) — `firmware-analysis/03`
- ✅ **Bridge firmware scaffolded** (`bridge/`): RS-485 TX, Art-Net/sACN target, web UI
- ✅ **OSC/TouchOSC control + on-device pattern engine scaffolded** (`bridge/src/osc.*`, `patterns.*`)
- ✅ **Pattern set modeled on the original gear** (strobe idiom; native/framework/alternate tiers) — `bridge/PATTERNS.md`
- ✅ **Bridge firmware builds clean** (`pio run`, esp32-poe-iso, 67% flash / 19% RAM)
- ✅ **Host-side golden framing reference + decoder** (`tools/dataflash_frame.py`) — mirrors `sendRefresh()`, round-trip self-tested; prints exact bytes/bits/timing to match against a capture
- ✅ **Bench validation procedure** — `bridge/BENCH-VALIDATION.md` (scope/LA steps, golden targets, [?]-flip checklist)
- ✅ **TX chain wire-verified on ESP32-S3-ETH** (GPIO17 data / GPIO16 DE, MAX485ESA): loopback capture via USB-RS485 (8-data+parity framing — see `tools/dataflash_frame.py --capture`) decoded **137/137 refreshes exactly** as `55 7F 88×8 F7`; scope bit-period ≈2.66µs. Confirms RMT 9-bit/375k framing, packet ordering, nibble packing. (Validates bridge *emits the spec*, not spec-vs-OEM.)
- ✅ **Bridge nibble-packing verified** via asymmetric capture (2026-06-25): fixture 0→`F0` (even=high nibble), fixture 1→`0F` (odd=low nibble) — matches `addr÷2`=byte index, `addr&1`=nibble. Bridge emits per spec; `nibbleSwap` flips it if real fixtures disagree.
- ⬜ Live-capture a **real original controller** (spec vs OEM); confirm nibble mapping on a real fixture
- ⬜ Hardware-verified end to end on a real fixture

## Protocol cheat-sheet (authoritative: protocol/dataflash-protocol-spec.md)
- Physical: **RS-422/485 differential** (DS8921), 3-pin — **Pin1=GND, Pin2=Data−, Pin3=Data+** (DMX convention). Each fixture is an active repeater w/ power-off relay bypass.
- Serial: **375000 baud, 9 data bits** (8051 Mode 2, SMOD=1, 12 MHz). No DMX break.
- **9th bit = control/data flag** (1=control, 0=data). Markers: `0x55`=ARM, `0x7F`=START, `0x00`=HEARTBEAT(~120Hz), `0xF7`=STOP/FIRE, `0xFF`=CLEAR, `0x12+95 88 25 A8`=special.
- Data byte = **2 fixtures × 4-bit intensity**; positional addressing via 8 DIP switches (byte index=addr÷2, nibble=addr&1).
- Packet: `ARM, START, data×N, STOP` + heartbeats. Tags: [C]firmware [P]patent [S]schematic [?]needs-capture.

## IMPORTANT: 9-bit TX is via RMT, not UART
Classic ESP32 UART has no stick parity, so it can't hold a constant 9th bit. The bridge emits each bit with **RMT** (`bridge/src/dataflash_tx.cpp`), 213 ticks/bit @ clk_div=1 ≈ 375.6 kbaud. SPI/I2S are valid alternatives.

## Patterns are in the STROBE idiom, modeled on the original controller
The Dataflash is a strobe, not an LED array: flash timing + which heads fire + 16 intensity levels. The original controller's model = **Program (sequence of Stages)** + modifiers (Auto/Beat advance, Multiply, Random, Modulate, Flash, Blackout). The engine mirrors this: a stage sequencer (`patterns.cpp`) with those modifiers as params. Tiers — NATIVE: live/all/single/seq; FRAMEWORK: chase/alternate/build/strobe/sparkle; ALTERNATE: wave/pingpong/comet. The grid builder == a Program. Full catalog + caveats in `bridge/PATTERNS.md`.

## Bridge architecture (`bridge/src/`)
```
inputs.*      Art-Net(6454)+sACN(5568) -> g_dmx (HTP merge)         [target role]
osc.*         TouchOSC OSC in (8000) / feedback out (9000)          [controller role]
patterns.*    stage-sequencer strobe engine (free-runs); modifiers: factor/random/advance/modulate
main.cpp      scheduler: PAT_LIVE=network passthrough; else patterns_render; refresh + heartbeats
dataflash_tx.* RMT 9-bit/375k framing: ARM,START,<2 fixtures/byte>,STOP + heartbeats
net.*  ETH+WiFi-AP fallback   webui.*  async status/config/test   config.h  NVS+pins   ui.h  OLED+encoder stub
```
OSC schema + TouchOSC layout: `bridge/CONTROL-TOUCHOSC.md`. Pattern catalog: `bridge/PATTERNS.md`.

## Working with the firmware image
```bash
cd firmware-analysis
unzip -o ../assets/firmware/df282.exe -d df282_extract   # -> df282_extract/Df31f2.82
python3 dis51.py "[('region', 0x0C06, 0x0CB0)]"
```

## Building the bridge
Three PlatformIO envs (pick one with `-e`, else `pio run` builds all):
- `esp32-poe-iso` — Olimex ESP32-PoE-ISO (LAN8720 RMII, PoE) — production target role.
- `esp32-s3-eth` — WaveShare ESP32-S3-ETH (W5500 SPI ETH, native USB-C) — bench/TX board. Wired ETH needs W5500 SPI init (not yet in net.cpp); WiFi-AP only for now.
- `esp32-c3` — generic C3 devkit (WiFi only) — alt bench board.
```bash
cd bridge && pio run -e esp32-s3-eth -t upload && pio device monitor -e esp32-s3-eth
```

## Next tasks (suggested order)
1. **Build & flash**, scope `DF_TX_PIN`: ~2.667 µs bit time, 11-bit frame, 9th bit 1=control/0=data.
2. **Live-capture a real controller** -> `captures/`; flip the spec's [?] items (ordering, heartbeat cadence, `0xFF`/`0x12`, nibble mapping). Use `nibbleSwap` if even/odd land wrong.
3. **Find the duty/cooldown ceiling** on real heads (fast modes drop flashes by thermal self-protection) — note safe ranges for speed/factor/bpm.
4. **TouchOSC**: build the 3-page layout per `CONTROL-TOUCHOSC.md`; generate builder-grid Lua + feedback routing for the chosen grid size.
5. Later: audio-in for `modulate`/beat `advance`; OLED+encoder front panel (`ui.h`); non-blocking RMT; custom hardware.

## Conventions
- Working dir `/Users/runnerr0/Temp/dataflash-re`. Spec-driven: `protocol/...spec.md` is the source of truth; tag claims [C]/[P]/[S]/[?].
- US Patent 5,078,039 is public domain. Schematic PDF is scanned: render fixture sheets p26 (CPU/ROM), p27 (Data Link Drivers), p29 (Zero Cross) with `pdftoppm -r 300`.
- Interop RE of owned hardware — keep work documented and reproducible.
