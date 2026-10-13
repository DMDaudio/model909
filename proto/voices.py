#!/usr/bin/env python3
"""ctypes front end for the float circuit prototypes in t9f.c: named parameters, circuit defaults, fit bounds.

    render("rs", accent=1.0)        -> np.ndarray (48 kHz)
    VOICES["rs"]["p"]                -> default parameter dict (order = the C p[] layout)
"""
import ctypes
import os
import subprocess

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC, LIB = os.path.join(HERE, "t9f.c"), os.path.join(HERE, "t9f.dylib")
if not os.path.exists(LIB) or os.path.getmtime(LIB) < os.path.getmtime(SRC):
    subprocess.check_call(["cc", "-O2", "-shared", "-fPIC", "-o", LIB + f".{os.getpid()}", SRC])
    os.replace(LIB + f".{os.getpid()}", LIB)            # a rename: a running fit keeps its mapped copy
_lib = ctypes.CDLL(LIB)

# name: (default, lo, hi). Defaults are the circuit values from research/909-circuits.md.
VOICES = {
    "bd": dict(fn="bd_f", n=0.8, p=dict(
        tune=(0.0118, 0.004, 0.035), decay=(0.5, 0, 1), attack=(0.5, 0, 1), accent=(1.0, 0, 1),
        fb=(49.5, 42, 58), df=(230, 120, 400), tp=(5.1, 4.6, 5.6), h2=(0.10, 0.05, 0.15),
        click=(0.2431, 0.2431, 0.2431), nz=(0.0, 0.0, 0.0), c8=(1.0, 0.4, 6), hold=(0.062, 0.02, 0.1), body=(1.0, 0.2, 5),
        thr=(0.6, 0.3, 0.8), vstart=(2.63, 0.9, 2.63))),
    "sd": dict(fn="sd_f", n=0.5, p=dict(
        tune=(0.5, 0, 1), tone=(0.5, 0, 1), snappy=(0.6, 0, 1), accent=(1.0, 0, 1),
        f1=(117.4, 90, 160), bend=(216, 50, 500), bend_t=(0.005, 0.001, 0.02), t1=(0.024, 0.008, 0.08),
        t2=(0.021, 0.008, 0.08), l2=(0.355, 0, 1.5), v4=(10, 2, 14), hold4=(0.024, 0.0, 0.05),
        vend=(-0.5, -4, 0.5), lpf=(7400, 3000, 12000), lpq=(1.6, 0.5, 3), sgain=(1.0, 0.01, 20),
        bgain=(0.3, 0, 5), tp=(6.75, 1.5, 12), r2=(1.59, 1.45, 1.7), thr=(0.6, 0.3, 0.8), dgain=(8.0, 0.5, 40), dsat=(2.0, 0.2, 40))),
    "tom": dict(fn="tom_f", n=0.9, p=dict(
        tune=(0.5, 0, 1), decay=(0.5, 0, 1), accent=(1.0, 0, 1), f1=(57.0, 40, 100),
        vfast=(3.02, 0.5, 6), tfast=(0.022, 0.005, 0.08), vslow=(0.645, 0, 2), tslow=(0.22, 0.05, 0.6),
        r2=(0.667, 0.55, 0.78), r3=(1.83, 1.4, 2.1), tod=(0.032, 0.005, 0.08), od=(10, 1, 20),
        t1=(0.103, 0.03, 0.3), rise2=(0.034, 0.005, 0.08), t2min=(0.038, 0.01, 0.2), t2max=(0.378, 0.1, 0.8),
        t3=(0.026, 0.005, 0.08), l2=(0.4, 0.05, 3), l3=(0.5, 0, 2), nz=(0.3, 0, 3), tick=(0.3, 0, 3),
        tp=(6.75, 1.5, 12), share=(0.3, 0, 2), nbf=(900, 400, 2500), nbq=(0.8, 0.3, 3), l1=(1.0, 0.2, 3))),
    "rs": dict(fn="rs_f", n=0.25, p=dict(
        accent=(1.0, 0, 1), kick=(2.2, 0.3, 8), kick_t=(0.0008, 0.0002, 0.003),
        f1=(494.9, 440, 540), f2=(218.8, 190, 250), f3=(1053, 950, 1150),
        q1=(7.3, 2, 16), q2=(6.1, 2, 16), q3=(7.3, 2, 16), g1=(8.0, 0.5, 40), g2=(8.0, 0.5, 40), g3=(8.0, 0.5, 40),
        r3=(3300, 1000, 15000), vca_t=(0.047, 0.01, 0.15), hpf=(229, 120, 400), hpq=(1.58, 0.5, 3),
        lpf=(7200, 3000, 16000), rail=(13.5, 2, 15), r12=(12000, 4000, 30000))),
    "hc": dict(fn="hc_f", n=0.7, p=dict(
        accent=(1.0, 0, 1), bpf=(963, 750, 1100), bpq=(2.13, 0.8, 4),
        on2=(0.0115, 0.009, 0.014), on3=(0.024, 0.02, 0.028), on4=(0.0305, 0.027, 0.035),
        depth=(3.6, 0.5, 8), ctl_t=(0.008, 0.002, 0.03), bgain=(1.0, 0.05, 10), tgain=(0.25, 0.0, 5),
        rise=(0.018, 0.003, 0.06), tail=(0.09, 0.04, 0.25), tlpf=(440, 200, 900), bhpf=(181, 50, 600),
        last_t=(0.012, 0.002, 0.25), tiltf=(1940, 500, 4000), tiltmix=(0.15, 0, 1))),
}


# the front-panel knobs: free per reference in a joint fit (each Mars hit was made at its own settings)
KNOBS = {"bd": ("tune", "decay", "attack"), "sd": ("tune", "tone", "snappy"), "tom": ("tune", "decay"),
         "rs": (), "hc": ()}


def defaults(v):
    return {k: d for k, (d, lo, hi) in VOICES[v]["p"].items()}


def bounds(v):
    return {k: (lo, hi) for k, (d, lo, hi) in VOICES[v]["p"].items()}


def render(v, n=None, seed=0x909, **over):
    spec = VOICES[v]
    p = {**defaults(v), **over}
    n = int(48000 * (n or spec["n"]))
    arr = (ctypes.c_double * len(p))(*[float(p[k]) for k in spec["p"]])
    out = (ctypes.c_float * n)()
    _lib.noise_seed(ctypes.c_uint32(seed))
    getattr(_lib, spec["fn"])(arr, n, out)
    return np.frombuffer(out, dtype=np.float32).astype(np.float64)
