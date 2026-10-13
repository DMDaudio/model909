/*
 * T9: TR-909-style voices in integer arithmetic. See t9.h.
 *
 * Written from scratch. The analog voices (t9_analog.inc) model the 909's circuit as described in public sources
 * (network-909.de circuit pages, Robin Whittle's TR-909 sound-mod notes, the TR-909 Service Notes; collected in
 * research/909-circuits.md), with the constants fitted to the 909 From Mars hits (proto/). No code was taken from
 * other emulations.
 *
 * Fixed point: audio is Q15 in int32, envelopes Q31 (Q28 for the bass drum's, which starts above 1 and so holds
 * flat), phases are 32-bit. Coefficients come from knob values once a block (cold code: log2/exp2 polynomials).
 */
#include "t9.h"
#include "t9_tables.h"
#include "t9_samples.h"

typedef int32_t s32;
typedef uint32_t u32;

#if defined(__GNUC__) && !defined(__clang__)
#define COLD __attribute__((noinline, optimize("Os")))
#else
#define COLD __attribute__((noinline))
#endif

#define SR 48000
#define ONE31 0x7fffffff
#define ACCENT_GAIN 6880                                   /* +4.5 dB, Q12 */

/* ---- multiply: a x / 2^31, truncated. The EMAC must be in fractional mode (t9_render sets it). ---------- */
#if defined(__mcoldfire__)
static inline s32 q31(s32 a, s32 x)
{
	s32 r;
	__asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=d"(r) : "r"(a), "r"(x));
	return r;
}
/* sums of products in the accumulator, read once (each separate q31 waits for the EMAC to finish before its read):
 * a x + b y, and a biquad's b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2 */
static inline s32 q31x2(s32 a, s32 x, s32 b, s32 y)
{
	s32 r;
	__asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
			  : "=d"(r) : "r"(a), "r"(x), "r"(b), "r"(y));
	return r;
}
static inline s32 q31bq(const s32 *c, s32 x, s32 x1, s32 x2, s32 y1, s32 y2)
{
	s32 r;
	__asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\t"
			  "msac.l %7,%8,%%acc0\n\tmsac.l %9,%10,%%acc0\n\tmovclr.l %%acc0,%0"
			  : "=d"(r) : "r"(c[0]), "r"(x), "r"(c[1]), "r"(x1), "r"(c[2]), "r"(x2), "r"(c[3]), "r"(y1),
			    "r"(c[4]), "r"(y2));
	return r;
}
struct emac_save { s32 macsr, acc0; };
static inline void emac_enter(struct emac_save *e)
{
	s32 z = 0;
	__asm__ volatile ("move.l %%macsr,%0\n\tmove.l %%acc0,%1\n\tmove.l #0x20,%%macsr\n\tmove.l %2,%%acc0"
			  : "=&d"(e->macsr), "=&d"(e->acc0) : "d"(z));
}
static inline void emac_leave(const struct emac_save *e)
{
	__asm__ volatile ("move.l %0,%%acc0\n\tmove.l %1,%%macsr" : : "d"(e->acc0), "d"(e->macsr));
}
#else
static inline s32 q31(s32 a, s32 x) { return (s32)(((int64_t)a * x) >> 31); }
/* the EMAC in fractional mode keeps each product a x to units of 2^-39 (its low 23 bits truncated), sums, and the
 * read takes the sum >> 31: modelled exactly (as tools/emu/emac.py), so the Mac build stays sample-exact */
static inline int64_t emac_p(s32 a, s32 x) { return ((int64_t)a * x) >> 23; }
static inline s32 q31x2(s32 a, s32 x, s32 b, s32 y) { return (s32)((emac_p(a, x) + emac_p(b, y)) >> 8); }
static inline s32 q31bq(const s32 *c, s32 x, s32 x1, s32 x2, s32 y1, s32 y2)
{
	return (s32)((emac_p(c[0], x) + emac_p(c[1], x1) + emac_p(c[2], x2) - emac_p(c[3], y1) - emac_p(c[4], y2)) >> 8);
}
struct emac_save { int unused; };
static inline void emac_enter(struct emac_save *e) { (void)e; }
static inline void emac_leave(const struct emac_save *e) { (void)e; }
#endif

/* ---- cold helpers ---------------------------------------------------------------------------------------- */

/* log2(x) in Q16, x >= 1 */
COLD static s32 lg(u32 x)
{
	s32 e = 31, f, p;

	if (!x)
		return 0;
	while (!(x & 0x80000000u)) {
		x <<= 1;
		e--;
	}
	f = (s32)((x >> 15) & 0xffff);
	p = -1381;
	p = 5302 + ((p * f) >> 16);
	p = -11110 + ((p * f) >> 16);
	p = 23569 + ((p * f) >> 16);
	return (e << 16) + (((p * f) >> 16) << 2);
}

/* 2^(x / 65536), 0 <= x < 32 << 16 */
COLD static u32 pow2(s32 x)
{
	s32 i = x >> 16;
	u32 f = (u32)x & 0xffff, p = 5071;

	p = 14873 + ((p * f) >> 16);
	p = 45576 + ((p * f) >> 16);
	p = 65536 + ((p * f) >> 16);
	if (i < 0)
		return 0;
	return i >= 16 ? p << (i - 16) : p >> (16 - i);
}

/* lo (hi / lo)^(knob / 127): an exponential knob between lo and hi (any unit, integers) */
COLD static u32 kexp(int knob, u32 lo, u32 hi)
{
	s32 a = lg(lo), b = lg(hi);
	return pow2(a + (b - a) * knob / 127);
}

/* per-sample decay factor for a time constant of ms16 / 16 milliseconds */
COLD static s32 kdecay(u32 ms16)
{
	u32 tau = ms16 * 3;                                  /* samples: ms16 / 16 x 48 */
	if (tau < 2)
		tau = 2;
	return (s32)(ONE31 - ONE31 / tau);
}

#define L2_INC_1HZ 1078018                                  /* log2(2^32 / 48000), Q16 */

/* phase increment of hz16 / 16 Hz, transposed by note - 60 semitones (note Q16) */
COLD static u32 inc_of(u32 hz16, s32 note)
{
	s32 x = L2_INC_1HZ + lg(hz16) - (4 << 16) + (note - (60 << 16)) / 12;
	if (x > (31 << 16))
		x = 31 << 16;
	return pow2(x);
}

/* x (r / 65536), x up to 2^31 */
static inline u32 ratio(u32 x, u32 r)
{
	return (x >> 16) * r + (((x & 0xffff) * r) >> 16);
}

/* one-pole coefficient (Q15) for a cutoff of hz: w / (1 + w), w = 2 pi hz / SR */
COLD static s32 kpole(u32 hz)
{
	u32 w = hz * 549u >> 6;                              /* Q16: 2 pi / 48000 x 2^16 = 8.578 = 549 / 64 */
	return (s32)((w << 8) / ((65536 + w) >> 7));
}

/* ---- audio helpers -------------------------------------------------------------------------------------- */

static inline s32 noise15(u32 *s)
{
	*s = *s * 1664525u + 1013904223u;
	return (s32)*s >> 16;
}

static inline s32 clip15(s32 x)
{
	return x > 32767 ? 32767 : x < -32768 ? -32768 : x;
}

#include "t9_analog.inc"

/* ---- hats and cymbals: IMA ADPCM samples ----------------------------------------------------------------- *
 * The 909 plays its hats and cymbals from 6-bit ROMs; here 909 recordings at 32 kHz, 4-bit ADPCM.
 *   PITCH: playback rate   DECAY: an extra decay on top of the sample (127 = the sample as it is)
 *   COLOR: tone (low-pass at 0, flat at 64, high-pass above)   SHAPE: 0 CH / 1 OH, or 0 crash / 1 ride
 *   SWEEP: bits (0: all, 127: 5 bits)   CONTOUR: -                                                           */

struct t9_sample { const uint8_t *data; u32 len; };
static const struct t9_sample samples[4] = {
	{ t9_s_ch, T9_S_CH_LEN }, { t9_s_oh, T9_S_OH_LEN }, { t9_s_crash, T9_S_CRASH_LEN }, { t9_s_ride, T9_S_RIDE_LEN },
};

static inline s32 ima_next(struct t9_voice *v)
{
	u32 at = v->dec_at++;
	s32 code = (v->adpcm[at >> 1] >> ((at & 1) << 2)) & 15, step = t9_ima_step[v->idx];
	s32 diff = step >> 3;

	if (code & 4) diff += step;
	if (code & 2) diff += step >> 1;
	if (code & 1) diff += step >> 2;
	v->pred += code & 8 ? -diff : diff;
	v->pred = clip15(v->pred);
	v->idx += t9_ima_idx[code];
	v->idx = v->idx < 0 ? 0 : v->idx > 88 ? 88 : v->idx;
	return v->pred;
}

COLD static void smp_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	if (trig) {
		/* SHAPE: 0 CH, 1 OH, 2 crash, 3 ride (one machine for all four: a new hit cuts the last, as the 909's
		 * closed hat chokes its open one) */
		const struct t9_sample *s = &samples[v->kind == T9_CY ? 2 + (k->shape ? 1 : 0) : k->shape > 3 ? 3 : k->shape];
		v->adpcm = s->data;
		v->len = s->len;
		v->pos = 0;
		v->dec_at = 0;
		v->pred = v->idx = 0;
		v->s0 = v->s1 = 0;
		v->aenv = ONE31;
		v->lp1 = 0;
		v->active = 1;
	}
	/* T9_S_RATE / SR x 2^(note - 60 / 12), Q16 */
	v->step = pow2((16 << 16) + lg(T9_S_RATE) - lg(SR) + (k->note - (60 << 16)) / 12);
	v->ka = k->decay >= 127 ? ONE31 : kdecay(kexp(k->decay, 20 * 16, 3000 * 16));
	v->mix = k->color;
	v->asym = ~((1 << (k->sweep * 11 / 127)) - 1);        /* bits kept: 16 .. 5 */
	v->fa = kpole(k->color < 64 ? kexp(k->color, 1500, 16000) : kexp(k->color - 64, 300, 6000));
}

static void smp_render(struct t9_voice *v, s32 *out)
{
	s32 i, y, f;

	for (i = 0; i < T9_BLOCK; i++) {
		u32 want = (v->pos >> 16) + 1;
		if (want >= v->len) {
			v->active = 0;
			for (; i < T9_BLOCK; i++)
				out[i] = 0;
			return;
		}
		while (v->dec_at <= want) {
			v->s0 = v->s1;
			v->s1 = ima_next(v);
		}
		f = (s32)((v->pos >> 1) & 0x7fff);
		y = v->s0 + (((v->s1 - v->s0) * f) >> 15);
		v->pos += v->step;
		y &= v->asym;
		y = (y * (v->aenv >> 16)) >> 15;
		v->aenv = q31(v->aenv, v->ka);
		v->lp1 += ((y - v->lp1) * v->fa) >> 15;
		if (v->mix < 64)
			y = v->lp1;
		else if (v->mix > 64)
			y -= v->lp1;
		out[i] = y * v->acc >> 12;
	}
	if (v->aenv < 0x40000)
		v->active = 0;
}

/* ---- entry points ---------------------------------------------------------------------------------------- */

void t9_init(struct t9_voice *v, int kind, u32 seed)
{
	char *p = (char *)v;
	u32 i;

	for (i = 0; i < sizeof *v; i++)
		p[i] = 0;
	v->kind = (uint8_t)kind;
	v->rng = seed;
}

void t9_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct emac_save e;

	emac_enter(&e);
	if (trig || !v->acc)                                  /* the analog voices take the accent themselves */
		v->acc = k->accent && (v->kind == T9_HH || v->kind == T9_CY) ? ACCENT_GAIN : 4096;
	if (trig)
		v->start = 1;
	/* the coefficients only change with the knobs or the note: without a trig and with the same knobs (most
	 * blocks, and every block of a silent track), there is nothing to recompute */
	if (!trig && v->kn.note == k->note && v->kn.decay == k->decay && v->kn.color == k->color &&
	    v->kn.shape == k->shape && v->kn.sweep == k->sweep && v->kn.contour == k->contour && v->kn.accent == k->accent) {
		emac_leave(&e);
		return;
	}
	v->kn = *k;
	switch (v->kind) {
	case T9_BD: bd_update(v, k, trig); break;
	case T9_SD: sd_update(v, k, trig); break;
	case T9_TOM: tom_update(v, k, trig); break;
	case T9_RC: rc_update(v, k, trig); break;
	default: smp_update(v, k, trig); break;
	}
	emac_leave(&e);
}

/* A new hit restarts the voice from zero while the last one may still sound: the output would step from the old
 * note's last sample to the new one's first (a click, measured up to 19000 on a BD retriggered mid-decay). The step
 * is added back and faded out on a half cosine over 2 ms; the new hit itself is unchanged. */
static void declick(struct t9_voice *v, s32 *out)
{
	int i;

	if (v->start) {
		s32 d = v->last - out[0];
		v->dc_off = d > 65535 ? 65535 : d < -65535 ? -65535 : d;
		v->dc_n = 0;
		v->start = 0;
	}
	for (i = 0; i < T9_BLOCK && v->dc_n < T9_DECLICK_N; i++, v->dc_n++)
		out[i] += (v->dc_off * t9_declick[v->dc_n]) >> 15;
	v->last = out[T9_BLOCK - 1];
}

void t9_render(struct t9_voice *v, s32 *out)
{
	struct emac_save e;
	int i;

	if (!v->active) {
		for (i = 0; i < T9_BLOCK; i++)
			out[i] = 0;
		if (v->dc_n < T9_DECLICK_N && v->dc_off)
			declick(v, out);
		else
			v->last = 0;
		return;
	}
	emac_enter(&e);
	switch (v->kind) {
	case T9_BD: bd_render(v, out); break;
	case T9_SD: sd_render(v, out); break;
	case T9_TOM: tom_render(v, out); break;
	case T9_RC: if (v->sub) clap_render(v, out); else rim_render(v, out); break;
	default: smp_render(v, out); break;
	}
	emac_leave(&e);
	declick(v, out);
}
