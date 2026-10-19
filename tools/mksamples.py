#!/usr/bin/env python3
"""Builds the sample bank from your own four 909 hits (closed hat, open hat, crash, ride): mono, 32 kHz, trimmed,
IMA ADPCM (4 bits a sample). Any WAV/AIFF ffmpeg reads will do. The web flasher does the same in the browser.

Bank (big-endian, at most 180224 bytes): 'T9SB', the rate, then per sample (CH, OH, crash, ride) its offset from the
bank's start and its length in samples, then the ADPCM data.

The bank (and the .syx built from it) carries your samples: keep it to yourself if the samples' licence does not allow
sharing them.

    python3 tools/mksamples.py --ch CH.wav --oh OH.wav --crash Crash.wav --ride Ride.wav [--rate 32000] [-o bank.bin]
"""
import argparse
import pathlib
import struct
import subprocess

import gen_tables

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "out" / "t9_bank.bin"
BANK_SIZE = 0x2c000                                       # T9_BANK_SIZE (src/t9.h)
MAGIC = b"T9SB"
NAMES = ("ch", "oh", "crash", "ride")                      # the C names, in the order t9.c uses them


def load(path, rate):
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(path), "-ac", "1", "-ar", str(rate), "-f", "s16le",
                          "-"], check=True, capture_output=True).stdout
    return [int.from_bytes(raw[i:i + 2], "little", signed=True) for i in range(0, len(raw), 2)]


def trim(x, rate):
    peak = max(abs(v) for v in x) or 1
    x = [round(v * 30000 / peak) for v in x]                 # same peak for all
    thr = 30000 * 10 ** (-66 / 20)
    end = max(i for i, v in enumerate(x) if abs(v) > thr) + 1
    start = next(i for i, v in enumerate(x) if abs(v) > 30000 * 10 ** (-40 / 20))
    start = max(0, start - rate // 4000)                     # keep 0.25 ms before the onset
    x = x[start:end]
    fade = min(len(x), rate // 100)                          # 10 ms fade out
    for i in range(fade):
        x[len(x) - fade + i] = round(x[len(x) - fade + i] * (fade - i) / fade)
    return x


def ima_encode(x):
    """Standard IMA ADPCM, first sample's prediction from 0. Low nibble first."""
    pred, idx, out = 0, 0, []
    for v in x:
        step = gen_tables.IMA_STEP[idx]
        d = v - pred
        code = 8 if d < 0 else 0
        d = abs(d)
        q, diff = 0, step >> 3
        if d >= step:
            q |= 4; d -= step; diff += step
        if d >= step >> 1:
            q |= 2; d -= step >> 1; diff += step >> 1
        if d >= step >> 2:
            q |= 1; diff += step >> 2
        pred = pred - diff if code else pred + diff
        pred = max(-32768, min(32767, pred))
        code |= q
        idx = max(0, min(88, idx + gen_tables.IMA_IDX[code]))
        out.append(code)
    if len(out) & 1:
        out.append(0)
    return bytes(out[i] | out[i + 1] << 4 for i in range(0, len(out), 2))


def bank(hits, rate):
    """hits: four lists of 16-bit samples (CH, OH, crash, ride), trimmed -> the bank's bytes."""
    head, data, at = MAGIC + struct.pack(">I", rate), b"", 8 + 8 * len(hits)
    for x in hits:
        enc = ima_encode(x)
        head += struct.pack(">II", at + len(data), len(x))
        data += enc
    out = head + data
    if len(out) > BANK_SIZE:
        raise SystemExit(f"!! samples too long: {len(out)} bytes, the bank holds {BANK_SIZE}; trim them or lower --rate")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for n in NAMES:
        ap.add_argument(f"--{n}", required=True, type=pathlib.Path, help=f"the {n} sample (WAV, AIFF, ...)")
    ap.add_argument("--rate", type=int, default=32000)
    ap.add_argument("-o", "--out", type=pathlib.Path, default=OUT)
    args = ap.parse_args()
    hits = []
    for name in NAMES:
        x = trim(load(getattr(args, name), args.rate), args.rate)
        print(f"  {name}: {len(x)} samples ({len(x) / args.rate * 1000:.0f} ms), {(len(x) + 1) // 2} bytes")
        hits.append(x)
    b = bank(hits, args.rate)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(b)
    print(f"wrote {args.out}: {len(b)} of {BANK_SIZE} bytes")


if __name__ == "__main__":
    main()
