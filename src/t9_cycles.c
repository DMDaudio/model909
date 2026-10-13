/*
 * T9 on the Model:Cycles (OS 1.13): the six stock machines' update/render entries point here. KICK BD, SNARE SD,
 * METAL hats, PERC RS/CP, TONE toms, CHORD cymbals.
 *
 * Voice-loop contract (Modded-Cycles notes/14): update(pmod, v, p) then render(out, v), once a
 * block of 32 frames at 48 kHz. pmod = the trig's note (semitones << 16); v = the Cycles voice (0x31c bytes);
 * p = the track's parameters (8.8 words: +0x14 PITCH, +0x16 COLOR, +0x18 SHAPE, +0x1a SWEEP, +0x1c CONTOUR,
 * +0x1e PUNCH, +0x22 FINE, +0x24 DECAY); v+0x38: a trig this block; out: 32 x int32 (Q31).
 *
 * The 909 voices make their own envelopes, so the stock amp chain (AMP_ENV/VCA/PUNCH) is not called: DECAY is
 * each voice's own decay, PUNCH is the 909's accent (t9.h), GATE does nothing. State: one t9_voice a track, set
 * up on the first note after a machine change (mark at v+0x2c, cleared by the voice reset 0x400a7ab8).
 */
#include <stdint.h>
#include "t9.h"

typedef int32_t s32;
typedef uint32_t u32;

#define CYC_VOICE0  0x42308828
#define CYC_VSTRIDE 0x31c
#define MARK_OFF    0x2c
#define MARK        0x54393039                       /* 'T909' */

#define V32(o)      (*(s32 *)(v + (o)))
#define P16(o)      (*(const int16_t *)(p + (o)))

static struct t9_voice voices[6];

static inline struct t9_voice *voice_of(const char *v)
{
	return &voices[((u32)v - CYC_VOICE0) / CYC_VSTRIDE];
}

static inline uint8_t knob(s32 x)                    /* 8.8 -> 0..127 */
{
	x >>= 8;
	return (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x);
}

static void update(int kind, s32 pmod, char *v, const char *p)
{
	struct t9_voice *t = voice_of(v);
	struct t9_knobs k;
	s32 n, d;

	if (V32(MARK_OFF) != MARK || t->kind != kind) {   /* first note on this machine */
		t9_init(t, kind, 0x9090u + (u32)v);
		V32(MARK_OFF) = MARK;
	}
	/* note: PITCH - 64 + the trig's note + FINE (+-2 semitones), like the stock machines */
	n = ((s32)P16(0x14) << 8) + (((s32)P16(0x22) - 0x4000) << 3) - (64 << 16) + pmod;
	k.note = n < 0 ? 0 : n > (127 << 16) ? 127 << 16 : n;
	d = (signed char)p[0x24];
	k.decay = (uint8_t)(d < 0 ? 0 : d > 127 ? 127 : d);
	k.color = knob(P16(0x16));
	k.shape = knob(P16(0x18));
	k.sweep = knob(P16(0x1a));
	k.contour = knob(P16(0x1c));
	k.accent = P16(0x1e) != 0;
	t9_update(t, &k, V32(0x38) != 0);
}

static void render(s32 *out, char *v)
{
	s32 buf[T9_BLOCK];
	int i;

	t9_render(voice_of(v), buf);
	for (i = 0; i < T9_BLOCK; i++) {                 /* Q15 -> Q31 / 2, clipped */
		s32 x = buf[i];
		x = x > 65535 ? 65535 : x < -65535 ? -65535 : x;
		out[i] = x << 15;
	}
#ifdef T9_AH_NOENV
	/* Model-TG's per-track stage, as for its stock machines: Attack, Filter/Resonance, the
	 * dry copy for track resampling, mute fades (ah_noenv, address given by gen_t9.py) */
	((void (*)(s32 *, char *))T9_AH_NOENV)(out, v);
#endif
}

#define ENTRY(name, kind)                                                         \
	void t9_##name##_update(s32 pmod, char *v, const char *p) { update(kind, pmod, v, p); } \
	void t9_##name##_render(s32 *out, char *v) { render(out, v); }

ENTRY(bd, T9_BD)
ENTRY(sd, T9_SD)
ENTRY(hh, T9_HH)
ENTRY(rc, T9_RC)
ENTRY(tom, T9_TOM)
ENTRY(cy, T9_CY)

#ifdef T9_STEMS
/*
 * USB stems after LEVEL (with the 6-channel USB mod). The mod sends each track's block as the voices leave it
 * (0x80001858), before the mixer, so LEVEL, VOL and mute never reached the USB tracks on the Cycles OS. Here the
 * mixer's call (0x40059872; with Model-TG, its wrapper) comes through t9_stems: that runs, then each track's block is copied into
 * t9_stems_buf times the track's mixer gain without its pan, and the mod's copy reads t9_stems_buf instead.
 *
 * The mixer keeps two gains a track, main left and right (0x40a78c08 / 0x40a78be8, negative Q31, ramped to their
 * targets over the block). Its pan law is a parabolic sine/cosine pair (0x40056524: 4x(1-x)), so sqrt(L^2 + R^2) is
 * LEVEL x VOL to within +-0.5 dB whatever the pan; a muted track's gains go to 0. The gain ramps over the block from
 * the last one, so moves and mutes stay smooth.
 */
#define MIXER       ((void (*)(u32, u32))T9_STEMS)        /* the mixer, or what wraps it (Model-TG) */
#define TRACK_OUT   ((const s32 *)0x80001858)
#define GAIN_L      ((const s32 *)0x40a78c08)
#define GAIN_R      ((const s32 *)0x40a78be8)

s32 t9_stems_buf[6 * T9_BLOCK];
static s32 stem_g[6];                                /* last block's gain, Q30 */

static inline s32 mul31(s32 a, s32 x)                /* a x / 2^31 (fractional EMAC, MACSR saved around) */
{
	s32 r;
	__asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(x));
	return r;
}

static u32 isqrt(u32 x)                              /* floor(sqrt(x)) */
{
	u32 r = 0, b = 1u << 30;

	while (b > x)
		b >>= 2;
	while (b) {
		if (x >= r + b) {
			x -= r + b;
			r = (r >> 1) + b;
		} else {
			r >>= 1;
		}
		b >>= 2;
	}
	return r;
}

void t9_stems(u32 a1, u32 a2)
{
	s32 macsr, acc0, z = 0;
	int t, i;

	MIXER(a1, a2);
	__asm__ volatile ("move.l %%macsr,%0\n\tmove.l %%acc0,%1\n\tmove.l #0x20,%%macsr\n\tmove.l %2,%%acc0"
			  : "=&d"(macsr), "=&d"(acc0) : "d"(z));
	for (t = 0; t < 6; t++) {
		const s32 *in = TRACK_OUT + 32 * t;
		s32 *out = t9_stems_buf + T9_BLOCK * t;
		s32 l = GAIN_L[t], r = GAIN_R[t];
		u32 sq = (u32)(mul31(l, l) >> 1) + (u32)(mul31(r, r) >> 1);   /* Q30, up to ~1.13 */
		s32 g1 = (s32)(isqrt(sq) << 15), g = stem_g[t], d = (g1 - g) >> 5;   /* Q30 */

		for (i = 0; i < T9_BLOCK; i++) {
			s32 y;
			g += d;
			y = mul31(in[i], g);                         /* x G / 2: clipped before the << 1 */
			out[i] = (y > 0x3fffffff ? 0x3fffffff : y < -0x40000000 ? -0x40000000 : y) << 1;
		}
		stem_g[t] = g1;
	}
	__asm__ volatile ("move.l %0,%%acc0\n\tmove.l %1,%%macsr" : : "d"(acc0), "d"(macsr));
}
#endif
