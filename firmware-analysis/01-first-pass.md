# Firmware analysis — first pass

## Source
- `assets/firmware/df282.exe` — ZIP archive (mislabeled `.exe`), dated 1994-02-16.
- Contains one member: `Df31f2.82`, 65536 bytes (a 27C512-class 64 KB EPROM image).
- De-mirrored image extracted to `assets/firmware/dataflash-strobe-head-fw-2.82-16k.hex` (Intel HEX, sparse).

## Identity (from embedded string @ 0x0025)
> Copyright 1989 Lightwave Research Inc. All rights reserved. Rev 2.82 — Data Flash strobe head. Author - Steve Tulk

This is the **fixture (strobe head)** firmware, not the controller. That's exactly what we want: it contains the *receiver*-side protocol parser.

## Memory map
- CPU: **8051 family** (Intel MCS-51). Confirmed by the interrupt vector layout, opcodes, and SFR usage.
- The 64 KB image is the unique **16 KB** mirrored 4× (all four 16 KB banks byte-identical → upper address lines A14/A15 not decoded).
- Within the 16 KB, all real content is in **0x0000–0x0FFF (4 KB)**; 0x1000–0x3FFF is erased (0xFF).
- So the entire program is ~3,125 bytes of code+data in the low 4 KB.

## Interrupt vector table
| Addr | Vector | Contents | Meaning |
|---|---|---|---|
| 0x0000 | RESET | `AJMP 0x0187` | jumps to init |
| 0x0003 | INT0 | `CLR EX0 ; RETI` | self-disabling stub |
| 0x000B | TIMER0 | `LJMP 0x0440` | active ISR |
| 0x0013 | INT1 | `CLR EX1 ; RETI` | self-disabling stub |
| 0x001B | TIMER1 | `LJMP 0x0481` | active ISR |
| 0x0023 | SERIAL | `CLR ES ; RETI` | serial IRQ disabled → **RX is polled, not interrupt-driven** |

## Serial / UART configuration
- `0x01A5: MOV SCON,#0x90` → **Mode 2** (9-bit UART), REN=1 (receive enabled).
  - Mode 2 = fixed baud = fosc/32 or fosc/64. A 16 MHz crystal at /64 yields **250 kbaud = DMX rate**; the 9th bit is the classic trick for handling DMX's 2 stop bits / framing. Strongly consistent with the "DMX-like" hypothesis. **Confirm the crystal value on the board to lock down baud.**
- `MOV TMOD,#0x11` (both timers 16-bit mode 1) at several sites; Timer0/Timer1 ISRs handle flash/break timing (preloads like TH0=0xB8, TH0=0xAA seen).

## Protocol parser — located
Reception is polled (`JNB RI` / `MOV A,SBUF`). Two regions:
- **0x0571 / 0x05C7** — receive helper(s).
- **0x0BC0–0x0CA0** — the main frame parser: a tight sequence of `JNB RI → MOV A,SBUF` reads (0x0BCF, 0x0BDE, 0x0BED, 0x0BFC, evenly ~0x0F apart) reading consecutive frame fields, with compares (`CJNE`/`C3 94 xx` subtract-and-test) branching on byte values — i.e. matching a header/start-code then dispatching. There is a `MOV 0x99,...`/`E5 99` SBUF read at 0x0C00 feeding a value-dispatch (`64 7F`, `64 55`, `64 F7`, `64 00`, `64 FF`, `64 12` — XRL #imm comparisons against marker bytes).

Those literal compare values (0x7F, 0x55, 0xF7, 0x00, 0xFF, 0x12) are candidate **start codes / command markers** in the Data Flash framing — top priority to decode next.

## Next steps
1. Full 8051 disassembly of 0x0000–0x0FFF (annotate from these anchors).
2. Decode the 0x0BC0–0x0CA0 parser into the actual frame format: start/marker bytes, address field, the intensity/rate/duration bytes (RAM regions 0x36–0x4B are zeroed at init and look like the channel/state buffer), any checksum.
3. Cross-check against the schematic: identify the UART transceiver, the crystal (→ exact baud), and the lamp-trigger output pin (toggled via P3 bits in the Timer ISRs).
4. Validate against a live capture from the working controller (scope for break/bit timing, logic analyzer for the byte stream).

## Open
- Exact crystal frequency (→ confirms 250 kbaud).
- Whether framing carries a DMX-style break, or a marker-byte protocol (the 0x55/0xF7/0xFF markers suggest possibly the latter, or a hybrid).
- Fixture addressing mechanism (DIP switches read via a port? look for a port read masked into an address compare).
