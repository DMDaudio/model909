#!/usr/bin/env python3
"""Waveform-level comparison of a model's hit with a 909 recording, and a small optimizer to fit a model to one.

The old checks compared averages (octave bands over the whole hit, the time to fall below -20 dB, zero-crossing
pitch at a few moments): very different sounds can share those. Here, after aligning the onsets:
  - env:  log envelope, 1 ms resolution, first 400 ms (dB error, weighted to the loud part);
  - spec: multi-resolution log spectrogram (64/256/1024-point windows), so both the transient's spectrum and the
          body's are compared in time;
  - pitch: a pitch track for tonal voices (autocorrelation, 5 ms hop), compared in cents.
distance() returns their weighted sum; report() prints the parts. fit() is a Nelder-Mead over named parameters.
"""
import sys
import wave

import numpy as np

SR = 48000


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    dt = {2: "<i2", 4: "<i4"}[sw]
    x = np.frombuffer(raw, dtype=dt).reshape(-1, ch)[:, 0].astype(np.float64) / (2 ** (8 * sw - 1))
    assert sr == SR, sr
    return x


def onset(x, thr_db=-30):
    a = np.abs(x)
    i = int(np.argmax(a > a.max() * 10 ** (thr_db / 20)))
    return max(0, i - 24)


def align(x, ref):
    """Both from their onsets, then x shifted (+-5 ms) to best match ref's first 100 ms of envelope (so a
    weaker click does not move the alignment); peak-normalized, cut to the shorter (at most 1 s)."""
    x, ref = x[onset(x):], ref[onset(ref):]
    er = env_db(ref[:int(0.1 * SR)] / (np.abs(ref).max() + 1e-12), 0.5)
    best, bs = None, 0
    for s in range(-240, 241, 12):
        xs = x[s:] if s >= 0 else np.concatenate([np.zeros(-s), x])
        ex = env_db(xs[:int(0.1 * SR)] / (np.abs(xs).max() + 1e-12), 0.5)
        n = min(len(ex), len(er))
        w = np.clip((er[:n] - er[:n].max() + 40) / 40, 0, 1)
        c = float(np.sum(w * np.abs(ex[:n] - er[:n])))
        if best is None or c < best:
            best, bs = c, s
    x = x[bs:] if bs >= 0 else np.concatenate([np.zeros(-bs), x])
    n = min(len(x), len(ref), int(SR * 1.0))
    x, ref = x[:n], ref[:n]
    return x / (np.abs(x).max() + 1e-12), ref / (np.abs(ref).max() + 1e-12)


def env_db(x, ms=1.0):
    w = int(SR * ms / 1000)
    n = len(x) // w
    e = np.sqrt(np.mean(x[:n * w].reshape(n, w) ** 2, axis=1))
    return 20 * np.log10(e + 1e-6)


def spectro(x, n):
    hop = max(n // 4, 24)
    win = np.hanning(n)
    frames = [x[i:i + n] * win for i in range(0, len(x) - n, hop)]
    if not frames:
        return np.zeros((1, n // 2 + 1))
    m = np.abs(np.fft.rfft(np.array(frames), axis=1))
    return 20 * np.log10(m + 1e-5)


def pitch_track(x, hop_ms=5, win=2048, lo=35, hi=900, thr=0.15):
    """YIN-style: cumulative-mean-normalized difference, first dip under thr (with parabolic refinement)."""
    out = []
    tmax = SR // lo
    for i in range(0, len(x) - win - tmax, int(SR * hop_ms / 1000)):
        s = x[i:i + win + tmax]
        if np.sqrt(np.mean(s[:win] ** 2)) < 1e-3:
            out.append(np.nan)
            continue
        e = np.cumsum(np.concatenate([[0.0], s ** 2]))
        r = np.fft.irfft(np.fft.rfft(s, 2 * len(s)) * np.conj(np.fft.rfft(s[:win], 2 * len(s))))[:tmax]
        d = e[win] + (e[win:win + tmax] - e[:tmax]) - 2 * r          # sum (x_j - x_{j+tau})^2
        d[0] = 0
        cm = d[1:] * np.arange(1, tmax) / (np.cumsum(d[1:]) + 1e-12)
        cand = np.nonzero(cm[SR // hi:] < thr)[0]
        if not len(cand):
            out.append(np.nan)
            continue
        t = SR // hi + cand[0]
        while t + 1 < len(cm) and cm[t + 1] < cm[t]:
            t += 1
        if 0 < t < len(cm) - 1:
            a, b, c = cm[t - 1], cm[t], cm[t + 1]
            t = t + 0.5 * (a - c) / (a - 2 * b + c + 1e-12)
        out.append(SR / (t + 1))
    return np.array(out)


def peak_track(x, hop_ms=5, win=4096, lo=40, hi=400):
    """Strongest spectral peak in lo..hi per frame (for the toms: several detuned VCOs defeat YIN)."""
    out, nf = [], 16384
    f = np.fft.rfftfreq(nf, 1 / SR)
    band = (f >= lo) & (f <= hi)
    idx = np.nonzero(band)[0]
    w = np.hanning(win)
    for i in range(0, len(x) - win, int(SR * hop_ms / 1000)):
        s = x[i:i + win]
        if np.sqrt(np.mean(s ** 2)) < 1e-3:
            out.append(np.nan)
            continue
        m = np.abs(np.fft.rfft(s * w, nf))
        k = idx[np.argmax(m[idx])]
        a, b, c = np.log(m[k - 1] + 1e-12), np.log(m[k] + 1e-12), np.log(m[k + 1] + 1e-12)
        out.append(f[k] + 0.5 * (a - c) / (a - 2 * b + c + 1e-12) * (f[1] - f[0]))
    return np.array(out)


_late = {}


def late_peak(ref):
    key = (len(ref), float(ref[1000]))
    if key not in _late:
        s = ref[int(0.12 * SR):int(0.4 * SR)]
        m = np.abs(np.fft.rfft(s * np.hanning(len(s)), 65536))
        f = np.fft.rfftfreq(65536, 1 / SR)
        sel = (f > 30) & (f < 700)
        _late[key] = float(f[sel][np.argmax(m[sel])])
    return _late[key]


_bins = np.geomspace(30, 3000, 24 * 7 + 1)               # 1/24-octave bins, 30 Hz .. 3 kHz


def partial_db(x, a, b):
    s = x[int(a * SR):int(b * SR)]
    if len(s) < 256:
        return None
    m = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 16)) ** 2
    f = np.fft.rfftfreq(1 << 16, 1 / SR)
    idx = np.searchsorted(f, _bins)
    e = np.array([m[i:j].max() if j > i else 0 for i, j in zip(idx[:-1], idx[1:])])
    e = 10 * np.log10(e + 1e-20)
    return e - e.max()


def partials(x, ref):
    """Where the partials are and how loud, per time window: 1/24-octave peak spectra, dB error over the ref's
    top 30 dB (the tuning term: the spectrogram's windows are too short to resolve low partials)."""
    d, n = 0.0, 0
    for a, b in ((0.0, 0.03), (0.03, 0.1), (0.1, 0.35)):
        er, ex = partial_db(ref, a, b), partial_db(x, a, b)
        if er is None or ex is None or len(ref) < b * SR:
            continue
        w = np.clip((er + 30) / 30, 0, 1)
        d += float(np.sum(w * np.abs(ex - er)) / (np.sum(w) + 1e-9))
        n += 1
    return d / max(n, 1)


_tb = 30 * 2 ** (np.arange(0, 29) / 3)                     # 1/3-octave edges, 30 Hz .. 15 kHz
_tw = ((0, .005), (.005, .015), (.015, .04), (.04, .1), (.1, .2), (.2, .4))


def band_levels(x):
    out = []
    f = np.fft.rfftfreq(1 << 15, 1 / SR)
    idx = np.searchsorted(f, _tb)
    for a, b in _tw:
        s = x[int(a * SR):int(b * SR)]
        if len(s) < 64:
            out.append(np.full(len(_tb) - 1, -120.0))
            continue
        m = np.abs(np.fft.rfft(s * np.hanning(len(s)), 1 << 15)) ** 2 / len(s)
        out.append(10 * np.log10(np.array([m[i:j].sum() for i, j in zip(idx[:-1], idx[1:])]) + 1e-12))
    return np.array(out)


def bands(x, ref):
    """Balance: 1/3-octave levels in six windows over the first 400 ms, on the common peak normalization (so how
    loud the noise is against the drum, and the click against the body, count), over the ref's top 45 dB."""
    bx, br = band_levels(x), band_levels(ref)
    w = np.clip((br - br.max() + 45) / 15, 0, 1)
    return float(np.sum(w * np.abs(bx - br)) / (np.sum(w) + 1e-9))


def parts(x, ref, tonal=False):
    x, ref = align(x, ref)
    ex, er = env_db(x), env_db(ref)
    w = np.clip((er - er.max() + 75) / 50, 0, 1)            # weight the loud part, and tails down to -75 dB
    env = float(np.sum(w * np.abs(ex - er)) / (np.sum(w) + 1e-9))
    spec = 0.0
    for n in (64, 256, 1024, 4096):
        sx, sr_ = spectro(x, n), spectro(ref, n)
        m = min(len(sx), len(sr_))
        sx, sr_ = sx[:m], sr_[:m]
        wt = np.clip((sr_ - sr_.max() + 60) / 60, 0, 1)
        spec += float(np.sum(wt * np.abs(sx - sr_)) / (np.sum(wt) + 1e-9)) / 4
    p = 0.0
    if tonal:
        if tonal == "peak":                                  # follow VCO1: the ref's strongest late partial
            fl = late_peak(ref)
            px, pr = peak_track(x, lo=0.82 * fl, hi=1.9 * fl), peak_track(ref, lo=0.82 * fl, hi=1.9 * fl)
        else:
            px, pr = pitch_track(x), pitch_track(ref)
        m = min(len(px), len(pr))
        ok = ~np.isnan(px[:m]) & ~np.isnan(pr[:m])
        p = float(np.mean(np.abs(1200 * np.log2(px[:m][ok] / pr[:m][ok])))) / 100 if ok.any() else 10.0
    return dict(env=env, spec=spec, pitch=p, part=partials(x, ref), bands=bands(x, ref))


def distance(x, ref, tonal=False):
    d = parts(x, ref, tonal)
    return d["env"] + d["spec"] + 3 * d["pitch"] + 0.5 * d["part"] + d["bands"]


def report(x, ref, tonal=False, label=""):
    d = parts(x, ref, tonal)
    print(f"{label:24s} env {d['env']:5.2f} dB   spec {d['spec']:5.2f} dB" + (f"   pitch {100 * d['pitch']:5.1f} cents" if tonal else "") + f"   partials {d['part']:5.2f} dB   bands {d['bands']:5.2f} dB")
    return d


def fit(render, params, bounds, refs, tonal=False, iters=300, seed=0):
    """Nelder-Mead in a box. render(**p) -> signal; refs: list of (ref signal, fixed overrides for that ref)."""
    names = list(params)
    lo = np.array([bounds[k][0] for k in names], dtype=float)
    hi = np.array([bounds[k][1] for k in names], dtype=float)
    to_p = lambda u: {k: float(v) for k, v in zip(names, lo + np.clip(u, 0, 1) * (hi - lo))}

    def cost(u):
        p = to_p(u)
        return sum(distance(render(**{**p, **over}), r, tonal) for r, over in refs) / len(refs)

    rng = np.random.default_rng(seed)
    x0 = np.where(hi > lo, (np.array([params[k] for k in names], dtype=float) - lo) / np.where(hi > lo, hi - lo, 1), 0)
    simplex = [x0] + [np.clip(x0 + 0.15 * np.eye(len(names))[i] * (1 if rng.random() < .5 else -1), 0, 1)
                      for i in range(len(names))]
    vals = [cost(s) for s in simplex]
    for it in range(iters):
        order = np.argsort(vals)
        simplex = [simplex[i] for i in order]
        vals = [vals[i] for i in order]
        c = np.mean(simplex[:-1], axis=0)
        xr = c + (c - simplex[-1]); fr = cost(xr)
        if fr < vals[0]:
            xe = c + 2 * (c - simplex[-1]); fe = cost(xe)
            simplex[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            simplex[-1], vals[-1] = xr, fr
        else:
            xc = c + 0.5 * (simplex[-1] - c); fc = cost(xc)
            if fc < vals[-1]:
                simplex[-1], vals[-1] = xc, fc
            else:
                simplex = [simplex[0]] + [simplex[0] + 0.5 * (s - simplex[0]) for s in simplex[1:]]
                vals = [vals[0]] + [cost(s) for s in simplex[1:]]
        if it % 50 == 0:
            print(f"  iter {it}: {min(vals):.3f}", flush=True)
    best = int(np.argmin(vals))
    return to_p(simplex[best]), vals[best]


if __name__ == "__main__":
    a, b = load(sys.argv[1]), load(sys.argv[2])
    report(a, b, tonal="--tonal" in sys.argv, label="a vs b")
