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
- ✅ **Real OEM controller captured** (2026-06-26): read the original controller via FTDI USB-RS485 + `tools/dataflash_frame.py --capture`. **Baud 375000 confirmed**; captured a labeled **program library** (`captures/prog-01..10.bin`, gitignored). Gotchas: FTDI wedges on controller power-off (USB replug); the controller's XLR uses a **nonstandard pinout** (GND not on the DMX pin).
- ✅ **Firmware re-read reconciles 0x7F** (`firmware-analysis/02`): `0x7F`=START is a real equality test (handler `0x0A87` reads DIP→position counter `0x34`=addr÷2; ARM `0x0A9E` only sets flags) — gated by the 9th bit. But `0x7F` is **absent on the 8-bit wire** under N/E/O parity → the control plane is only visible WITH the 9th bit.
- ✅ **9-bit RX sniffer firmware** (`bridge/src/dataflash_rx.*`, env `esp32-s3-sniff`) — recovers the control/data 9th bit no USB-serial adapter can; listen-only protocol analyzer, prints each byte tagged C(control)/d(data).
- ✅ **Sniffed the full program library** (2026-06-25) with the 9-bit sniffer + `tools/sniff_sampler.py`: all **99 programs + 3 function previews** captured. Decoded the **data plane** completely — every program is `55 40` + 8 fixture bytes on a heartbeat timebase (`0xC0` refresh marker); **no ARM/START/FIRE on the wire** → addressing is frame-relative.
- ✅ **Fixture model confirmed: 8 heads, 1 byte each** (this controller) — verified empirically by decoding chase/wash programs (chase walks byte-by-byte across exactly 8 positions; wash drives full-range 0–255 values that can't be nibbles). The firmware's 2-per-byte is the separate 256-head SKU. See `firmware-analysis/04` + the protocol spec's "Live OEM controller capture".
- ✅ **Program catalog + web visualizer** (`tools/pattern_catalog.py` → `captures/sniff/preview.html`): auto-classifies motion, collapses heartbeat re-sends to true states, legend + per-fixture readout + rename/export. Per-fixture byte is a single ~8-bit intensity (`00`=off); see master-sweep finding below.
- ✅ **Control plane finished** (2026-06-26): the operating control plane is the `0x00` heartbeat (master timebase) **alone**. `0xC0`/`0x80` were proven to be **sniffer decode artifacts** — misread heartbeats from a late start-edge `t0` (98.4% embedded in heartbeat runs, uniform position, exact bit mechanism); no ARM/START/FIRE/CLEAR/SPECIAL ever on the wire. Sniffer sampling phase fixed in `bridge/src/dataflash_rx.cpp` (data bits at 0.30/window). See `firmware-analysis/04`.
- ✅ **Per-fixture byte = single ~8-bit intensity** (2026-06-27, master-knob sweep on a sparse program: head byte drops monotonically `0x80`→`0x55`→`0x17`→`0x0F`). NOT a `[mode|intensity]` nibble split — the 8-head product affords full 8-bit/fixture vs the 256-head's 2×4-bit. **Flash/Fire button = all heads full (`80×8`) via the ordinary `55 40` broadcast**, no markers. See `firmware-analysis/04` + protocol spec.
- ⬜ Whether strobe-*effect* programs encode any flash-rate/mode in the byte (steady programs are pure intensity)
- ⬜ Hardware-verified end to end on a real fixture

## Protocol cheat-sheet (authoritative: protocol/dataflash-protocol-spec.md)
- Physical: **RS-422/485 differential** (DS8921), 3-pin — **Pin1=GND, Pin2=Data−, Pin3=Data+** (DMX convention). Each fixture is an active repeater w/ power-off relay bypass.
- Serial: **375000 baud, 9 data bits** (8051 Mode 2, SMOD=1, 12 MHz). No DMX break.
- **9th bit = control/data flag** (1=control, 0=data). Markers: `0x55`=ARM, `0x7F`=START, `0x00`=HEARTBEAT(~120Hz), `0xF7`=STOP/FIRE, `0xFF`=CLEAR, `0x12+95 88 25 A8`=special.
- Data byte: **8-head controller (the one we captured)** = **1 fixture per byte, 8-bit** (`55 40` + 8 bytes = 8 fixtures; chassis says "EIGHT FIXTURES LINEAR", verified empirically). **256-head firmware variant** = 2 fixtures × 4-bit (byte index=addr÷2, nibble=addr&1). Same `55 40` framing family, two product variants.
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
dataflash_rx.* 9-bit RX SNIFFER (bit-bang on core 1): recovers value + 9th bit [DF_SNIFF_MODE build only]
audio.*       I2S audio in (mic=I2S1 INMP441 / line-in=I2S0, SW-select); DSP task -> envelope=g_pat.audioLevel (Modulate) + onset->g_pat.beatTicks (Audio1) [DF_AUDIO build only, env esp32-s3-audio]
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
- `esp32-s3-sniff` — S3 **9-bit RX sniffer** (listen-only analyzer; `-DDF_SNIFF_MODE`). Wire MAX485 to receive: RE=DE=GND, RO→GPIO16, A/B on the link. Prints `<hex>C` (control 9th=1) / `<hex>d` (data 9th=0).
```bash
cd bridge && pio run -e esp32-s3-eth -t upload && pio device monitor -e esp32-s3-eth
```

## Next tasks (suggested order)
1. **Build & flash**, scope `DF_TX_PIN`: ~2.667 µs bit time, 11-bit frame, 9th bit 1=control/0=data.
2. **Live-capture a real controller** -> `captures/`; flip the spec's [?] items (ordering, heartbeat cadence, `0xFF`/`0x12`, nibble mapping). Use `nibbleSwap` if even/odd land wrong.
3. **Find the duty/cooldown ceiling** on real heads (fast modes drop flashes by thermal self-protection) — note safe ranges for speed/factor/bpm.
4. **TouchOSC**: build the 3-page layout per `CONTROL-TOUCHOSC.md`; generate builder-grid Lua + feedback routing for the chosen grid size.
5. **Audio-in (in progress, env `esp32-s3-audio`):** dual-I2S (INMP441 mic / line-in ADC, SW-select) DSP task drives Modulate (`audioLevel`) + Audio-1 beat-advance (`beatTicks`). DONE: capture+envelope+onset, OSC `/df/audio/*`, builds clean. TODO: verify I2S pins on real S3-ETH header (placeholders 8-13), line-in MCLK if the PCM card needs it, Audio-2 (beat-halt), web-UI controls + meter, FFT frequency-banding (modern enhancement).
6. Later: OLED+encoder front panel (`ui.h`); non-blocking RMT; custom hardware.

## Conventions
- Working dir `/Users/runnerr0/Temp/dataflash-re`. Spec-driven: `protocol/...spec.md` is the source of truth; tag claims [C]/[P]/[S]/[?].
- US Patent 5,078,039 is public domain. Schematic PDF is scanned: render fixture sheets p26 (CPU/ROM), p27 (Data Link Drivers), p29 (Zero Cross) with `pdftoppm -r 300`.
- Interop RE of owned hardware — keep work documented and reproducible.
