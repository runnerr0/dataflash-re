# Schematic findings — original Dataflash fixture board

Read from `assets/schematics/dataflash-original-schematics.pdf` (rasterized at 300 DPI). The doc covers three boards: Controller Mother Board (PDF pp ~4–20), Controller Front Panel (~20–25), and the **Fixture Board "DF-1" (9 sheets)** — the strobe head we care about.

## Fixture board sheet index (PDF page → title)
| PDF p | Sheet | Notes |
|---|---|---|
| 26 | CPU AND ROM (1/9) | the control core |
| 27 | DATA LINK DRIVERS (5/9) | **physical layer** |
| 29 | ZERO CROSS DETECTION (4/9) | AC phase reference |
| 28,31,32,33,34 | power supply / flash trigger / watchdog reset / thermal | high-voltage strobe circuit (not needed for protocol) |

## CPU AND ROM (PDF p26) — confirms the firmware analysis
- **IC10 = 8031/8051** (jumper-selectable internal/external via #EA). 8-bit MCS-51. ✓
- **X1 = 12 MHz** crystal, C1/C2 = 27pF. ⇒ UART Mode 2 + SMOD=1 = fOSC/32 = **375 kbaud**. ✓ (matches patent + firmware)
- **IC5 = 27128 = 16 KB EPROM** (A0–A13). Confirms the dump is the true 16 KB image mirrored ×4 in the 64 KB file. ✓
- **IC1 = 74373** address latch (AD0–7 demux), ALE/#PSEN standard external-ROM wiring.
- **Serial in = RXD = P3.0 (pin 10).**

### Port 3 map (corrects an earlier firmware guess)
| Pin | Signal | Dir |
|---|---|---|
| P3.0 / RXD | serial data in | in |
| **P3.2** | **WATCHDOG** kick | out |
| **P3.3** | **#ARM** | out |
| **P3.4** | **FIRE** (trigger) | out |
| **P3.5** | **ADDRESS** (DIP read strobe) | out |
| P3.6 | mode/lookup select (also 110/220 sense path) | in |
| P3.7 | #LED | out |

> Correction: the `SETB/CLR P3.2` pulses seen in the firmware are **watchdog kicks**, not the fire trigger. The actual trigger is **P3.4 = FIRE**, asserted in the phase-controlled (zero-cross-synchronized) timer path, not directly in the STOP handler. Update applied to the protocol spec.

- Config jumpers: 110/220 V select (R3) and internal/external sync (R4). Zero-cross input feeds phase control.

## DATA LINK DRIVERS (PDF p27) — the physical layer
- **IC16 = DS8921** — National **RS-422/RS-485 differential driver+receiver**. So the link is differential serial (electrically DMX-like).
- **3-pin connector pinout (Data In and Data Out are separate connectors):**
  - **Pin 1 = Ground / shield**
  - **Pin 2 = Data−** (R58 24K pull-down — fail-safe bias)
  - **Pin 3 = Data+** (R57 24K pull-up — fail-safe bias)
  - Same pin convention as DMX512. (Connector is 3-pin XLR per manuals/community; earliest units used ¼".)
- Receiver output → two 74xx gates → **RXD** to CPU (TP10 test point on RXD).
- The DS8921 **driver re-transmits to DATA OUT** ⇒ **each fixture is an active repeater** (series RS-422 hops), not a passive multidrop bus.
- **RL1 relay** bypasses DATA IN→DATA OUT when the fixture is unpowered, so the daisy chain survives a powered-off unit.

## Implications for the bridge
- Drive **fixture #1's DATA IN** with a standard RS-485/RS-422 differential driver (e.g. MAX485/SN75176): Pin3=A/Data+, Pin2=B/Data−, Pin1=GND. The chain self-repeats downstream.
- Provide the 24K-style idle bias is already on the fixture; just drive clean differential.
- UART: **375000 baud, 9 data bits** (the 9th bit = control/data flag). No DMX break needed.
- We do **not** need to reproduce zero-cross/phase control or cooldown — the fixtures do that locally. The bridge only emits the serial packet.

## Still unconfirmed
- DIP-switch sheet not separately located, but firmware already gives the addressing (8 active-low switches on P1, strobed by P3.5; byte index = addr÷2; nibble = addr&1).
- Live capture still wanted to confirm packet ordering and heartbeat cadence.
