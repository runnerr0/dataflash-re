#!/usr/bin/env python3
"""Dataflash framing — golden reference generator + decoder.

Mirrors bridge/src/dataflash_tx.cpp::sendRefresh()/sendHeartbeat() exactly so a
logic-analyzer / scope capture of DF_TX_PIN (or the RS-485 A/B decoded as UART)
has a precise byte- and bit-level target to compare against.

Protocol source of truth: protocol/dataflash-protocol-spec.md
  - 375000 baud, 9 data bits (9th bit = control(1)/data(0) flag)
  - UART frame, LSB first, idle HIGH: start(0) d0..d7 ninth stop(1)  = 11 bits
  - Packet: ARM(0x55) START(0x7F) <data x N> STOP/FIRE(0xF7), all markers 9th=1
  - Data byte = two fixtures, 4-bit each. Default: even addr -> high nibble.

No hardware required. Run directly to print golden vectors and self-test:
    python3 tools/dataflash_frame.py
"""
from __future__ import annotations
from dataclasses import dataclass

# --- protocol constants (keep in sync with bridge/src/dataflash_tx.h) ---------
DF_ARM       = 0x55
DF_START     = 0x7F
DF_HEARTBEAT = 0x00
DF_STOP_FIRE = 0xF7
DF_CLEAR     = 0xFF

BAUD          = 375000
RMT_TICK_NS   = 12.5          # clk_div=1 on 80 MHz APB -> 12.5 ns/tick
DF_BIT_TICKS  = 213           # dataflash_tx.cpp: 213 ticks/bit
BIT_NS_IDEAL  = 1e9 / BAUD    # 2666.67 ns
BIT_NS_ACTUAL = DF_BIT_TICKS * RMT_TICK_NS   # 2662.5 ns
BAUD_ACTUAL   = 1e9 / BIT_NS_ACTUAL          # 375586.85 -> +0.156%


@dataclass(frozen=True)
class Word:
    """One 9-bit UART word: the 8-bit value plus the 9th control/data flag."""
    value: int      # 0..255
    ninth: bool     # True = control marker, False = data

    def __repr__(self) -> str:
        kind = "C" if self.ninth else "D"
        return f"{self.value:#04x}[{kind}]"


# --- encode (mirror of DataflashTx) -------------------------------------------
def encode_refresh(intensities: list[int], nibble_swap: bool = False) -> list[Word]:
    """Build the word sequence for one full refresh. intensities = 4-bit levels."""
    count = len(intensities)
    if count == 0:
        return []
    if count > 256:
        count = 256
        intensities = intensities[:256]

    words = [Word(DF_ARM, True), Word(DF_START, True)]

    data_bytes = (count + 1) // 2
    for k in range(data_bytes):
        even_fix = intensities[2 * k] & 0x0F
        odd_fix  = (intensities[2 * k + 1] & 0x0F) if (2 * k + 1 < count) else 0
        # default: even address -> high nibble, odd -> low nibble
        if nibble_swap:
            b = ((odd_fix << 4) | even_fix)
        else:
            b = ((even_fix << 4) | odd_fix)
        words.append(Word(b, False))

    words.append(Word(DF_STOP_FIRE, True))
    return words


def encode_heartbeat() -> list[Word]:
    return [Word(DF_HEARTBEAT, True)]


def word_to_bits(w: Word) -> list[int]:
    """UART bit stream for one word: start, 8 data LSB-first, 9th, stop (idle high)."""
    bits = [0]                                   # start bit
    bits += [(w.value >> i) & 1 for i in range(8)]  # LSB first
    bits.append(1 if w.ninth else 0)             # 9th bit
    bits.append(1)                               # stop bit
    return bits


def words_to_bits(words: list[Word]) -> list[int]:
    out: list[int] = []
    for w in words:
        out += word_to_bits(w)
    return out


# --- decode (validate a capture, the inverse of encode) -----------------------
def decode_refresh(words: list[Word], nibble_swap: bool = False) -> list[int]:
    """Parse a captured word sequence back to per-fixture intensities.

    Validates marker framing and 9th-bit flags; raises on malformed packets.
    """
    if not words or words[0] != Word(DF_ARM, True):
        raise ValueError(f"expected ARM 0x55[C] first, got {words[0] if words else None}")
    if len(words) < 2 or words[1] != Word(DF_START, True):
        raise ValueError(f"expected START 0x7F[C] second, got {words[1] if len(words) > 1 else None}")
    if words[-1] != Word(DF_STOP_FIRE, True):
        raise ValueError(f"expected STOP 0xF7[C] last, got {words[-1]}")

    intensities: list[int] = []
    for w in words[2:-1]:
        if w.ninth:
            raise ValueError(f"data region contains a control word: {w}")
        hi, lo = (w.value >> 4) & 0x0F, w.value & 0x0F
        even_fix, odd_fix = (lo, hi) if nibble_swap else (hi, lo)
        intensities += [even_fix, odd_fix]
    return intensities


def bits_to_words(bits: list[int]) -> list[Word]:
    """Decode a raw UART bit stream (as a logic analyzer would) back to words.

    Assumes bits are already sampled one-per-bit-period, idle HIGH. Finds each
    start bit (falling edge to 0), reads 11 bits, checks the stop bit.
    """
    words: list[Word] = []
    i, n = 0, len(bits)
    while i < n:
        if bits[i] != 0:           # idle high; advance to next start bit
            i += 1
            continue
        if i + 11 > n:
            break
        frame = bits[i:i + 11]
        if frame[10] != 1:
            raise ValueError(f"bad stop bit at bit index {i}: {frame}")
        value = sum(frame[1 + b] << b for b in range(8))
        ninth = bool(frame[9])
        words.append(Word(value, ninth))
        i += 11
    return words


# --- helpers ------------------------------------------------------------------
def words_hex(words: list[Word]) -> str:
    return " ".join(f"{w.value:02X}{'+' if w.ninth else ' '}".strip() for w in words)


def sigrok_words(words: list[Word]) -> str:
    """How a 9-bit-aware UART decoder reports each word (9th bit = bit 8)."""
    return " ".join(f"{(w.value | (0x100 if w.ninth else 0)):#05x}" for w in words)


# --- self-test + golden vectors -----------------------------------------------
def _roundtrip(intensities: list[int], nibble_swap: bool = False) -> None:
    words = encode_refresh(intensities, nibble_swap)
    # word-level round trip
    back = decode_refresh(words, nibble_swap)
    expect = intensities + ([0] if len(intensities) % 2 else [])  # odd count pads one fixture
    assert back == expect, f"word round-trip failed: {back} != {expect}"
    # bit-level round trip (encode -> bits -> words -> decode)
    bits = words_to_bits(words)
    assert bits_to_words(bits) == words, "bit round-trip failed"
    assert len(bits) == len(words) * 11, "frame length wrong"


def main() -> None:
    print("=== Dataflash framing — golden reference ===\n")
    print(f"baud (ideal)  : {BAUD}")
    print(f"bit period    : {BIT_NS_IDEAL:.1f} ns ideal  /  {BIT_NS_ACTUAL:.1f} ns actual "
          f"({DF_BIT_TICKS} ticks @ {RMT_TICK_NS} ns)")
    print(f"baud (actual) : {BAUD_ACTUAL:.0f}  (+{(BAUD_ACTUAL/BAUD - 1)*100:.3f}% — within UART tolerance)")
    print(f"frame         : 11 bits = start(0) d0..d7(LSB first) ninth stop(1), idle HIGH")
    print(f"frame time    : {11 * BIT_NS_ACTUAL/1000:.2f} us\n")

    cases = [
        ("Heartbeat (0x00, 9th=1)",        None),
        ("Single fixture @ addr0 = 15",    [15]),
        ("2 fixtures: addr0=15, addr1=0",  [15, 0]),
        ("4 fixtures: 15,0,8,4",           [15, 0, 8, 4]),
        ("All-on, 6 fixtures @ 15",        [15] * 6),
    ]
    for name, intens in cases:
        if intens is None:
            words = encode_heartbeat()
        else:
            words = encode_refresh(intens)
        print(f"--- {name} ---")
        print(f"  words   : {words}")
        print(f"  TX bytes: {words_hex(words)}   ('+' = 9th/control bit set)")
        print(f"  sigrok  : {sigrok_words(words)}   (9-bit UART; 9th = bit 8)")
        frame_us = len(words) * 11 * BIT_NS_ACTUAL / 1000
        print(f"  on wire : {len(words)} words, {len(words)*11} bit-times, {frame_us:.2f} us\n")

    # exhaustive-ish self test
    print("=== self-test (encode<->decode round trip) ===")
    _roundtrip([15])
    _roundtrip([15, 0])
    _roundtrip([15, 0, 8, 4])
    _roundtrip([i & 0xF for i in range(256)])          # full 256-fixture refresh
    _roundtrip([i & 0xF for i in range(7)])            # odd count -> pad
    _roundtrip([3, 12], nibble_swap=True)              # nibble swap path
    # full refresh sizing matches spec (128 data bytes for 256 fixtures)
    full = encode_refresh([15] * 256)
    assert len(full) == 2 + 128 + 1, f"256-fixture refresh has {len(full)} words"
    print("  OK — all round trips pass; 256-fixture refresh = ARM+START+128data+STOP")
    print(f"  full 256-fixture refresh = {len(full)} words, "
          f"{len(full)*11*BIT_NS_ACTUAL/1000:.2f} us on the wire\n")
    print("All checks passed.")


# --- live capture via USB-RS485 adapter -------------------------------------
# A standard USB-serial chip reads only 8 data bits, so the 9th (control/data)
# bit is NOT recovered here — we frame on marker VALUES + inter-burst gaps and
# verify the byte sequence/payload. Use the scope for the 9th-bit level.
MARKERS = {0x55: "ARM", 0x7F: "START", 0xF7: "STOP/FIRE", 0xFF: "CLEAR", 0x00: "HB/0"}


def _decode_chunk(buf: bytes, nibble_swap: bool) -> str:
    b = list(buf)
    hexs = " ".join(f"{x:02X}" for x in b)
    # recognise a refresh: ARM, START, <data...>, STOP
    if len(b) >= 3 and b[0] == 0x55 and b[1] == 0x7F and b[-1] == 0xF7:
        fixtures = []
        for x in b[2:-1]:
            hi, lo = (x >> 4) & 0xF, x & 0xF
            fixtures += ([lo, hi] if nibble_swap else [hi, lo])
        return f"REFRESH  {hexs}\n         -> {len(fixtures)} fixtures: {fixtures}"
    if len(b) == 1 and b[0] in MARKERS:
        return f"{MARKERS[b[0]]:9} {hexs}"
    return f"chunk     {hexs}"


def capture(port: str, baud: int, parity: str, nibble_swap: bool, save_path=None) -> None:
    try:
        import serial
    except ImportError:
        print("pyserial not installed: pip install pyserial"); return
    pmap = {"N": serial.PARITY_NONE, "M": serial.PARITY_MARK,
            "S": serial.PARITY_SPACE, "E": serial.PARITY_EVEN, "O": serial.PARITY_ODD}
    try:
        s = serial.Serial(port, baud, bytesize=8, parity=pmap[parity.upper()],
                          stopbits=1, timeout=0.2, inter_byte_timeout=0.002)
    except ValueError as e:
        print(f"{e}  (macOS pyserial: MARK/SPACE unsupported — use --parity E)"); return
    raw = open(save_path, "wb") if save_path else None
    print(f"capturing {port} @ {baud} 8{parity.upper()}1"
          f"{' -> ' + save_path if save_path else ''}. Ctrl-C to stop.")
    print("9-bit protocol framed via 8-data+parity (the parity slot absorbs the 9th bit); "
          "byte values decode clean. Use a scope for the 9th-bit level.\n")
    try:
        while True:
            chunk = s.read(4096)          # returns on 2ms idle gap
            if chunk:
                if raw: raw.write(chunk); raw.flush()
                print(_decode_chunk(chunk, nibble_swap))
    except KeyboardInterrupt:
        s.close()
        if raw: raw.close()
        print("\nstopped.")


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Dataflash framing reference + live capture")
    ap.add_argument("--capture", metavar="PORT", help="USB-RS485 serial port to record")
    ap.add_argument("--baud", type=int, default=375000)
    ap.add_argument("--parity", default="E",
                    help="E (default — clean 9-bit framing) | N (raw, data scrambles) | M|S|O. "
                         "macOS pyserial: use E (M/S unsupported).")
    ap.add_argument("--save", metavar="FILE", help="also write the raw byte stream to FILE")
    ap.add_argument("--nibble-swap", action="store_true")
    a = ap.parse_args()
    if a.capture:
        capture(a.capture, a.baud, a.parity, a.nibble_swap, a.save)
    else:
        main()
