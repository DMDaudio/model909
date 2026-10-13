#!/usr/bin/env python3
"""Generates the T9 tweak of model909 (github.com/DMDaudio/model909): a TR-909-style kit that REPLACES the six stock
machines of the Model:Cycles OS 1.13, on top of Model-TG (model-tg-st) and the 6-channel USB mod (6ch-usbup).

  - 34-t9-tg.json: KICK -> BD, SNARE -> SD, METAL -> Hats (CH/OH samples), PERC -> RS/CP, TONE -> Toms,
    CHORD -> Cymbal (crash/ride samples). The Sampler (Model-TG's 7th machine) is unchanged.

Replacing needs no new machine: the 12 entries of the stock update/render tables (0x40118628 / 0x40118610, which
Model-TG's dispatch still reads) point at the T9 glue, the MACHINES screen gets Model-TG's name table copied with
six new names, and the six machines' parameter descriptors (46..75) get new names, defaults and ranges in place.
The code (model909/src: t9.c, t9_cycles.c) is a payload at gs.PAY_TG, copied there at boot (the slot the MACRO
machine and the Syntakt engines use, so it conflicts with them).

The hats and cymbals are the user's own WAVs (model909/tools/mksamples.py writes src/t9_samples.h): the generated
JSON contains them, so it is for the user's own build only; do not publish it.

    MODEL909=/path/to/model909 python3 tools/gen_t9.py --cycles model-cycles_OS1.13.syx
"""
import argparse
import json
import os
import pathlib
import struct
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "emu"))

import gen_sdvintage_exact as gx   # noqa: E402
import gen_sdvintage_7th as g7     # noqa: E402
import gen_syntakt_engines as gs   # noqa: E402
import sprites                     # noqa: E402

DEV = HERE.parent / "tweaks" / "model-cycles_OS1.13"
OUT_TG = DEV / "34-t9-tg.json"
# model909's sources (github.com/DMDaudio/model909): $MODEL909, else a model909 folder next to this Modded-Cycles
# checkout
SRC = pathlib.Path(os.environ.get("MODEL909", HERE.parent.parent / "model909")) / "src"
BASE = gx.BASE
# The payload's own layout (it owns gs.PAY_TG's 1 MB): code, tables and samples up to
# DATA_OFF, then our data (names, lock maps), then the variables (the six voices)
DATA_OFF = 0x3c000
DATA_MAX = 0x1000
BSS_OFF = 0x3e000
FIRST_DESC = 46                    # COLOR, SHAPE, SWEEP, CONTOUR, Amp Decay of machine 0, then 5 a machine
MIXER_CALL = 0x40059872             # jsr 0x400567ba (the mixer), after the voice loop, before the USB send
TRACKS6 = (0x4015c1c0, 96)           # 6ch-usbup's copy of the 6 tracks to the USB ring (tools/relocate_6ch.py)
# p-locks of Model-TG's Attack, Filter and Resonance (parameters k = 23, 24, 25; Model-TG issue #3): the pattern's
# lock rows carry a stored parameter number from the 23-entry map 0x4010ee4c, read past its end for k > 22 (into the
# reverse map: 0, 1, 2), so their locks came back on other parameters. Extended copies of both maps give them 33, 34,
# 35 (a stock OS skips rows above 32, so it never misreads them), and the loader and the reverse lookup accept them.
LOCK_FWD = 0x4010ee4c                # k -> stored, 23 entries
LOCK_INV = 0x4010eea8                # stored -> k, 24 entries
LOCK_OURS = {23: 33, 24: 34, 25: 35}
LOCK_WRITES = [                      # (VA, stock bytes, new bytes: a function of (fwd, inv))
    (0x4005b996, "4bf94010ee4c", lambda f, i: "4bf9" + f"{f:08x}"),   # save, all locks: lea map,%a5
    (0x4005bac8, "43f94010ee4c", lambda f, i: "43f9" + f"{f:08x}"),   # save, one parameter: lea map,%a1
    (0x4005aa46, "41f94010eea8", lambda f, i: "41f9" + f"{i:08x}"),   # reverse lookup: lea map,%a0
    (0x4005aa34, "7417", lambda f, i: "7423"),                        # its bound: stored <= 23 -> <= 35
    (0x4005b816, "7a20", lambda f, i: "7a23"),                        # the loader's: stored <= 32 -> <= 35
]
PUNCH_DESC = 44                    # Punch (all machines): the 909's accent here
ACCENT = ("Accent", "ACC")

# per stock machine: (MACHINES name, glue prefix, knobs COLOR/SHAPE/SWEEP/CONTOUR, Amp Decay default or None).
# A knob is (long, short, default[, min, max]), or ("=", long) for the descriptors Model-TG's Sampler page borrows
# (SHARED): their range and default stay stock, and Model-TG puts their stock name pointers back each time another
# page is built, so the new long name is written over the stock string itself (it must fit) and the short name stays.
SHARED = {46, 62, 73, 74, 75}          # Kick Color, Perc Shape, Chord Sweep, Chord Contour (its Start, End, Filter,
                                       # Resonance) and Chord's Amp Decay (its Amp Decay)
MACHINES = [
    ("BD", "bd", [("BD Attack", "ATCK", 64), ("BD Shape", "SHPE", 64), ("BD Sweep", "SWP", 64),
                  ("BD Sweep Time", "STIM", 64)], 64),
    ("SD", "sd", [("SD Snappy", "SNAP", 64), ("SD Tone", "TONE", 64), ("SD Pitch Drop", "DROP", 64),
                  ("SD Body", "BODY", 64)], 64),
    ("Hats", "hh", [("HH Tone", "TONE", 64), ("HH Type", "TYPE", 0, 0, 1), ("HH Bits", "BITS", 0),
                    ("HH None", "----", 0)], 127),
    ("RS/CP", "rc", [("RC Tone", "TONE", 64), ("RC Type", "TYPE", 0, 0, 1), ("RC Spread", "SPRD", 64),
                     ("RC Burst", "BRST", 64)], 64),
    ("Toms", "tom", [("Tom Noise", "NOIS", 64), ("Tom Type", "TYPE", 0, 0, 2), ("Tom Sweep", "SWP", 64),
                     ("Tom Sweep Time", "STIM", 64)], 64),
    ("Cymbal", "cy", [("Cym Tone", "TONE", 64), ("Cym Type", "TYPE", 0, 0, 1), ("Cym Bits", "BITS", 0),
                      ("Cym None", "----", 0)], 127),
]


CFLAGS = ["-mcpu=54418", "-O2", "-U_FORTIFY_SOURCE", "-ffreestanding", "-fno-builtin", "-fno-tree-loop-distribute-patterns",
          "-nostdlib", "-fno-pic", "-fno-common", "-ffunction-sections", "-fdata-sections", "-fomit-frame-pointer",
          "-Wall", "-Werror", "-Wno-unused-function", "-Wno-unused-const-variable"]


def assemble(tmp, name, src, at, defs=()):
    """Assemble src (a .S file) at address at: (bytes, symbols)."""
    o, e, b = (tmp / (name + x) for x in (".o", ".elf", ".bin"))
    gx.run([gx.CROSS + "gcc", "-mcpu=54418", *defs, "-c", str(src), "-o", str(o)])
    gx.run([gx.CROSS + "ld", "-Ttext", f"{at:#x}", "-o", str(e), str(o)])
    gx.run([gx.CROSS + "objcopy", "-O", "binary", "-j", ".text", str(e), str(b)])
    syms = {p_[-1]: int(p_[0], 16) for p_ in (l.split() for l in gx.run([gx.CROSS + "nm", str(e)]).splitlines())
            if len(p_) == 3}
    return b.read_bytes(), syms


def other_ids():
    """Tweaks that add machines or use the same payload slot after the image: never with T9."""
    ids = {"macro", "macro-tg", "sdvintage-snare", "sdvintage-exact", "sdvintage-7th", "syntakt-vintage", "syntakt-meter",
           "syntakt-tg-meter", "syntakt-tg-profile"}
    for codes in gs.subsets():
        ids |= {gs.subset_id(codes), gs.tweak_id(codes, tg=True)}
    return ids

LINK = """SECTIONS
{{
  .text {code:#x} : {{ *(.text*) }}
  .rodata : {{ *(.rodata*) }}
  .data : {{ *(.data*) }}
  .bss {bss:#x} (NOLOAD) : {{ *(.bss*) *(COMMON) }}
  /DISCARD/ : {{ *(.comment) *(.note*) *(.eh_frame*) }}
}}
"""


def compile_t9(tmp, pay, tg, mixer):
    """t9.c + t9_cycles.c linked at pay (code, tables, samples) and pay + BSS_OFF (the six voices)."""
    if not (SRC / "t9_samples.h").exists():
        raise SystemExit(f"!! {SRC / 't9_samples.h'} missing: run model909's tools/mksamples.py with your own WAVs")
    objs = []
    for src in (SRC / "t9_cycles.c", SRC / "t9.c"):
        o = tmp / (src.stem + ".o")
        gx.run([gx.CROSS + "gcc", *CFLAGS, f"-DT9_AH_NOENV={tg['ah_noenv']:#x}", f"-DT9_STEMS={mixer:#x}",
                f"-I{SRC}", "-c", str(src), "-o", str(o)])
        objs.append(o)
    ld, elf, out = tmp / "link.ld", tmp / "t9.elf", tmp / "t9.bin"
    ld.write_text(LINK.format(code=pay, bss=pay + BSS_OFF))
    keep = [a for _, p, _, _ in MACHINES for a in ("-u", f"t9_{p}_update", "-u", f"t9_{p}_render")] + ["-u", "t9_stems"]
    gx.run([gx.CROSS + "ld", "-T", str(ld), "--gc-sections", "--no-warn-rwx-segments", "-e", "t9_bd_update", *keep,
            "-o", str(elf), *map(str, objs)])
    secs = {}
    for line in gx.run([gx.CROSS + "objdump", "-h", str(elf)]).splitlines():
        f = line.split()
        if len(f) > 4 and f[0].isdigit():
            secs[f[1]] = (int(f[3], 16), int(f[2], 16))
    extra = set(secs) - {".text", ".rodata", ".data", ".bss"}
    if extra:
        raise SystemExit(f"!! unexpected sections: {sorted(extra)}")
    gx.run([gx.CROSS + "objcopy", "-O", "binary", "-R", ".bss", str(elf), str(out)])
    blob = out.read_bytes()
    syms = {}
    for line in gx.run([gx.CROSS + "nm", str(elf)]).splitlines():
        p_ = line.split()
        if len(p_) == 3:
            syms[p_[2]] = int(p_[0], 16)
        elif len(p_) == 2 and p_[0] == "U":
            raise SystemExit(f"!! undefined symbol {p_[1]} (a libgcc call?)")
    if pay + len(blob) > pay + DATA_OFF:
        raise SystemExit(f"!! T9 code and samples too big: {len(blob)} B (room {DATA_OFF} B)")
    bss = secs.get(".bss", (pay + BSS_OFF, 0))
    if bss[0] != pay + BSS_OFF or bss[0] + bss[1] > pay + 0x100000:
        raise SystemExit("!! BSS")
    return blob, syms, (bss[0] + bss[1] + 3) & ~3, secs


def build_tweak(img, tg):
    pay = gs.PAY_TG
    gs.set_base(pay)
    import build                                  # tools/build.py: the image after Model-TG and 6ch-usbup
    six = json.loads((DEV / "11-6ch-usbup.json").read_text(encoding="utf-8"))
    cur, _ = build.apply_writes(tg["img"], [six])
    u32 = lambda va: struct.unpack_from(">I", cur, va - BASE)[0]
    blob_u32 = lambda va: struct.unpack_from(">I", tg["blob"], va - tg["blob_at"])[0]
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        mixer = u32(MIXER_CALL + 2)               # what the call runs now: Model-TG's wrapper of the mixer
        if cur[MIXER_CALL - BASE:MIXER_CALL - BASE + 2] != b"\x4e\xb9":
            raise SystemExit("!! the mixer's call")
        blob, syms, bss_end, secs = compile_t9(tmp, pay, tg, mixer)

        # --- data: the MACHINES names (Model-TG's table of 7, ours for 0..5) and the knob names
        names_at = pay + DATA_OFF
        strings = [m[0] for m in MACHINES] + [s_ for m in MACHINES for k in m[2] if k[0] != "=" for s_ in k[:2]] \
            + list(ACCENT)
        at, addr = names_at + 4 * 7, {}
        for s_ in strings:
            if s_ not in addr:
                addr[s_] = at
                at += len(s_) + 1
        names = [addr[m[0]] for m in MACHINES] + [blob_u32(tg["sampler_name_table"] + 4 * 6)]
        data = b"".join(g7.be32(x) for x in names) + b"".join(s_.encode("ascii") + b"\0" for s_ in addr)
        data += bytes(-len(data) % 4)
        fwd_at = names_at + len(data)              # the extended maps of the lock rows (LOCK_*)
        fwd = [u32(LOCK_FWD + 4 * k) for k in range(23)] + [LOCK_OURS.get(k, 0xff) for k in range(23, 33)]
        data += b"".join(g7.be32(x) for x in fwd)
        inv_at = names_at + len(data)
        inv = [u32(LOCK_INV + 4 * i) for i in range(24)] + [0] * 9 + [k for k, _ in sorted(LOCK_OURS.items(), key=lambda kv: kv[1])]
        data += b"".join(g7.be32(x) for x in inv)
        if len(data) > DATA_MAX:
            raise SystemExit("!! data")

        writes = []

        def w(va, old, new_):
            if cur[va - BASE:va - BASE + len(old)] != old:
                raise SystemExit(f"!! {va:#x}: expected {old.hex()}, found {cur[va - BASE:va - BASE + len(old)].hex()}")
            if old != new_:
                writes.append({"off": va - BASE, "old": old.hex(), "new": new_.hex()})

        writes.append(None)                     # boot hook: written below (it carries the payload's pieces)
        red = sprites.redirect_write(gx.CAVE)
        w(BASE + red["off"], bytes.fromhex(red["old"]), bytes.fromhex(red["new"]))
        w(gs.BOOT_CALL + 2, g7.be32(tg["boot_extra_hook"]), g7.be32(gx.CAVE))   # ours, then Model-TG's (CHAIN_TO)

        # --- the 12 entries
        for i, (_, pre, _, _) in enumerate(MACHINES):
            w(g7.UPDATE_TAB + 4 * i, g7.be32(u32(g7.UPDATE_TAB + 4 * i)), g7.be32(syms[f"t9_{pre}_update"]))
            w(g7.RENDER_TAB + 4 * i, g7.be32(u32(g7.RENDER_TAB + 4 * i)), g7.be32(syms[f"t9_{pre}_render"]))

        # --- USB stems after LEVEL (6ch-usbup, t9_cycles.c): the mixer's call goes through t9_stems, and the mod's
        # copy of the 6 tracks (its tracks6 stub, in the 0x4015c044 cave) reads t9_stems_buf instead of 0x80001858
        w(MIXER_CALL, bytes.fromhex("4eb9") + g7.be32(mixer), bytes.fromhex("4eb9") + g7.be32(syms["t9_stems"]))
        lea = bytes.fromhex("41f980001858")
        k = cur.find(lea, TRACKS6[0] - BASE, TRACKS6[0] - BASE + TRACKS6[1])
        if k < 0 or cur.find(lea, k + 1, TRACKS6[0] - BASE + TRACKS6[1]) >= 0:
            raise SystemExit("!! 6ch-usbup's tracks6: one 'lea 0x80001858,%a0' expected")
        w(BASE + k, lea, bytes.fromhex("41f9") + g7.be32(syms["t9_stems_buf"]))

        # --- p-locks of Attack / Filter / Resonance (LOCK_*)
        for va, old, new_ in LOCK_WRITES:
            w(va, bytes.fromhex(old), bytes.fromhex(new_(fwd_at, inv_at)))

        # --- the names: references to Model-TG's name table -> our copy
        rs = g7.refs32(cur, tg["sampler_name_table"])
        if len(rs) != 1:
            raise SystemExit(f"!! references to Model-TG's name table: {len(rs)}")
        for va in rs:
            w(va, g7.be32(tg["sampler_name_table"]), g7.be32(names_at))

        # --- descriptors 46..75, in place: names, defaults, ranges
        for m, (_, _, knobs, decay) in enumerate(MACHINES):
            for k, knob in enumerate(knobs):
                i = FIRST_DESC + 5 * m + k
                e = g7.DESC + i * g7.DSTRIDE
                if u32(e) != m:
                    raise SystemExit(f"!! descriptor {i}: machine {u32(e):#x}, expected {m}")
                long_, short, default, *rng = knob
                lo, hi = rng if rng else (0, 127)
                if i in SHARED:                       # Model-TG's Sampler page borrows it and puts back its stock name
                    va = u32(e + 0x2c)                # pointers: the new long name goes over the stock string
                    old = cur[va - BASE:cur.index(b"\0", va - BASE) + 1]
                    if len(long_) + 1 > len(old) or len(g7.refs32(cur, va)) != 1:
                        raise SystemExit(f"!! descriptor {i}: {long_!r} does not fit in {old!r}, or the string is shared")
                    w(va, old, long_.encode("ascii") + bytes(len(old) - len(long_)))
                    w(e + 0x08, g7.be32(u32(e + 0x08)), g7.be32(lo << 8))
                    w(e + 0x0c, g7.be32(u32(e + 0x0c)), g7.be32(hi << 8))
                    w(e + 0x10, g7.be32(u32(e + 0x10)), g7.be32(default << 8))
                    continue
                w(e + 0x08, g7.be32(u32(e + 0x08)), g7.be32(lo << 8))
                w(e + 0x0c, g7.be32(u32(e + 0x0c)), g7.be32(hi << 8))
                w(e + 0x10, g7.be32(u32(e + 0x10)), g7.be32(default << 8))
                w(e + 0x2c, g7.be32(u32(e + 0x2c)), g7.be32(addr[long_]))
                w(e + 0x34, g7.be32(u32(e + 0x34)), g7.be32(addr[short]))
            e = g7.DESC + (FIRST_DESC + 5 * m + 4) * g7.DSTRIDE           # its Amp Decay: the default only
            w(e + 0x10, g7.be32(u32(e + 0x10)), g7.be32(decay << 8))
        e = g7.DESC + PUNCH_DESC * g7.DSTRIDE                             # Punch -> Accent (its names only)
        w(e + 0x2c, g7.be32(u32(e + 0x2c)), g7.be32(addr[ACCENT[0]]))
        w(e + 0x34, g7.be32(u32(e + 0x34)), g7.be32(addr[ACCENT[1]]))

        # --- payload
        parts = [{"dest": f"{pay:#x}", "hex": blob.hex()}, {"dest": f"{names_at:#x}", "hex": bytes(data).hex()}]
        size = bss_end - pay
        segs = gs.segments(parts, size)
        at = tg["at"]
        if at + sum(n_ for _, n_ in segs) > gs.END_LIMIT:
            raise SystemExit(f"!! adding {sum(n_ for _, n_ in segs)} B: the image would pass {gs.END_LIMIT:#x}")
        (tmp / "segs.inc").write_text("".join(f"\t.long\t{a:#x}, {n_ // 4}\n" for a, n_ in segs))
        stub, _ = assemble(tmp, "stub", gx.SRC / "stub.S", gx.CAVE, [
            f"-DPAYLOAD_SRC={at:#x}", f"-DPAYLOAD_DST={pay:#x}", f"-DPAYLOAD_LONGS={size // 4}", "-DPACK", f"-I{tmp}",
            f"-DCHAIN_TO={tg['boot_extra_hook']:#x}"])
    if len(stub) > sprites.zone(gx.CAVE)[1]:
        raise SystemExit("!! boot hook too big for its cave")
    if cur[gx.CAVE - BASE:gx.CAVE - BASE + len(stub)] != b"\xff" * len(stub):
        raise SystemExit("!! the boot hook's cave is taken")
    writes[0] = {"off": gx.CAVE - BASE, "old": "ff" * len(stub), "new": stub.hex()}
    writes.sort(key=lambda x: x["off"])
    for a_, b_ in zip(writes, writes[1:]):
        if a_["off"] + len(a_["new"]) // 2 > b_["off"]:
            raise SystemExit(f"!! overlapping writes at {BASE + b_['off']:#x}")

    tid = "t9-tg"
    ids = other_ids() | {"model-tg"}
    out = {
        "id": tid,
        "order": 34,
        "name": "Model-TG + T9 (model909): a TR-909-style kit in place of the six stock machines (experiment)",
        "description": [
            "On top of model-tg-st. KICK -> BD, SNARE -> SD, METAL -> Hats (CH/OH samples, SHAPE 0/1), PERC -> RS/CP",
            "(SHAPE 0 rim, 1 clap), TONE -> Toms (SHAPE 0/1/2 = LT/MT/HT), CHORD -> Cymbal (SHAPE 0 crash, 1 ride).",
            "BD, SD, toms, rim and clap model the 909's circuits, fitted to recordings of a real 909. The Sampler page",
            "borrows Perc Shape, so its End knob is 0..1 with this tweak; its Start/Filter/Res/Decay defaults follow ours.",
            "Their output goes through Model-TG's per-track stage (Attack, Filter/Resonance, resampling, mute fades).",
            "The voices make their own envelopes: DECAY is theirs; PUNCH (shown as Accent) is the 909's accent: it raises",
            "the envelopes' start like the 909's (the sampled hats/cymbals: +4.5 dB). GATE does nothing. Projects using",
            "the stock machines sound different. Needs 6ch-usbup: its USB tracks now carry LEVEL, VOL and mute (not pan).",
            "Also fixes Model-TG issue #3: p-locks of its Attack, Filter and Resonance survive a save (lock rows 33-35).",
            "Code: github.com/DMDaudio/model909 (src/). Generated by tools/gen_t9.py.",
            "Contains the builder's own hat and cymbal samples: for their own build only, do not publish this file.",
        ],
        "device": "Model:Cycles",
        "os": "1.13",
        "section": 3,
        "requires": [tg["id"], "6ch-usbup"],
        "conflicts": sorted(ids - {tid}),
        "writes": writes,
        "append": {"at": f"{at:#x}", "dest": f"{pay:#x}", "size": size, "parts": parts, "reloc": [],
                   "pack": [[f"{a_:#x}", n_] for a_, n_ in segs]},
        "symbols": {k: f"{v:#x}" for k, v in syms.items() if k.startswith("t9_") and k[3:].split("_")[0] in
                    {m[1] for m in MACHINES}} | {k: f"{syms[k]:#x}" for k in ("voices", "t9_init", "t9_update", "t9_render", "t9_stems", "t9_stems_buf") if k in syms},
    }
    info = dict(code=secs[".text"][1], rodata=secs.get(".rodata", (0, 0))[1], bss=size - BSS_OFF,
                image=sum(n_ for _, n_ in segs), end=at + sum(n_ for _, n_ in segs))
    return out, info


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cycles", required=True, help="the official model-cycles_OS1.13.syx")
    args = ap.parse_args()
    img = g7.cycles_main(args.cycles)
    if len(img) != gx.IMAGE_LEN:
        raise SystemExit("!! not the official MAIN OS 1.13")
    tweak, info = build_tweak(img, gs.tg_context(img))
    OUT_TG.write_text(json.dumps(tweak, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"  {tweak['id']}: {len(tweak['writes'])} writes, code {info['code']} B, tables+samples {info['rodata']} B, "
          f"voices {info['bss']} B, added to the image {info['image']} B (ends {info['end']:#x}, limit "
          f"{gs.END_LIMIT:#x})")
    print(f"    wrote {OUT_TG}")


if __name__ == "__main__":
    main()
