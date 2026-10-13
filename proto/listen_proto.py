#!/usr/bin/env python3
"""Mars hit, then the fitted prototype, for each fit given: python listen_proto.py out.wav fits/a.json ..."""
import json, sys, wave
import numpy as np
import match, voices
gap = np.zeros(int(0.35 * 48000))
seq = []
for f in sys.argv[2:]:
    d = json.load(open(f)); v = f.split("/")[-1].split("_")[0]
    knobs = voices.KNOBS[v] if len(d.get("refs", [0])) > 1 else ()
    for i, ref in enumerate(d.get("refs") or ["_".join(f.split("/")[-1][:-5].split("_")[1:])]):
        q = {k: x for k, x in d["p"].items() if "@" not in k}
        q.update({k: d["p"][f"{k}@{i}"] for k in knobs})
        r = match.load(f"../refs/{ref}.wav"); x = voices.render(v, n=len(r) / 48000 + 0.05, **q)
        r, x = r[match.onset(r):], x[match.onset(x):]
        x = x / np.abs(x).max() * np.abs(r).max()
        seq += [r, gap, x, gap, gap]
y = np.concatenate(seq)
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes((np.clip(y, -1, 1) * 32767).astype("<i2").tobytes())
print(sys.argv[1], f"{len(y)/48000:.1f} s")
