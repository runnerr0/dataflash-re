#!/usr/bin/env python3
"""Interactive sampler for the Dataflash 9-bit sniffer.

Reads the `esp32-s3-sniff` firmware's decoded output over the ESP32-S3 USB-CDC
port. That firmware prints every recovered byte tagged `<hex>C` (control, 9th=1)
or `<hex>d` (data, 9th=0). This tool lets you walk every program on the original
controller hands-free:

    1. Set a program on the controller.
    2. Type a label (or just Enter to auto-number) and press Enter -> it records
       a few seconds, saves the raw stream, and prints a decoded summary.
    3. Repeat for the next program. Type 'q' to quit.

Each frame the controller sends is `0x55 0x40` + 8 data bytes (16 fixtures, 2 per
byte, 4-bit each); the summary pulls those frames out so you can see the animation.

Usage:
    python3 tools/sniff_sampler.py                       # auto-detect port, 4s captures
    python3 tools/sniff_sampler.py --port /dev/cu.usbmodemXXXX --secs 5
"""
import argparse, glob, os, re, sys, time
from collections import Counter

TOKEN = re.compile(r'([0-9A-Fa-f]{2})([Cd])')


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/tty.usbmodem*"))
    return ports[0] if ports else None


def record(ser, secs):
    ser.reset_input_buffer()                 # drop the backlog; capture fresh
    end = time.time() + secs
    buf = b""
    while time.time() < end:
        buf += ser.read(16384)
    return buf.decode("utf-8", "replace")


def summarize(txt):
    toks = [(int(v, 16), t) for v, t in TOKEN.findall(txt)]
    ctl = Counter(v for v, t in toks if t == 'C')
    dat = Counter(v for v, t in toks if t == 'd')
    vals = [v for v, _ in toks]
    # frames: 0x55 0x40 followed by 8 data/payload bytes
    frames = [tuple(vals[i + 2:i + 10]) for i in range(len(vals) - 9)
              if vals[i] == 0x55 and vals[i + 1] == 0x40]
    return toks, ctl, dat, Counter(frames)


def main():
    ap = argparse.ArgumentParser(description="Interactive Dataflash 9-bit sniffer sampler")
    ap.add_argument("--port", default=None, help="S3 sniffer USB-CDC port (auto-detect if omitted)")
    ap.add_argument("--secs", type=float, default=4.0, help="seconds to record per program")
    ap.add_argument("--outdir", default="captures/sniff", help="where to save raw captures")
    a = ap.parse_args()

    try:
        import serial
    except ImportError:
        sys.exit("pyserial not installed:  pip install pyserial")

    port = a.port or find_port()
    if not port:
        sys.exit("No /dev/cu.usbmodem* found — is the ESP32-S3 sniffer plugged in?")
    os.makedirs(a.outdir, exist_ok=True)
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
    except Exception as e:
        sys.exit(f"could not open {port}: {e}")

    print(f"=== Dataflash program sampler — sniffer on {port} ({a.secs:.0f}s/capture) ===")
    print("For each program: set it on the controller, type a label + Enter to record.")
    print("Empty label = auto-number.   q = quit.\n")

    saved, n = [], 0
    try:
        while True:
            label = input("program label (Enter=auto, q=quit) > ").strip()
            if label.lower() == "q":
                break
            n += 1
            if not label:
                label = f"{n:02d}"
            safe = re.sub(r'[^A-Za-z0-9_.-]', '_', label)

            print(f"  recording {a.secs:.0f}s for '{label}' ...", flush=True)
            txt = record(ser, a.secs)
            path = os.path.join(a.outdir, f"prog-{safe}.txt")
            with open(path, "w") as f:
                f.write(txt)

            toks, ctl, dat, frames = summarize(txt)
            print(f"  -> {path}   ({len(toks)} bytes, {sum(frames.values())} '55 40' frames)")
            print("     control:", " ".join(f"{k:02X}:{v}" for k, v in ctl.most_common(6)) or "none")
            print("     data:   ", " ".join(f"{k:02X}:{v}" for k, v in dat.most_common(8)) or "none")
            if frames:
                print("     frames (8 bytes after 55 40):")
                for fr, c in frames.most_common(4):
                    print("       " + " ".join(f"{b:02X}" for b in fr) + f"   x{c}")
            else:
                print("     (no '55 40' frames seen — is the controller running / wired in?)")
            print()
            saved.append((label, path))
    except (KeyboardInterrupt, EOFError):
        print()
    finally:
        ser.close()

    if saved:
        print(f"done — {len(saved)} program(s) saved to {a.outdir}/:")
        for label, path in saved:
            print(f"  {label:>10}  {path}")
    else:
        print("nothing recorded.")


if __name__ == "__main__":
    main()
