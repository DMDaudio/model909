#!/usr/bin/env python3
"""Fit one prototype voice to Mars references: python fit_voice.py <voice> <ref>[,<ref>...] [rounds] [iters]

With several references the circuit values are shared and the front-panel knobs (voices.KNOBS) are free per
reference ("tune@1"), since each Mars hit was made at its own settings. Best result: fits/<voice>_<refs>.json.
"""
import json
import os
import sys

import match
import voices

v, refs = sys.argv[1], sys.argv[2].split(",")
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 4
iters = int(sys.argv[4]) if len(sys.argv) > 4 else 400
tonal = {"bd": True, "tom": "peak"}.get(v, False)
out = f"fits/{v}_{'+'.join(refs)}.json"
knobs = voices.KNOBS[v] if len(refs) > 1 else ()

B = {k: b for k, b in voices.bounds(v).items() if k not in knobs}
for i in range(len(refs)):
    for k in knobs:
        B[f"{k}@{i}"] = voices.bounds(v)[k]
p0 = voices.defaults(v)
if os.path.exists(out):
    p0.update(json.load(open(out))["p"])
p = {k: p0.get(k, p0.get(k.split("@")[0])) for k in B}


def split(q, i):
    return {**{k: x for k, x in q.items() if "@" not in k and k != "_i"}, **{k: q[f"{k}@{i}"] for k in knobs}}


def render(_i=0, **q):
    return voices.render(v, **split(q, _i))


R = [(match.load(f"../refs/{f}.wav"), {"_i": i}) for i, f in enumerate(refs)]
best = None
for k in range(rounds):
    p, c = match.fit(render, p, B, R, tonal=tonal, iters=iters, seed=k)
    print(f"round {k}: {c:.3f}", flush=True)
    if best is None or c < best[1]:
        best = (p, c)
        json.dump({"p": best[0], "cost": c, "refs": refs}, open(out, "w"), indent=1)
for (r, o), f in zip(R, refs):
    match.report(render(**best[0], **o), r, tonal, label=f"{v} {f}")
