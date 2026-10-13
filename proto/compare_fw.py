#!/usr/bin/env python3
"""Firmware (host build of src/t9.c) vs the float prototype at the same settings, per voice."""
import math, subprocess, sys, tempfile, os
import numpy as np
import match, voices, gen_cal
TMP = tempfile.mkdtemp()


def fw(kind, decay, color, shape, sweep, contour, note=60, ms=900, acc=1):
    out = os.path.join(TMP, f"{kind}.wav")
    subprocess.check_call(["../host/render", kind, str(decay), str(color), str(shape), str(sweep), str(contour),
                           str(note), str(ms), out, str(acc)], stdout=subprocess.DEVNULL)
    return match.load(out)


def cases():
    v, p = gen_cal.fit("bd")
    c = round(127 * math.log10(p["tune"] / 0.004)); d = round(p["decay"] * 127); a = round(16 + p["attack"] * 111)
    yield "bd", fw("bd", d, a, 64, 64, c), voices.render("bd", seed=0x12345678, **{**p, "tune": 0.004 * 10 ** (c / 127), "decay": d / 127, "attack": (a - 16) / 111}), True
    v, p = gen_cal.fit("sd")
    d, s = round(p["tone"] * 127), round(p["snappy"] * 127)
    yield "sd", fw("sd", d, s, 64, 64, 64, ms=500), voices.render("sd", seed=0x12345678, **{**p, "tone": d / 127, "snappy": s / 127}), False
    for i, n in enumerate(("lt", "mt", "ht")):
        v, p = gen_cal.fit(n)
        d = round(p["decay"] * 127)
        yield n, fw("tom", d, 64, i, 64, 64), voices.render("tom", seed=0x12345678, **{**p, "decay": d / 127}), "peak"
    v, p = gen_cal.fit("rs")
    yield "rs", fw("rc", 64, 64, 0, 64, 64, ms=250), voices.render("rs", seed=0x12345678, **p), False
    v, p = gen_cal.fit("hc")
    yield "hc", fw("rc", 64, 64, 1, 64, 64, ms=900), voices.render("hc", seed=0x12345678, **p), False


for name, a, b, tonal in cases():
    pa, pb = np.abs(a).max(), np.abs(b).max()
    match.report(a, b, tonal, label=f"{name} fw vs proto (fw peak {20*np.log10(pa+1e-9):.1f} dBFS)")
