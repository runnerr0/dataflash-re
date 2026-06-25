# Bench validation — scope / logic-analyzer the 9-bit TX

Turnkey procedure to flip the spec's open `[?]` items on real silicon. Pairs with
`../tools/dataflash_frame.py`, which prints the **exact** bytes/bits the firmware
emits so your capture has a golden target. No fixture required for steps 1–4.

Pins (from `src/config.h`): **DF_TX_PIN = GPIO4** (RMT TTL out → MAX485 DI),
**DF_DE_PIN = GPIO5** (driver enable, held HIGH). Probe **GPIO4** for the raw TTL
frame; probe the MAX485 **A/B** to validate the differential pair.

## 0. Flash + make output deterministic
```
cd bridge && pio run -t upload && pio device monitor   # 115200; note the IP
```
On the web UI (Ethernet IP, or WiFi AP `dataflash-bridge`/`dataflash` → 192.168.4.1):
set **Output test → All-on**, **level 8**, **fixture count 16**, **Output enable ON**.
That makes every refresh identical and trivial to decode.

Golden target for that exact config (ARM, START, 8 data bytes `0x88`, STOP):
```
python3 ../tools/dataflash_frame.py        # prints vectors + timing
# All-on level 8, 16 fixtures  -> 55+ 7F+ 88 88 88 88 88 88 88 88 F7+
#   (sigrok 9-bit words: 0x155 0x17f 0x088 ...x8... 0x1f7)
```

## 1. Bit timing (the riskiest claim — RMT 213 ticks)
Scope **GPIO4**, single shot on a falling edge.
- [ ] Bit period = **2.6625 µs** (213 ticks × 12.5 ns), i.e. ~375.6 kbaud. Tolerance ±2%.
- [ ] Frame = **11 bit-times**: start(0) + 8 data + 9th + stop(1), idle HIGH between frames.
- [ ] One full refresh ≈ **29.3 µs** for the 4-word case, **~3.84 ms** for a 256-fixture refresh (matches README "~4 ms"; the spec's "~6 ms" is the patent-era figure — update if confirmed).

## 2. Byte decode (logic analyzer, GPIO4)
Configure the UART decoder: **baud 375000, 9 data bits, no parity, 1 stop, LSB-first, idle high.**
(Most decoders that support "9 data bits" surface the 9th bit as bit 8 of each word —
so markers read `0x1xx` and data reads `0x0xx`, exactly the `sigrok` column above.)
- [ ] Word order is `ARM(0x55) START(0x7F) <data> STOP(0xF7)`.
- [ ] **9th bit = 1 on markers, 0 on data** — the core protocol claim. (firmware tests RB8 @0x0C87)
- [ ] Data bytes equal the golden `0x88…` for the all-on test.

## 3. Heartbeat cadence  [flips spec item]
Idle the output (no refresh between beats): with refresh at 44 Hz and heartbeat 120 Hz,
beats interleave between refreshes.
- [ ] Heartbeat word = `0x00` with **9th = 1** (`sigrok 0x100`).
- [ ] Measure actual beat interval; confirm/replace the spec's "~120 Hz [?]".

## 4. Differential layer (MAX485 A/B)
- [ ] A/B is a clean differential mirror of GPIO4; decodes identically as 375000-9.
- [ ] If A/B looks inverted vs the fixture's expectation, swap A/B (or note polarity).

## 5. On a real fixture  [flips nibble mapping + ordering]
Set a DIP address, drive **fixture #1 Data In** (Pin3=A/Data+, Pin2=B/Data−, Pin1=GND).
- [ ] Web UI **Single fixture** at the DIP address, level 15 → that head strobes.
- [ ] Walk addresses 0,1,2,3 → confirm **even→high nibble / odd→low nibble**. If even/odd
      land on the wrong heads, toggle **nibbleSwap** in the UI and re-test.
- [ ] Find the duty/cooldown ceiling: ramp refresh/factor/bpm until the head starts
      dropping flashes (thermal self-protect) — record safe ranges.
- [ ] If a real controller is available, capture it (`../captures/`) and diff ordering /
      heartbeat cadence against our emission to close the remaining `[?]` items.

## Results → docs
Fold confirmed values back into `protocol/dataflash-protocol-spec.md`, flipping the
relevant `[?]` tags to `[C-wire]` (or similar), and note the nibbleSwap outcome.
```
