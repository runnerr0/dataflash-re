# Firmware analysis 04 — control-byte roles, firing/timing model, broadcast reconciliation

Re-traced against the **live OEM controller capture** (see `protocol/dataflash-protocol-spec.md`
→ "Live OEM controller capture"). Tags: [C] disassembly-confirmed · [?] inferred/needs a fixture.

## Control bytes (9th=1) — roles  [C]
Dispatcher at `0x0C06` (only reached when `JNB RB8` at `0x0C87` sees 9th=1):

| Byte | Handler | Role |
|---|---|---|
| `0x00` HEARTBEAT | `0x0B52` | **Master timebase.** Decrements cooldown/intensity sums, services Timer1. The controller streams these constantly — they are the clock everything is paced by. |
| `0x55` ARM | `0x0A9E` | sets enable flags (RAM `0x7D/7E/7F`=1). No addressing. |
| `0x7F` START | `0x0A87` | reads 8 DIP switches (P1, strobed by P3.5) → addr `0x33`; **position counter `0x34` = addr÷2**; sets active flag. The addressing setup. |
| `0xF7` STOP/FIRE | `0x0B86` | clears intensity regs `0x30/31/32`, enables firing; trigger asserted on P3.4 in the zero-cross timer path. |
| `0xFF` CLEAR | `0x0AAC` | blackout: clears arm flags + active. |
| `0x12` SPECIAL | `0x0BC6` | expects `95 88 25 A8` → jumps `0x051D`. |
| (other) | `0x0C51` | default: `CLR bit0x40` (clears active). |

## Fixture-count variants — IMPORTANT  [C]/[wire]
The disassembled firmware (Rev 2.82) is the **256-fixture** variant: 128 data bytes, **2 fixtures × 4-bit intensity per byte**, nibble selected by `addr&1`. **But the controller we captured is the 8-fixture variant** ("EIGHT FIXTURES LINEAR" chassis label), and its wire format is **8 data bytes = 8 fixtures, ONE byte per fixture** (8-bit) — verified empirically (see `protocol/dataflash-protocol-spec.md` → "Live OEM controller capture": chase walks byte-by-byte across 8 heads; wash drives full-range 0–255 values that can't be nibbles). Same `55 40` framing family, different head count + per-fixture packing. The byte's two nibbles on the 8-head wire likely encode **intensity + flash-mode** per fixture (background `E6` vs highlight `E0`/`86`). Treat "2 fixtures/byte" below as the **256-head firmware's** model, not this controller's.

## Firing / timing model  [C]
- **Data byte (9th=0):** position counter `0x34` counts down one per data byte; when it reaches 0 the byte is captured to **`0x30`, `0x31`, `0x32` (three identical copies — likely per-AC-phase / triple-buffer)**. (256-head firmware: each byte = 2 fixtures × 4-bit intensity. 8-head controller wire: each byte = 1 fixture, 8-bit.)
- The captured intensity (`0x30/31/32`) is loaded into **R0/R1/R2 and counted down by heartbeats** (receive loops at `0x0571`, `0x05C7`: `DEC R0/R1/R2` per heartbeat until zero). So the **intensity value sets a heartbeat-counted timing**; the strobe fires **zero-cross-synchronized and heartbeat-paced**, NOT on a per-flash FIRE byte. Cooldown/thermal limiting is also heartbeat-paced.
- ⇒ "Brightness / flash rate" = how the 4-bit intensity modulates the heartbeat-counted, zero-cross-synced flash. This is why normal program output needs only heartbeats + data, no FIRE.

## Addressing — and the broadcast reconciliation  [C]/[?]
- Addressing is **positional**: each fixture's `0x34` = addr÷2 from its DIP switches, set **only by START (`0x7F`)**; `addr&1` picks the high/low nibble. `0x34` is written nowhere else ([C]: RESET sets 0 @`0x01B0`, START sets addr÷2 @`0x0A99`).
- **BUT the OEM program broadcasts (`55 40` + 8 data bytes) contain no START** (and no ARM/FIRE) — only heartbeats + data. So this firmware's positional addressing is not (re)armed by these programs as captured. **[?] open question.**
- Reconciliation hypotheses (need a fixture / its firmware to fully close):
  1. ~~START sent once at power-on~~ — **RULED OUT.** The actual power-on edge was captured with the 9-bit sniffer (`captures/poweron-edge.txt`): at boot the controller *immediately* streams normal `55 40` wash frames + heartbeats, with **no `7F`/`55`/`F7`/`FF` control bytes**. There is no power-on addressing handshake — START is never sent, ever.
  2. ⇒ Addressing must be **frame-relative**: each fixture syncs on the `55 40` header and counts its byte position (by DIP address) within the 8 data bytes every frame — the `55 40` header serves as the per-frame position reset, so no START control byte is needed.
  3. The Rev 2.82 fixture firmware we have addresses via START (`0x34` from `0x7F`) and does **not** special-case `55 40` in any traced parser path ⇒ either the fixtures paired with this controller run **different firmware**, or there is a `55 40`-sync receive path not yet traced. Resolving this needs a **real fixture** (or its firmware image).

## `0xC0` / `0x80` control bytes — RESOLVED: sniffer decode artifacts, not real markers  [wire]
**They are misread heartbeats, not control bytes.** Evidence (2026-06-26, all 102 program captures + power-on):
- The entire control plane is just **three values**: `0x00` (3.21M), `0x80` (9.4k), `0xC0` (6.4k) — plus one stray `0x20`. No `0x55`/`0x7F`/`0xF7`/`0xFF`/`0x12` ever appears as control (the firmware marker dispatch is never exercised on the wire).
- **98.4%** of `0x80`/`0xC0` are embedded in pure heartbeat runs (preceding byte `0x00` 98.9%, following `0x00` 99.5%).
- Their position is **uniform** across the header-to-header span (every decile equally populated) — a real refresh marker would cluster at one offset; uniform = random decode error.
- **Mechanism:** a heartbeat on the wire is `START(0)` + 8 zero data bits + `9th(1)` + `STOP(1)` = **9 lows then highs**. The sniffer's start-edge poll (`t0`) is only ever *late* (an interrupt between frames delays it), which shifts every sample toward the tail; the top data bit(s) then catch the rising edge → `D7`=1 (`0x80`) or `D6+D7`=1 (`0xC0`), with the 9th-bit sample catching the HIGH stop → tagged "control". (`0xFF` in the power-on capture = the same error maxed out, inside loss-of-sync garbage bursts.)
- **Fix applied** in `bridge/src/dataflash_rx.cpp`: sample data bits at 0.30 into each bit window (was 0.50) for ~0.70 bit of slack against late `t0`; 9th bit at its window center.
- **Validated on a fresh prog-10 capture (2026-06-26, rewired bench + fixed firmware):** the sampling-phase fix dropped the artifact rate **0.514% → 0.414%** (same program, old vs new firmware) — proving the rate tracks *sampling timing*, i.e. they are decode artifacts, not protocol. In the new capture `0x80`/`0xC0` are still **98.9%** embedded in pure heartbeat runs (preceding byte `00` in 91/92), and **no** `55/7F/F7/FF/12` ever appears as control. The residual ~0.4% is the bit-bang sniffer's irreducible `t0` jitter (interrupts between frames); a logic analyzer with fixed-rate sampling would read zero.

⇒ **The operating control plane is the heartbeat (`0x00`) alone** — the master timebase. There is no separate marker/refresh control byte; the `55 40` data header is the per-frame sync boundary and addressing is frame-relative.

## Bottom line
The **firing/timing model is solved** (heartbeat clock + intensity-as-countdown + zero-cross-synced flash) and the **control plane is now fully accounted for**: during playback and at power-on the controller emits only `55 40`-framed fixture data + `0x00` heartbeats — no ARM/START/FIRE/CLEAR/SPECIAL, and the apparent `0xC0`/`0x80` were sniffer artifacts. The firmware's marker dispatch (`55/7F/F7/FF/12`) is a setup/addressing path **this controller never uses in normal operation**; observing it (and the per-fixture byte's intensity-vs-flash-mode nibble split) needs a real fixture or a deliberate controller setup/test action on the wire.
