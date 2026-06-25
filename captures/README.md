# captures/

Logic-analyzer / serial captures of the Dataflash link. Raw dumps (`*.bin`, `*.sr`, `*.vcd`)
are git-ignored; keep this guide and any small decoded/annotated logs.

## Capturing the REAL original controller / encoder

This is the capture that flips the spec's remaining `[?]` items — what the *original gear*
actually emits, vs. what our bridge emits. The bridge has only proven it emits the spec;
the OEM controller is the ground truth.

### Wiring
The controller's 3-pin output → USB-RS485 adapter:

```
controller Pin 3 (Data+) → adapter A
controller Pin 2 (Data−) → adapter B
controller Pin 1 (GND)   → adapter GND
```
If it decodes inverted/garbled, swap A/B. Bench runs are usually fine without termination;
add 120 Ω across A/B if marginal. The controller drives the bus — you are only listening.

### Capture command
Use the **8-data + parity** trick so a standard USB-serial adapter frames the 9-bit protocol
cleanly (the 9th bit lands in the parity slot, the stop bit stays aligned):

```bash
# EVEN parity is what works on macOS pyserial (MARK/SPACE raise "Invalid parity").
python3 ../tools/dataflash_frame.py --capture /dev/cu.usbserial-XXXX --save real-controller.bin
```
(`--save` writes the raw byte stream for offline re-analysis; the decoder prints framed refreshes live.)

### What to look for — resolves these spec `[?]` items
- **Packet ordering** and whether `ARM`/`START` repeat every refresh or are sent once.
- **Heartbeat cadence** — measure the `0x00` interval (spec says ~120 Hz, [?]).
- **`0xFF` (CLEAR)** and the **`0x12` + `95 88 25 A8`** special/diag sequence — are they used in normal operation?
- **Nibble mapping vs OEM** — the big one (see below).

### Method: nail nibble mapping against the OEM controller
1. On the real controller, address **one** fixture/channel to a known address and set it to a
   distinct level (e.g. 15), everything else at 0/min.
2. Capture; find the single non-zero data byte and which **nibble** (high/low) holds the 15.
3. Compare to our bridge for the same address+level (we measured: addr 0 → byte0 `F0`,
   addr 1 → byte0 `0F`, i.e. even→high / odd→low). If the OEM controller disagrees, set
   `nibbleSwap` in the bridge config.
4. Repeat for an odd address to confirm the high/low assignment both ways.

### Naming
`real-controller-YYYYMMDD-<what>.bin` (e.g. `real-controller-20260625-addr0-lvl15.bin`).
Note the controller's settings (addresses, levels, mode) alongside each capture.
