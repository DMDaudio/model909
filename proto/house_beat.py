#!/usr/bin/env python3
"""A classic house beat (125 BPM, 4 bars), played once with the Mars hits and once with the fitted circuit models
(hats and cymbals are the Mars samples in both, as on the device). python house_beat.py out_prefix"""
import json, sys, wave
import numpy as np
import match, voices

BPM, SR = 125, 48000
STEP = 60 / BPM / 4
FITS = {  # voice: (fit file, index of the ref in a joint fit, Mars file)
    "bd": ("bd_BD_909_Clean_Long_A_01+BD_909_Tape_Medium_F_04+BD_909_Tube_Long_F_06", 0, "BD_909_Clean_Long_A_01"),
    "sd": ("sd_SD_909_Clean_E_01+SD_909_Clean_E_03+SD_909_Clean_E_10", 1, "SD_909_Clean_E_03"),
    "cp": ("hc_Clap_909_Clean", 0, "Clap_909_Clean"),
    "rs": ("rs_Rim_909_Clean", 0, "Rim_909_Clean"),
    "lt": ("tom_Tom_Lo_909_Clean_01", 0, "Tom_Lo_909_Clean_01"),
    "mt": ("tom_Tom_Mid_909_Clean_03", 0, "Tom_Mid_909_Clean_03"),
    "ht": ("tom_Tom_Hi_909_Clean_04", 0, "Tom_Hi_909_Clean_04"),
}
SAMPLES = {"ch": "CH_909_Clean_04", "oh": "OH_909_Clean_01", "cy": "Crash_909_Clean_06"}
GAIN = {"bd": 1.0, "sd": 0.55, "cp": 0.6, "rs": 0.45, "lt": 0.6, "mt": 0.6, "ht": 0.6, "ch": 0.3, "oh": 0.35, "cy": 0.3}
#        1...2...3...4...
BAR = {"bd": "x...x...x...x...", "cp": "....x.......x...", "oh": "..x...x...x...x.", "ch": "x.xxx.xxx.xxx.xx",
       "rs": "...x.....x....x."}
FILL = {"bd": "x...x...x...x...", "cp": "....x.......x...", "oh": "..x...x.........", "ch": "x.xxx.xx........",
        "sd": "..........x.xxxx", "ht": "........x.......", "mt": "..........x.....", "lt": "............x.x."}
BARS = [dict(BAR, cy="x..............."), BAR, BAR, FILL, {"bd": "x...............", "cy": "x..............."}]


def model(v):
    f, i, _ = FITS[v]
    d = json.load(open(f"fits/{f}.json"))
    name = {"cp": "hc", "lt": "tom", "mt": "tom", "ht": "tom"}.get(v, v)
    knobs = voices.KNOBS[name] if "+" in f else ()
    q = {k: x for k, x in d["p"].items() if "@" not in k}
    q.update({k: d["p"][f"{k}@{i}"] for k in knobs})
    x = voices.render(name, n=1.2, **q)
    return x / np.abs(x).max()


def mars(name):
    x = match.load(f"../refs/{name}.wav")
    return x[match.onset(x):] / np.abs(x).max()


def play(hits):
    n = int(SR * (STEP * 16 * len(BARS) + 1.5))
    y = np.zeros(n)
    for b, bar in enumerate(BARS):
        for v, row in bar.items():
            for s, c in enumerate(row):
                if c != ".":
                    h = hits[v] * GAIN[v]
                    t = int(SR * STEP * (16 * b + s))
                    m = min(len(h), n - t)
                    y[t:t + m] += h[:m]
    return y / np.abs(y).max() * 0.9


def write(path, y):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())


if __name__ == "__main__":
    smp = {k: mars(f) for k, f in SAMPLES.items()}
    A = play({**smp, **{v: mars(FITS[v][2]) for v in FITS}})
    B = play({**smp, **{v: model(v) for v in FITS}})
    gap = np.zeros(SR // 2)
    write(sys.argv[1] + "_mars.wav", A); write(sys.argv[1] + "_model.wav", B)
    write(sys.argv[1] + "_mars_then_model.wav", np.concatenate([A, gap, B]))
    print("ok")
