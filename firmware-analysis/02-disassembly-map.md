# Firmware analysis — disassembly map (control path)

8051 disassembly of the unique 16 KB image. Tool: `dis51.py` (in this folder). Source of truth for `protocol/dataflash-protocol-spec.md`.

## Prior art (found during this pass)
- **US Patent 5,078,039** — "Microprocessor controlled lamp flashing system with cooldown protection," Tulk & Belliveau, Lightwave Research, filed 1990-08-08, granted 1992-01-07. Same author as the firmware. Public-domain; describes the packet (arm/start/data/heartbeat/stop), 375K baud, 256 fixtures, 16 intensity levels, 8 DIP address switches, cooldown/max-intensity protection. This is the protocol's semantic spec.
- Community: original Dataflash uses Lightwave Research "LWR" native protocol (distinct from DMX); third-party/DIY converters have existed but no published byte-level spec found. Our reconstruction appears to be the first written byte-level spec.

## Key addresses
| Addr | Role |
|---|---|
| 0x0187 | RESET init |
| 0x01A5 | `MOV SCON,#0x90` (Mode 2, REN) |
| 0x0C54+ | main serial-service loop |
| 0x0C59–0C61 | SCON/PCON setup: Mode 2, REN, **SMOD=1** → fOSC/32 = 375 kbaud @12 MHz |
| 0x0C87 | `JNB RB8` — 9th bit routes control vs data |
| 0x0C90 | data-byte path: position counter 0x34, capture intensity |
| 0x0C06 | control-byte dispatcher (compares markers) |
| 0x0A87 | START handler (0x7F): DIP read, addr→0x34 |
| 0x0A9E | ARM handler (0x55) |
| 0x0AAC | CLEAR handler (0xFF) |
| 0x0B52 | HEARTBEAT handler (0x00): cooldown decrement, timers |
| 0x0B86 | STOP/FIRE handler (0xF7): enable firing; trigger asserted on P3.4=FIRE in the zero-cross-synced timer path (see 03) |
| 0x0BC6 | SPECIAL (0x12 + `95 88 25 A8`) → 0x051D |
| 0x0874 | max-intensity compute / cooldown helper |
| 0x0ABA, 0x07D1 | cooldown register math |
| 0x0140–0x0180 | per-intensity heat-value / gamma lookup tables |

## I/O pins (8051 ports)
> Corrected against the schematic (see `03-schematic-findings.md`, which is authoritative for pin assignments). The earlier first-pass guesses for P3.2/P3.3/P3.4/P3.7 were wrong; the protocol spec already adopted the corrected map.
- **P1** — DIP address switch read bus (input; strobed by P3.5).
- **P3.2** — **WATCHDOG** kick output (the `SETB/CLR P3.2` pulses; NOT the fire trigger).
- **P3.3** — **#ARM** output.
- **P3.4** — **FIRE** (strobe trigger), asserted in the zero-cross-synced timer path.
- **P3.5** — **ADDRESS** — DIP latch/enable strobe.
- **P3.6** — mode / lookup-table (personality) select; also 110/220 sense path (input).
- **P3.7** — **#LED** output.

## Confidence
- 9-bit/375k mechanism, marker→role bindings, 2-fixtures-per-byte, positional addressing, nibble select: **high** (directly disassembled + patent agreement).
- Exact packet ordering / heartbeat cadence: **medium** — confirm on live capture.
