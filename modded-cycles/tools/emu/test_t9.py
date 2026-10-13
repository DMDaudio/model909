#!/usr/bin/env python3
"""Proof of the T9 tweak (tools/gen_t9.py): a 909-style kit in place of the six stock machines, with Model-TG
(34-t9-tg.json on top of 30-model-tg-st.json). The OS's own code, emulated (Unicorn, exact EMAC on the payload):

  1. Boot: the bootstrap's depacker reads the grown OS back; our hook rebuilds the payload, then Model-TG's runs.
  2. Tables: the 12 update/render entries, the MACHINES names, the descriptors' names, defaults and ranges.
  3. Sound: each machine on a track in the real voice loop (Model-TG's dispatch); every block it renders is
     EXACTLY what model909/src/t9.c compiled for this computer renders from the same knobs and trigs (read at the
     call); the glue's knobs are the track's; a SHAPE p-lock changes the sub-voice on its trig; a finished voice
     stops being rendered; the Sampler (machine 7) is unchanged.
  4. Cost: instructions a block for each machine.

    python3 tools/emu/test_t9.py --cycles model-cycles_OS1.13.syx [--part boot|sound|cost]
"""
import argparse
import ctypes
import gc
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

import numpy as np
from unicorn import UC_HOOK_CODE
from unicorn import m68k_const as mk

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))

import build                        # noqa: E402
import emac                         # noqa: E402
import gen_syntakt_engines as gs    # noqa: E402
import gen_sdvintage_7th as g7      # noqa: E402
import gen_t9 as g9                 # noqa: E402
import mcengine as E                # noqa: E402
import test_macro as tm             # noqa: E402
import test_model_tg as TM          # noqa: E402
import test_sdvintage as T          # noqa: E402
import test_sdvintage_7th as t7     # noqa: E402

DEV = HERE.parent.parent / "tweaks" / "model-cycles_OS1.13"
BASE = E.BASE
check = t7.check
OFF = 9
KW = dict(note=60, pitch=64, color=64, shape=0, sweep=64, contour=64, punch=0, gate=0, finetune=64, decay=64)
KINDS = ["BD", "SD", "Hats", "RS/CP", "Toms", "Cymbal"]


def load(name):
    return json.loads((DEV / name).read_text(encoding="utf-8"))


class Fw:
    def __init__(self, stock, tweaks):
        self.stock = stock
        p, _ = build.apply_writes(stock, tweaks)
        pl, _ = build.build_payload(tweaks, stock, None)
        self.img, self.tweaks = bytes(p) + pl, tweaks
        tg = [t for t in tweaks if t["id"].startswith("model-tg")]
        self.blob_end = BASE + len(stock) + tg[0]["append"]["size"]
        ours = [t for t in tweaks if t["id"] == "t9-tg"]
        self.payload = None
        if not ours:
            return
        ap_ = ours[0]["append"]
        self.pay = int(ap_["dest"], 16)
        self.runtime = build.payload_runtime(ours[0], stock, None)
        self.payload = (self.pay, self.runtime)
        self.code_end = self.pay + len(ap_["parts"][0]["hex"]) // 2
        self.sym = {k: int(v, 16) for k, v in ours[0]["symbols"].items()}

    def engine(self, solo=0):
        gc.collect()
        e = TM.engine(self.img, end=self.blob_end, payload=None)
        if self.payload:
            try:
                e.uc.mem_map(self.pay & ~0xfffff, 0x00100000)
            except Exception:
                pass
            e.uc.mem_write(self.pay, self.runtime)
            with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
                tf.write(self.runtime[:self.code_end - self.pay])
            e.emac.install(emac.disasm(tf.name, self.pay, self.pay, self.code_end))
            os.unlink(tf.name)
        for t in range(6):
            if t != solo:
                e.uc.mem_write(E.VOICE0 + t * E.VSTRIDE, struct.pack(">II", OFF, OFF))
        return e


class Host:
    """model909/src/t9.c compiled for this computer."""

    def __init__(self, tmp):
        lib = tmp / "t9.so"
        subprocess.run(["cc", "-O2", "-w", "-shared", "-fPIC", str(g9.SRC / "t9.c"), "-o", str(lib)], check=True)
        self.m = ctypes.CDLL(str(lib))
        self.v = ctypes.create_string_buffer(4096)

    def init(self, kind, seed):
        self.m.t9_init(self.v, ctypes.c_int(kind), ctypes.c_uint32(seed))

    def update(self, kb, trig):
        k = ctypes.create_string_buffer(struct.pack("<i6B2x", *kb), 12)
        self.m.t9_update(self.v, k, ctypes.c_int(trig))

    def render(self):
        buf = (ctypes.c_int32 * 32)()
        self.m.t9_render(self.v, buf)
        return np.clip(np.array(buf[:], dtype=np.int64), -65535, 65535) << 15


def run(fw, host, machine, kw, blocks, trigs, changes=None):
    """One track of machine (0..5) in the voice loop; each block's output against the host replaying the calls."""
    e = fw.engine(solo=0)
    e.set(0, machine=machine, **kw)
    ev, got, cur = [], [], {"out": None}
    uc = e.uc

    def at_init(u, a, s, d):
        sp = u.reg_read(mk.UC_M68K_REG_A7)
        _, kind, seed = struct.unpack(">IiI", bytes(u.mem_read(sp + 4, 12)))
        ev.append(("init", kind, seed))

    def at_update(u, a, s, d):
        sp = u.reg_read(mk.UC_M68K_REG_A7)
        _, kp, trig = struct.unpack(">IIi", bytes(u.mem_read(sp + 4, 12)))
        ev.append(("upd", struct.unpack(">i6B", bytes(u.mem_read(kp, 10))), trig))

    def at_render(u, a, s, d):
        ev.append(("rnd",))
        cur["out"] = struct.unpack(">I", bytes(u.mem_read(u.reg_read(mk.UC_M68K_REG_A7) + 4, 4)))[0]

    def at_after(u, a, s, d):
        if cur["out"] is not None:
            got.append(np.frombuffer(bytes(u.mem_read(cur["out"], 128)), dtype=">i4").astype(np.int64))
            cur["out"] = None

    uc.hook_add(UC_HOOK_CODE, at_init, begin=fw.sym["t9_init"], end=fw.sym["t9_init"])
    uc.hook_add(UC_HOOK_CODE, at_update, begin=fw.sym["t9_update"], end=fw.sym["t9_update"])
    for _, pre, _, _ in g9.MACHINES:
        a = fw.sym[f"t9_{pre}_render"]
        uc.hook_add(UC_HOOK_CODE, at_render, begin=a, end=a)
    ah = int(load("30-model-tg-st.json")["symbols"]["ah_noenv"], 16)      # our render's buffer, before Model-TG's stage
    uc.hook_add(UC_HOOK_CODE, at_after, begin=ah, end=ah)
    outs, exact, rendered, knobs, kinds = [], True, 0, set(), []
    for b in range(blocks):
        for k, v in (changes or {}).get(b, {}).items():
            e.set(0, **{k: v})
        ev.clear()
        got.clear()
        out = e.block(1 if b in trigs else 0)[0]
        outs.append(out)
        did = False
        for x in ev:
            if x[0] == "init":
                host.init(x[1], x[2])
                kinds.append(x[1])
            elif x[0] == "upd":
                host.update(x[1], x[2])
                knobs.add(x[1])
            else:
                exp = host.render()
                did = True
                rendered += 1
                if len(got) != 1 or not np.array_equal(exp, got[0]):
                    exact = False
        if not did and np.any(out):
            exact = False
    return np.stack(outs), exact, rendered, knobs, kinds, e.unmapped


def tables(fw, stock_tg):
    tw = next(t for t in fw.tweaks if t["id"] == "t9-tg")
    u32 = lambda img, va: struct.unpack_from(">I", img, va - BASE)[0]
    syms = fw.sym
    ok = all(u32(fw.img, g7.UPDATE_TAB + 4 * i) == syms[f"t9_{p}_update"] and
             u32(fw.img, g7.RENDER_TAB + 4 * i) == syms[f"t9_{p}_render"] for i, (_, p, _, _) in enumerate(g9.MACHINES))
    check(ok, "the 12 update/render entries of the six stock machines point at the T9 glue")
    rt = fw.runtime
    cstr = lambda va: rt[va - fw.pay:rt.index(b"\0", va - fw.pay)].decode() if fw.pay <= va < fw.pay + len(rt) else None
    data_at = fw.pay + g9.DATA_OFF
    names = [cstr(struct.unpack_from(">I", rt, data_at - fw.pay + 4 * i)[0]) for i in range(6)]
    tg = {k: int(v, 16) for k, v in load("30-model-tg-st.json")["symbols"].items()}
    cave = next(w for w in tw["writes"] if w["off"] == g9.gx.CAVE - BASE)        # the boot stub's piece list
    refs = [va for va in g7.refs32(fw.img[:stock_tg], data_at)
            if not g9.gx.CAVE <= va < g9.gx.CAVE + len(cave["new"]) // 2]
    check(names == [m[0] for m in g9.MACHINES] and len(refs) == 1 and
          struct.unpack_from(">I", rt, data_at - fw.pay + 24)[0] ==
          struct.unpack_from(">I", fw.img, tg["sampler_name_table"] + 24 - BASE)[0],
          f"MACHINES names {names}, the Sampler's entry kept, read through our table ({len(refs)} reference)")
    bad = []
    img_str = lambda va: fw.img[va - BASE:fw.img.index(b"\0", va - BASE)].decode()
    stock = fw.stock
    for m, (_, _, knobs, decay) in enumerate(g9.MACHINES):
        for k, knob in enumerate(knobs):
            i = g9.FIRST_DESC + 5 * m + k
            e = g7.DESC + i * g7.DSTRIDE
            got = (u32(fw.img, e + 8) >> 8, u32(fw.img, e + 12) >> 8, u32(fw.img, e + 16) >> 8)
            long_, short, default, *rng = knob
            lo, hi = rng if rng else (0, 127)
            if i in g9.SHARED:                  # stock name pointers (Model-TG restores them), new text in place
                ok_ = got == (lo, hi, default) and img_str(u32(fw.img, e + 0x2c)) == long_ and \
                    all(u32(fw.img, e + o) == u32(stock, e + o) for o in (0x2c, 0x34))
            else:
                ok_ = got + (cstr(u32(fw.img, e + 0x2c)), cstr(u32(fw.img, e + 0x34))) == (lo, hi, default, long_, short)
            if not ok_:
                bad.append((m, k, got))
        e = g7.DESC + (g9.FIRST_DESC + 5 * m + 4) * g7.DSTRIDE
        if u32(fw.img, e + 16) >> 8 != decay:
            bad.append((m, 4))
    e = g7.DESC + g9.PUNCH_DESC * g7.DSTRIDE
    if (cstr(u32(fw.img, e + 0x2c)), cstr(u32(fw.img, e + 0x34))) != g9.ACCENT:
        bad.append("punch")
    check(not bad, "descriptors 46..75: names, defaults and ranges as gen_t9.MACHINES; Punch shown as Accent / ACC"
          + (f" {bad}" if bad else ""))


def sound(fw, host):
    cases = [(0, {}), (0, dict(pitch=76, contour=110)), (1, dict(color=20)), (1, dict(color=110, shape=100)),
             (2, dict(shape=0, decay=127)), (2, dict(shape=1, decay=127, sweep=100, color=20)),
             (3, dict(shape=0)), (3, dict(shape=1)), (3, dict(shape=1, sweep=0, color=20)), (4, dict(shape=0)),
             (4, dict(shape=2, color=120)), (5, dict(shape=0, decay=127, sweep=0)),
             (5, dict(shape=1, decay=90, color=100)), (0, dict(color=10))]
    for m, kw in cases:
        out, exact, n, knobs, kinds, unm = run(fw, host, m, dict(KW, **kw), 160, (1, 90))
        note = {k[0] for k in knobs}
        want_note = (kw.get("pitch", 64) - 64 + 60) << 16
        check(exact and kinds[-1:] == [m] and note == {want_note} and n > 20 and not unm and np.abs(out).max() > 1 << 26,
              f"{KINDS[m]} {kw or 'defaults'}: {n} blocks rendered = t9.c on the computer, sample for sample "
              f"(note {want_note >> 16}, peak {20 * np.log10(np.abs(out).max() / 2 ** 31):.1f} dBFS)")
    peaks = []
    for punch in (0, 1):
        out, exact, n, knobs, kinds, unm = run(fw, host, 1, dict(KW, color=90, punch=punch), 120, (1,))
        peaks.append(np.abs(out).max())
        check(exact and {k[6] for k in knobs} == {punch}, f"SD PUNCH {punch}: accent {punch} reaches the voice, exact")
    check(1.4 < peaks[1] / peaks[0] < 2.5, f"SD: the accented hit is {20 * np.log10(peaks[1] / peaks[0]):+.1f} dB (the 909: +4.8 dB, from its envelopes)")
    out, exact, n, knobs, kinds, unm = run(fw, host, 0, dict(KW), 180, (1, 90), changes={89: dict(punch=1)})
    check(exact and sorted({k[6] for k in knobs}) == [0, 1], "BD: PUNCH p-locked on the 2nd trig only, still exact")
    out, exact, n, knobs, kinds, unm = run(fw, host, 3, dict(KW, shape=0), 160, (1, 90), changes={89: dict(shape=1)})
    shapes = sorted({k[3] for k in knobs})
    check(exact and shapes == [0, 1], f"RS/CP: SHAPE 0 -> 1 before the 2nd trig: rim then clap, still exact")
    out, exact, n, knobs, kinds, unm = run(fw, host, 0, dict(KW, decay=0), 400, (1,))
    check(exact and not np.any(out[-100:]),
          f"BD DECAY 0: the voice ends and stays silent (still exact; {n} of 400 blocks went through its render)")


def filter_res(fw):
    """Model-TG's Filter/Resonance (track params k = 24, 25) now act on a T9 track, as on its stock machines."""
    def take(cut, res):
        e = fw.engine(solo=0)
        e.set(0, machine=1, **dict(KW, color=110, shape=110))
        e.uc.mem_write(E.PARAMS + 0xe + 46, struct.pack(">hhh", 0, cut, res))
        x = np.concatenate([e.block(1 if b == 1 else 0)[0] for b in range(200)]).astype(float)[32 * 4:]
        sp = np.abs(np.fft.rfft(x[:4096] * np.hanning(4096)))
        f = np.fft.rfftfreq(4096, 1 / 48000)
        return sp[f > 4000].sum() / sp.sum()
    h0, h1, h2 = take(32512, 0), take(9000, 0), take(9000, 32000)
    check(h1 < h0 / 3 and h2 != h1, f"SD through Model-TG's filter: closed, the share above 4 kHz goes from "
                                    f"{100 * h0:.1f} % to {100 * h1:.1f} %; Resonance changes it ({100 * h2:.1f} %)")


def sampler_knobs(ref_fw, fw):
    """The descriptors Model-TG's Sampler page borrows (Start, End, Filter, Resonance, Amp Decay): range and default
    exactly as with Model-TG alone."""
    bad = [i for i in sorted(g9.SHARED) for o in (8, 12, 16)
           if fw.img[g7.DESC + i * g7.DSTRIDE + o - BASE:][:4] != ref_fw.img[g7.DESC + i * g7.DSTRIDE + o - BASE:][:4]]
    check(not bad, f"Sampler page knobs (descriptors {sorted(g9.SHARED)}): ranges and defaults as with Model-TG alone"
          + (f" (differ: {bad})" if bad else ""))


def stems(fw):
    """USB stems after LEVEL: the mixer's call goes through t9_stems, the 6-channel copy reads t9_stems_buf, and each
    stem is the track's block x sqrt(L^2 + R^2) of its mixer gains, ramped over the block (t9_cycles.c), exactly."""
    tw = next(t for t in fw.tweaks if t["id"] == "t9-tg")
    u32 = lambda va: struct.unpack_from(">I", fw.img, va - BASE)[0]
    buf, fn = fw.sym["t9_stems_buf"], fw.sym["t9_stems"]
    lea = fw.img.find(bytes.fromhex("41f9") + struct.pack(">I", buf), g9.TRACKS6[0] - BASE, g9.TRACKS6[0] - BASE + g9.TRACKS6[1])
    check(u32(g9.MIXER_CALL + 2) == fn and lea > 0 and fw.img.find(bytes.fromhex("41f980001858"), g9.TRACKS6[0] - BASE,
          g9.TRACKS6[0] - BASE + g9.TRACKS6[1]) < 0,
          f"the mixer's call (0x40059872) -> t9_stems {fn:#x}; 6ch-usbup's track copy reads t9_stems_buf {buf:#x}")
    e = fw.engine(solo=0)
    uc = e.uc
    gains = {}

    def fake_mixer(u, a, s_, d):                # stands for Model-TG's rs_out and the mixer: set the gains, return
        for t_ in range(6):
            u.mem_write(0x40a78c08 + 4 * t_, struct.pack(">i", gains["l"][t_]))
            u.mem_write(0x40a78be8 + 4 * t_, struct.pack(">i", gains["r"][t_]))
        sp = u.reg_read(mk.UC_M68K_REG_A7)
        u.reg_write(mk.UC_M68K_REG_PC, struct.unpack(">I", u.mem_read(sp, 4))[0])
        u.reg_write(mk.UC_M68K_REG_A7, sp + 4)
    hook = next(w for w in tw["writes"] if w["off"] == g9.MIXER_CALL - BASE)
    target = int(hook["old"][4:], 16)             # Model-TG's rs_out, which t9_stems calls first
    uc.hook_add(UC_HOOK_CODE, fake_mixer, begin=target, end=target)
    rng = np.random.default_rng(9)

    def mul31(a, x):
        return (a * x) >> 31

    def isqrt(x):
        return int(np.floor(np.sqrt(float(x)))) if x < 2 ** 52 else None

    prev, ok, worst = [0] * 6, True, 0.0
    cases = [([-0x5a82799a] * 6, [-0x5a82799a] * 6), ([-0x7fffffff, -0x40000000, 0, -0x20000000, -0x10000000, -0x6000000],
             [0, -0x40000000, 0, -0x30000000, -0x70000000, -0x6000000]), ([0] * 6, [0] * 6)]
    for gl, gr in cases:
        gains["l"], gains["r"] = gl, gr
        blk = rng.integers(-2 ** 30, 2 ** 30, size=(6, 32))
        uc.mem_write(0x80001858, b"".join(struct.pack(">i", int(x)) for x in blk.ravel()))
        e.call(fn, 0, 0)
        got = np.frombuffer(bytes(uc.mem_read(buf, 6 * 32 * 4)), dtype=">i4").reshape(6, 32)
        for t_ in range(6):
            sq = (mul31(gl[t_], gl[t_]) >> 1) + (mul31(gr[t_], gr[t_]) >> 1)
            g1 = isqrt(sq) << 15
            g, dd = prev[t_], (g1 - prev[t_]) >> 5
            want = []
            for i in range(32):
                g += dd
                y = mul31(int(blk[t_, i]), g)
                want.append(max(-0x40000000, min(0x3fffffff, y)) << 1)
            ok &= list(got[t_]) == want
            prev[t_] = g1
            if gl[t_] or gr[t_]:
                worst = max(worst, abs(20 * np.log10(g1 / 2 ** 30 / np.hypot(gl[t_], gr[t_]) * 2 ** 31)))
    check(ok, f"stems = track x gain without pan, ramped, exactly as computed here, 3 blocks of 6 tracks "
              f"(gain within {worst:.2f} dB of sqrt(L^2 + R^2); all gains 0 -> silent)")


def lock_rows(ref_fw, fw):
    """Model-TG issue #3: p-locks of Attack, Filter, Resonance (k = 23, 24, 25) through a pattern's lock rows, the
    OS's own save (0x4005b95c) and load (0x4005b766). With Model-TG alone they come back on other parameters; here
    they come back where they were, with the stock locks untouched."""
    import test_model_tg_syntakt as tms
    TRK = 4385
    locks = {(2, 23): {0: 100, 7: 3000}, (0, 24): {3: 20000}, (5, 25): {15: 123, 63: 32000}, (1, 5): {4: 7777},
             (3, 22): {1: 50}}

    def round_trip(f):
        u = tms.UI(f)
        rt, rows, rt2 = 0x91000000, 0x91010000, 0x91020000
        img = bytearray(b"\xff\xff" * (6 * TRK // 2) + b"\xff")[:6 * TRK]
        for t_ in range(6):
            img[t_ * TRK + 4352:t_ * TRK + 4385] = bytes(33)
        for (t_, k), steps in locks.items():
            img[t_ * TRK + 4352 + k] = 1
            for st, val in steps.items():
                struct.pack_into(">h", img, t_ * TRK + st * 68 + 2 * k, val)
        u.uc.mem_write(rt, bytes(img))
        u.call(0x4005b95c, rows, rt)
        u.call(0x4005b766, rt2, rows)
        got = bytes(u.uc.mem_read(rt2, 6 * TRK))
        back = {}
        for t_ in range(6):
            for k in range(33):
                if got[t_ * TRK + 4352 + k]:
                    back[(t_, k)] = {st: struct.unpack_from(">h", got, t_ * TRK + st * 68 + 2 * k)[0] for st in range(64)
                                     if struct.unpack_from(">h", got, t_ * TRK + st * 68 + 2 * k)[0] >= 0}
        return back, u.bad
    want = {key: dict(v) for key, v in locks.items()}
    old, bad0 = round_trip(ref_fw)
    new, bad1 = round_trip(fw)
    lost = sorted(k for k in want if old.get(k) != want[k])
    check(new == want and not bad1 and lost == [(0, 24), (2, 23), (5, 25)],
          f"p-locks through the pattern's lock rows: Model-TG alone loses {lost} (Attack/Filter/Res; they come back "
          f"as {sorted(k for k in old if k not in want)}), here all {len(want)} come back exactly")


def sampler_unchanged(ref_fw, fw):
    def go(f):
        e = f.engine(solo=0)
        e.set(0, machine=6, **KW)
        return np.stack([e.block(1 if b in (1, 100) else 0)[0] for b in range(200)])
    check(np.array_equal(go(ref_fw), go(fw)), "the Sampler (machine 7): identical to Model-TG alone, 200 blocks")


def cost(fw):
    for m, kw in [(0, {}), (1, dict(color=110)), (2, dict(shape=1, decay=127)), (3, dict(shape=0)), (3, dict(shape=1)),
                  (4, dict(shape=0)), (5, dict(shape=1, decay=127))]:
        e = fw.engine(solo=0)
        e.set(0, machine=m, **dict(KW, **kw))
        cnt = [0]
        e.uc.hook_add(UC_HOOK_CODE, lambda u, a, s, d: cnt.__setitem__(0, cnt[0] + 1))
        per = []
        for b in range(60):
            cnt[0] = 0
            e.block(1 if b == 1 else 0)
            per.append(cnt[0])
        print(f"        {KINDS[m]:7s} {kw}: {np.mean(per[2:]):8,.0f} instructions a block on average, {max(per[2:]):,} "
              f"at most ({100 * np.mean(per[2:]) / 166667:.1f} % / {100 * max(per[2:]) / 166667:.1f} %)")


def boot(fw, tg):
    tweaks = fw.tweaks
    fw.tweaks = [dict(t, id="macro-tg") if t["id"] == "t9-tg" else t for t in tweaks]
    try:
        tm.boot(fw, tg)
    finally:
        fw.tweaks = tweaks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True)
    ap.add_argument("--part", choices=("boot", "sound", "cost"))
    args = ap.parse_args()
    if not args.part:
        bad = 0
        for part in ("boot", "sound", "cost"):
            r = subprocess.run([sys.executable, "-u", __file__, "--cycles", args.cycles, "--part", part])
            bad += r.returncode != 0
        print("\nALL OK" if not bad else f"\n{bad} PART(S) FAILED")
        return 1 if bad else 0
    stock = T.main_os_from_syx(args.cycles)
    gs.set_base(gs.PAY_TG)
    tg_tw, six, t9 = load("30-model-tg-st.json"), load("11-6ch-usbup.json"), load("34-t9-tg.json")
    fw = Fw(stock, [tg_tw, six, t9])
    print(f"== t9-tg, {args.part}")
    with tempfile.TemporaryDirectory() as d:
        if args.part == "boot":
            tg = {k: int(v, 16) for k, v in tg_tw["symbols"].items()}
            t7.X.bootstrap_depack_ok(args.cycles, [tg_tw, six, t9], None) or t7.FAIL.append("bootstrap")
            boot(fw, tg)
            tables(fw, len(stock))
            lock_rows(Fw(stock, [tg_tw, six]), fw)
        elif args.part == "sound":
            sound(fw, Host(pathlib.Path(d)))
            filter_res(fw)
            stems(fw)
            sampler_unchanged(Fw(stock, [tg_tw, six]), fw)
        else:
            cost(fw)
    fail = t7.FAIL
    print(f"  -> {args.part}: " + ("ok" if not fail else f"{len(fail)} failure(s)"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
