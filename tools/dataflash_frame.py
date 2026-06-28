#!/usr/bin/env python3
"""Dataflash framing — golden reference generator + decoder (8-head broadcast).

Mirrors bridge/src/dataflash_tx.cpp::sendRefresh()/sendHeartbeat() exactly so a
logic-analyzer / scope capture of DF_TX_PIN (or the RS-485 A/B decoded as UART)
has a precise byte- and bit-level target to compare against.

Protocol source of truth: protocol/dataflash-protocol-spec.md
  - 375000 baud, 9 data bits (9th bit = control(1)/data(0) flag)
  - UART frame, LSB first, idle HIGH: start(0) d0..d7 ninth stop(1)  = 11 bits
  - REAL 8-head broadcast (what the OEM controller emits, and now the bridge):
        55 40 <one 8-bit intensity per fixture> 00      (all data, 9th=0)
    interleaved with 0x00 heartbeats (9th=1). NO ARM/START/FIRE on the wire.
  - One byte per fixture = full 8-bit intensity (master-sweep confirmed).

No hardware required. Run directly to print golden vectors and self-test:
    python3 tools/dataflash_frame.py
"""
from __future__ import annotations
from dataclasses import dataclass

# --- protocol constants (keep in sync with bridge/src/dataflash_tx.h) ---------
DF_FRAME0    = 0x55          # alternating-bit preamble (data, 9th=0)
DF_FRAME1    = 0x40          # frame-start (data, 9th=0)
DF_TERM      = 0x00          # trailing frame terminator (data, 9th=0)
DF_HEARTBEAT = 0x00          # master timebase — the ONLY control byte (9th=1)
# Legacy 256-head firmware markers — NOT emitted by the 8-head controller/bridge.
DF_ARM, DF_START, DF_STOP_FIRE, DF_CLEAR = 0x55, 0x7F, 0xF7, 0xFF

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
def encode_refresh(intensities: list[int]) -> list[Word]:
    """Build the word sequence for one full refresh. intensities = 8-bit levels
    (0..255), one per fixture. Frame = 55 40 <data x count> 00, all 9th=0."""
    count = min(len(intensities), 256)
    words = [Word(DF_FRAME0, False), Word(DF_FRAME1, False)]
    for v in intensities[:count]:
        words.append(Word(v & 0xFF, False))         # one fixture per byte, full 8-bit
    words.append(Word(DF_TERM, False))              # trailing terminator
    return words


def encode_heartbeat() -> list[Word]:
    return [Word(DF_HEARTBEAT, True)]               # 0x00, 9th=1 (control)


def word_to_bits(w: Word) -> list[int]:
    """UART bit stream for one word: start, 8 data LSB-first, 9th, stop (idle high)."""
    bits = [0]                                       # start bit
    bits += [(w.value >> i) & 1 for i in range(8)]   # LSB first
    bits.append(1 if w.ninth else 0)                 # 9th bit
    bits.append(1)                                   # stop bit
    return bits


def words_to_bits(words: list[Word]) -> list[int]:
    out: list[int] = []
    for w in words:
        out += word_to_bits(w)
    return out


# --- decode (validate a capture, the inverse of encode) -----------------------
def decode_refresh(words: list[Word]) -> list[int]:
    """Parse a captured word sequence back to per-fixture 8-bit intensities.

    Validates 55 40 framing, the all-data 9th-bit flags, and the trailing 00;
    raises on malformed packets.
    """
    if len(words) < 3 or words[0] != Word(DF_FRAME0, False):
        raise ValueError(f"expected 0x55[D] first, got {words[0] if words else None}")
    if words[1] != Word(DF_FRAME1, False):
        raise ValueError(f"expected 0x40[D] second, got {words[1]}")
    body = words[2:]
    if body and body[-1] == Word(DF_TERM, False):    # strip trailing terminator
        body = body[:-1]
    for w in body:
        if w.ninth:
            raise ValueError(f"data region contains a control word: {w}")
    return [w.value for w in body]


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
def _roundtrip(intensities: list[int]) -> None:
    words = encode_refresh(intensities)
    back = decode_refresh(words)
    expect = [v & 0xFF for v in intensities[:256]]
    assert back == expect, f"word round-trip failed: {back} != {expect}"
    bits = words_to_bits(words)
    assert bits_to_words(bits) == words, "bit round-trip failed"
    assert len(bits) == len(words) * 11, "frame length wrong"


def main() -> None:
    print("=== Dataflash framing — golden reference (8-head broadcast) ===\n")
    print(f"baud (ideal)  : {BAUD}")
    print(f"bit period    : {BIT_NS_IDEAL:.1f} ns ideal  /  {BIT_NS_ACTUAL:.1f} ns actual "
          f"({DF_BIT_TICKS} ticks @ {RMT_TICK_NS} ns)")
    print(f"baud (actual) : {BAUD_ACTUAL:.0f}  (+{(BAUD_ACTUAL/BAUD - 1)*100:.3f}% — within UART tolerance)")
    print(f"frame         : 11 bits = start(0) d0..d7(LSB first) ninth stop(1), idle HIGH")
    print(f"refresh       : 55 40 <1 byte/fixture, 8-bit> 00  (all 9th=0); heartbeat 0x00 9th=1\n")

    cases = [
        ("Heartbeat (0x00, 9th=1)",          None),
        ("8 heads — Fire/all at full (0x80)", [0x80] * 8),
        ("8 heads — single head 0 @ 0x80",    [0x80, 0, 0, 0, 0, 0, 0, 0]),
        ("8 heads — graded ramp",             [0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0]),
    ]
    for name, intens in cases:
        words = encode_heartbeat() if intens is None else encode_refresh(intens)
        print(f"--- {name} ---")
        print(f"  words   : {words}")
        print(f"  TX bytes: {words_hex(words)}   ('+' = 9th/control bit set)")
        print(f"  sigrok  : {sigrok_words(words)}   (9-bit UART; 9th = bit 8)")
        frame_us = len(words) * 11 * BIT_NS_ACTUAL / 1000
        print(f"  on wire : {len(words)} words, {len(words)*11} bit-times, {frame_us:.2f} us\n")

    print("=== self-test (encode<->decode round trip) ===")
    _roundtrip([0x80] * 8)
    _roundtrip([0x80, 0, 0, 0, 0, 0, 0, 0])
    _roundtrip([i for i in range(256)])               # full 256-fixture refresh
    _roundtrip([0])                                   # single fixture
    eight = encode_refresh([0x80] * 8)
    assert len(eight) == 2 + 8 + 1, f"8-head refresh has {len(eight)} words"
    assert words_hex(eight) == "55 40 80 80 80 80 80 80 80 80 00", words_hex(eight)
    print("  OK — 8-head refresh = 55 40 + 8 data + 00 (11 words); round trips pass")
    print(f"  8-head refresh on the wire = {len(eight)*11*BIT_NS_ACTUAL/1000:.2f} us\n")
    print("All checks passed.")


# --- live capture via USB-RS485 adapter -------------------------------------
# A standard USB-serial chip reads only 8 data bits; frame on the 55 40 header +
# inter-burst gaps and verify the byte sequence/payload. Use the scope/sniffer
# for the 9th-bit level. With EVEN parity the data bytes (9th=0) decode clean.
def _decode_chunk(buf: bytes) -> str:
    b = list(buf)
    hexs = " ".join(f"{x:02X}" for x in b)
    if len(b) >= 3 and b[0] == DF_FRAME0 and b[1] == DF_FRAME1:   # 55 40 broadcast
        data = b[2:]
        if data and data[-1] == DF_TERM:
            data = data[:-1]                                      # strip terminator
        return f"REFRESH  {hexs}\n         -> {len(data)} fixtures (8-bit): {data}"
    if len(b) == 1 and b[0] == 0x00:
        return f"HB/term  {hexs}"
    return f"chunk     {hexs}"


def capture(port: str, baud: int, parity: str, save_path=None) -> None:
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
    print("8-head broadcast: refresh = 55 40 <8-bit/fixture> 00; heartbeats = 00. "
          "Use a scope/sniffer for the 9th-bit level.\n")
    try:
        while True:
            chunk = s.read(4096)          # returns on 2ms idle gap
            if chunk:
                if raw: raw.write(chunk); raw.flush()
                print(_decode_chunk(chunk))
    except KeyboardInterrupt:
        s.close()
        if raw: raw.close()
        print("\nstopped.")


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Dataflash framing reference + live capture (8-head broadcast)")
    ap.add_argument("--capture", metavar="PORT", help="USB-RS485 serial port to record")
    ap.add_argument("--baud", type=int, default=375000)
    ap.add_argument("--parity", default="E",
                    help="E (default — clean 9-bit framing) | N (raw) | M|S|O. "
                         "macOS pyserial: use E (M/S unsupported).")
    ap.add_argument("--save", metavar="FILE", help="also write the raw byte stream to FILE")
    a = ap.parse_args()
    if a.capture:
        capture(a.capture, a.baud, a.parity, a.save)
    else:
        main()
