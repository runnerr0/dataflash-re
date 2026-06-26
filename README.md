# dataflash-re

Reverse-engineering the original **Lightwave Research / High End Systems "Dataflash"** xenon-strobe control link — the proprietary **"LWR"** protocol, *not* the later DMX-native AF1000 — and building an **ESP32 + RS-485 bridge** so the fixtures can be driven from modern controllers (DMX / Art-Net / sACN / TouchOSC).

> **Status:** protocol decoded, and the bridge's **transmit path is wire-verified on real hardware** — 137/137 frames decoded byte-exact off a logic capture. What's left needs an original controller and/or a real fixture. See [Status](#status).

## Why this project

### Lightwave Research & High End Systems

In 1985, a few people in Austin, Texas — Richard Belliveau, Lowell Fowler, David Blair, and Bob Schacherl — started building lighting that would reshape what a club, a stage, and a festival could look like. **Lightwave Research** was Belliveau's engineering-and-manufacturing arm; **High End Systems** was the name on the front of the box. Together they did things *first*:

- They built the entertainment industry's **first optical thin-film coating lab** to make their own dichroic glass — color that punched hard and didn't fade.
- The **Color Pro** (1987) was their first fixture. The **Intellabeam** (1989) moving-mirror scanner put automated lighting on club ceilings, and by 1991 it was on Dire Straits' world tour — the business was never the same.
- A relentless run followed — **Cyberlight**, **Studio Color**, **Trackspot**, **Technobeam**, **Showgun** — and in 2000 the **Catalyst** media server helped invent the entire digital-lighting / media-server category.
- They merged with **Flying Pig Systems** (the Wholehog console) in 1999, were acquired by **Barco** in 2008, and finally by **ETC** in 2017, where the legacy lives on.

Richard Belliveau's list of inventions reads like a history of modern entertainment lighting. The **Dataflash** is on it.

### The Dataflash

The Dataflash is a **xenon strobe** — a real flashtube, not an LED panel imitating one. It throws a hard, brilliant, instantaneous burst that an LED simply cannot reproduce: the crack of white that cuts through fog and pins a whole dancefloor for a single frame. Daisy-chain up to **256** of them, give each a 4-bit intensity and a DIP address, and you can run waves of light across a room over the proprietary **"LWR" control link** this project decodes. Onboard thermal/cooldown protection keeps the tubes alive — these were built to be driven hard, and they've survived decades of exactly that.

### Keeping them running

That survival is the whole point. These fixtures are beautiful, overbuilt, and increasingly orphaned — the original controllers are scarce and the proprietary link locks them out of modern rigs. **The objective here is simple: keep Dataflashes firing, on modern control.** Decode the protocol, build a bridge (DMX / Art-Net / sACN / TouchOSC → the original 9-bit/375k link), and put these strobes back in a contemporary show.

That's the spirit of **Radiant Atmospheres**, the anarchist lighting collective this work belongs to — a hodgepodge of lighting nerds from around the California Bay Area (and well beyond), brought together to share the beauty of the lights at underground parties and festivals. We keep legacy lighting alive not behind museum glass but out in the world: still blowing minds, still making the kind of moments only real, physical, gorgeously-engineered light can make. Old lights, new tricks, same magic.

## How the protocol was reconstructed

Triangulated from three independent, agreeing sources. Every claim in [`protocol/dataflash-protocol-spec.md`](protocol/dataflash-protocol-spec.md) is tagged **[C]** firmware-confirmed · **[P]** patent · **[S]** schematic · **[?]** needs live capture.

1. **US Patent 5,078,039** (Tulk & Belliveau, Lightwave Research, 1992) — protocol semantics. Public domain (expired).
2. **Fixture firmware Rev 2.82** — 8051 disassembly of the strobe-head EPROM confirms the bit-level mechanism ([`firmware-analysis/`](firmware-analysis/)).
3. **Original schematics** — CPU, baud, transceiver, and connector pinout ([`firmware-analysis/03-schematic-findings.md`](firmware-analysis/03-schematic-findings.md)).

## Protocol at a glance

- **Physical:** RS-422/485 differential (DS8921), 3-pin — **Pin 1 = GND, Pin 2 = Data−, Pin 3 = Data+** (DMX convention). Each fixture is an active repeater with a power-off relay bypass.
- **Serial:** **375000 baud, 9 data bits** (8051 UART Mode 2, SMOD=1, 12 MHz). No DMX break.
- **9th bit = control/data flag** (1 = control, 0 = data). Firmware marker set: `0x55`=ARM, `0x7F`=START, `0x00`=HEARTBEAT (~120 Hz), `0xF7`=STOP/FIRE, `0xFF`=CLEAR.
- **Fixtures per byte — two product variants:** the **8-head controller we captured** sends **one byte per fixture** (8-bit), `55 40` + 8 bytes = 8 fixtures ("EIGHT FIXTURES LINEAR" on the chassis, confirmed empirically). The **256-head firmware variant** packs **two fixtures × 4-bit** per byte (byte = addr ÷ 2, nibble = addr & 1). Same `55 40` framing family.
- **What the controller actually broadcasts (live capture):** `55 40` + 8 fixture bytes, repeated on a heartbeat timebase with a `0xC0` refresh marker — and **no ARM/START/FIRE** at all (those never appear on the wire across all 102 programs). Fixtures sync frame-relative to the `55 40` header. The per-fixture byte is intensity + flash-mode coded, not a linear ramp.

Full detail and the bridge implementation note (9-bit TX is done with the ESP32 **RMT** peripheral, since classic UART can't hold a constant 9th bit) are in the spec and [`bridge/README.md`](bridge/README.md).

## Repository layout

```
dataflash-re/
├── protocol/            dataflash-protocol-spec.md   ← the reconstructed spec (source of truth)
├── firmware-analysis/   8051 disassembly notes + dis51.py (the OEM image itself is not included)
├── bridge/              ESP32 firmware: RMT 9-bit/375k TX, Art-Net/sACN/OSC in, WiFi STA+AP, web UI
│   ├── src/             inputs/osc/patterns/dataflash_tx/net/webui
│   ├── platformio.ini   envs: esp32-poe-iso · esp32-s3-eth · esp32-c3
│   ├── PATTERNS.md  CONTROL-TOUCHOSC.md  BENCH-VALIDATION.md
├── tools/               dataflash_frame.py (golden framing); sniff_sampler.py + pattern_catalog.py (program-library capture, auto-naming, web preview)
├── captures/            logic-analyzer / serial captures + capture guide (raw dumps gitignored)
└── assets/              MANIFEST.md (provenance). OEM firmware/manuals/schematics NOT included — see below.
```

## The bridge (ESP32 firmware)

An Art-Net / sACN **target** and a TouchOSC-driven **controller** that re-emits a DMX universe (or an on-device strobe pattern) as the Dataflash 9-bit/375k RS-485 link.

```bash
cd bridge
pio run -e esp32-s3-eth -t upload        # or -e esp32-poe-iso (production) / -e esp32-c3
pio device monitor -e esp32-s3-eth
```

First boot with no saved Wi-Fi raises a config AP (`dataflash-bridge` / `dataflash`, http://192.168.4.1); set your network from the **Wi-Fi** card and it joins as a station thereafter. Output test modes (All-on / Chase / Single) validate the wire without a controller. Pin map and board notes are in [`bridge/README.md`](bridge/README.md); hardware bring-up steps in [`bridge/BENCH-VALIDATION.md`](bridge/BENCH-VALIDATION.md).

## Tools

[`tools/dataflash_frame.py`](tools/dataflash_frame.py) — host-side, no hardware needed to self-test:

```bash
python3 tools/dataflash_frame.py                       # print golden vectors + run framing self-test
python3 tools/dataflash_frame.py --capture <port>      # decode a live USB-RS485 capture (8-data+parity)
```

It mirrors the bridge's `sendRefresh()` exactly, round-trip self-tests, and decodes a capture against the golden vectors. The capture path uses the **8-data + parity** trick to read this 9-bit protocol on an ordinary USB-serial adapter (the 9th bit lands in the parity slot, keeping the frame aligned). See [`captures/README.md`](captures/README.md).

**Program-library tools** (used with the `esp32-s3-sniff` 9-bit sniffer):

```bash
python3 tools/sniff_sampler.py                         # hit a program on the controller, Enter to record each
python3 tools/pattern_catalog.py                       # auto-name every program + build captures/sniff/preview.html
```

`sniff_sampler.py` records the controller's output per program over the sniffer's USB-CDC; `pattern_catalog.py` extracts each program's distinct strobe states, auto-classifies the motion (chase / bounce / ramp / sparkle / wash / …), and emits a self-contained **web visualizer** (`preview.html`) with a legend, per-fixture readout, and rename/export — open it to watch and name all 99 programs + 3 function previews. (Captures are local/gitignored.)

## Status

- [x] Assets identified; EPROM extracted & disassembled (8051, Rev 2.82)
- [x] Prior art found (US Patent 5,078,039); schematic read (CPU, 12 MHz, DS8921, pinout)
- [x] **Protocol spec drafted** ([`protocol/`](protocol/))
- [x] **Bridge firmware** (4 build targets); RMT 9-bit/375k TX, Art-Net/sACN/OSC, WiFi STA+AP, web UI
- [x] **TX chain wire-verified** on ESP32-S3-ETH via MAX485 → USB-RS485 loopback (137/137 refreshes byte-exact)
- [x] **Nibble packing verified** with an asymmetric payload (even→high, odd→low)
- [x] **Real OEM controller captured** — read via FTDI USB-RS485; **baud 375000 confirmed**, labeled program library captured. Firmware re-read confirms `0x7F`=START (loads DIP addressing) but it's **absent on the 8-bit wire** — the control plane needs the 9th bit.
- [x] **9-bit RX sniffer firmware** (`esp32-s3-sniff`) — listen-only analyzer that recovers the control/data 9th bit a USB-serial adapter can't.
- [x] **Sniffed the full program library** with the 9-bit sniffer — all **99 programs + 3 function previews** captured, decoded, auto-named, and rendered in a web preview ([`tools/pattern_catalog.py`](tools/pattern_catalog.py)).
- [x] **Fixture model confirmed: 8 heads, one byte each** (this controller) — verified empirically; resolves the 8-vs-256 / nibble-packing question.
- [x] **Data plane decoded** — `55 40` + 8 fixture bytes on a heartbeat timebase; no ARM/START/FIRE on the wire; addressing is frame-relative.
- [ ] **Finish the control plane**: confirm `0xC0`/`0x80` roles and the per-fixture byte's intensity-vs-flash-mode split (best done with a logic analyzer + a real fixture)
- [ ] **Drive a real fixture** end-to-end (confirm strobe + addressing; flip `nibbleSwap` if needed)

## Assets & provenance

The original **firmware image, user/AF1000 manuals, and schematics are copyrighted** by Lightwave Research / High End Systems / ETC and are **not included** in this repository (they are git-ignored). [`assets/MANIFEST.md`](assets/MANIFEST.md) documents each file and its original source (ETC's legacy High End Systems download archive) so anyone with a legitimate copy can drop them into `assets/` and reproduce the analysis. **US Patent 5,078,039 is public domain.**

This is interoperability reverse-engineering of independently-owned hardware, kept documented and reproducible.

## Safety (you read this far, so)

If you've gone deep enough into a repo about reverse-engineering a strobe to reach the bottom of the README, you probably already know this — but for the record: xenon strobes are *blindingly* bright and flash hard, so anyone photosensitive or prone to seizures should give them room (or sit a session out). And these are **high-voltage** devices — that's how you fire a flashtube — so don't go poking around inside one unless you know exactly what you're doing. Be careful, have fun, don't cook yourself or your friends.

## Acknowledgments

- **ETC (Electronic Theatre Controls)** for keeping the legacy High End Systems downloads alive — the manuals, schematics, and firmware that made this reconstruction possible are still out there because they've kept the archive up.
- Richard Belliveau, Steve Tulk, and the Lightwave Research / High End Systems crew, for building lights worth keeping running.

Built by **Alex Moening** for Radiant Atmospheres.

## License

[MIT](LICENSE) — covers the reconstructed spec, bridge firmware, tools, and analysis here. Third-party OEM materials are not covered and not redistributed.
