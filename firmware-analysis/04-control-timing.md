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

## Firing / timing model  [C]
- **Data byte (9th=0):** position counter `0x34` counts down one per data byte; when it reaches 0 the byte is captured to **`0x30`, `0x31`, `0x32` (three identical copies — likely per-AC-phase / triple-buffer)**. Each byte = 2 fixtures × 4-bit intensity.
- The captured intensity (`0x30/31/32`) is loaded into **R0/R1/R2 and counted down by heartbeats** (receive loops at `0x0571`, `0x05C7`: `DEC R0/R1/R2` per heartbeat until zero). So the **intensity value sets a heartbeat-counted timing**; the strobe fires **zero-cross-synchronized and heartbeat-paced**, NOT on a per-flash FIRE byte. Cooldown/thermal limiting is also heartbeat-paced.
- ⇒ "Brightness / flash rate" = how the 4-bit intensity modulates the heartbeat-counted, zero-cross-synced flash. This is why normal program output needs only heartbeats + data, no FIRE.

## Addressing — and the broadcast reconciliation  [C]/[?]
- Addressing is **positional**: each fixture's `0x34` = addr÷2 from its DIP switches, set **only by START (`0x7F`)**; `addr&1` picks the high/low nibble. `0x34` is written nowhere else ([C]: RESET sets 0 @`0x01B0`, START sets addr÷2 @`0x0A99`).
- **BUT the OEM program broadcasts (`55 40` + 8 data bytes) contain no START** (and no ARM/FIRE) — only heartbeats + data. So this firmware's positional addressing is not (re)armed by these programs as captured. **[?] open question.**
- Reconciliation hypotheses (need a fixture / its firmware to fully close):
  1. ~~START sent once at power-on~~ — **RULED OUT.** The actual power-on edge was captured with the 9-bit sniffer (`captures/poweron-edge.txt`): at boot the controller *immediately* streams normal `55 40` wash frames + heartbeats, with **no `7F`/`55`/`F7`/`FF` control bytes**. There is no power-on addressing handshake — START is never sent, ever.
  2. ⇒ Addressing must be **frame-relative**: each fixture syncs on the `55 40` header and counts its byte position (by DIP address) within the 8 data bytes every frame — the `55 40` header serves as the per-frame position reset, so no START control byte is needed.
  3. The Rev 2.82 fixture firmware we have addresses via START (`0x34` from `0x7F`) and does **not** special-case `55 40` in any traced parser path ⇒ either the fixtures paired with this controller run **different firmware**, or there is a `55 40`-sync receive path not yet traced. Resolving this needs a **real fixture** (or its firmware image).

## `0xC0` / `0x80` control bytes  [?]
Seen as control (9th=1) in every program, but **partly a 9th-bit decode artifact** (both end in a `1`; the single-bit `0x80` pulse is the marginal sniffer case). Neither is in the firmware's marker set, so as control they hit the default (clear-active) path. Real role TBD — needs the logic analyzer to confirm whether they're genuine markers or decode noise.

## Bottom line
The **firing/timing model is solved**: heartbeat clock + intensity-as-countdown + zero-cross-synced flash. The **control alphabet is mapped**. The one genuine gap is **how the broadcast addresses individual fixtures without START** — likely a power-on START or a firmware-version difference; resolving it needs a real fixture in the loop.
