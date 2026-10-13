#!/usr/bin/env python3
"""Each voice: Mars, model, Mars, model (peak-matched), for every reference fitted. python side_by_side.py out.wav"""
import json, sys, wave
import numpy as np
import match, voices
SR = 48000
ITEMS = [  # (fit file, voice)
    ("bd_BD_909_Clean_Long_A_01+BD_909_Tape_Medium_F_04+BD_909_Tube_Long_F_06", "bd"),
    ("sd_SD_909_Clean_E_01+SD_909_Clean_E_03+SD_909_Clean_E_10", "sd"),
    ("tom_Tom_Lo_909_Clean_01", "tom"), ("tom_Tom_Mid_909_Clean_03", "tom"), ("tom_Tom_Hi_909_Clean_04", "tom"),
    ("rs_Rim_909_Clean", "rs"), ("hc_Clap_909_Clean", "hc"),
]
gap, pause = np.zeros(int(0.25 * SR)), np.zeros(int(0.8 * SR))
seq, labels, t = [], [], 0.0
for f, v in ITEMS:
    d = json.load(open(f"fits/{f}.json"))
    refs = d.get("refs") or ["_".join(f.split("_")[1:])]
    knobs = voices.KNOBS[v] if len(refs) > 1 else ()
    for i, ref in enumerate(refs):
        q = {**voices.defaults(v), **{k: x for k, x in d["p"].items() if "@" not in k}}
        q.update({k: d["p"][f"{k}@{i}"] for k in knobs})
        r = match.load(f"../refs/{ref}.wav"); r = r[match.onset(r):]
        n = min(len(r), int(1.2 * SR))
        x = voices.render(v, n=n / SR + 0.02, **q); x = x[match.onset(x):][:n]
        r = r[:n] / np.abs(r[:n]).max() * 0.8; x = x / np.abs(x).max() * 0.8
        for k, s in enumerate((r, x, r, x)):
            labels.append(f"{t:6.1f} s  {ref:28s} {'Mars' if k % 2 == 0 else 'model'}")
            seq += [s, gap]; t += (len(s) + len(gap)) / SR
        seq.append(pause); t += len(pause) / SR
y = np.concatenate(seq)
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
print("\n".join(l for l in labels if "Mars" in l and labels.index(l) % 4 == 0))
