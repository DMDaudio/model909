/*
 * T9: TR-909-style voices for the Model:Cycles voice loop, in integer arithmetic (no FPU on the MCF54415).
 * Written from scratch from the 909's published circuit descriptions and measurements of real 909 hits.
 *
 * One voice per track. Each machine replaces a stock one:
 *   T9_BD   (KICK)   bass drum
 *   T9_SD   (SNARE)  snare drum
 *   T9_HH   (METAL)  hi-hats, ROM-style sample playback: SHAPE picks CH / OH
 *   T9_RC   (PERC)   SHAPE picks rim shot / hand clap
 *   T9_TOM  (TONE)   SHAPE picks low / mid / high tom
 *   T9_CY   (CHORD)  cymbals, sample playback: SHAPE picks crash / ride
 *
 * t9_update() once per 32-sample block (reads the knobs, starts a note on trig), then t9_render().
 * Knobs are 0..127; note is semitones << 16 (60 << 16 = the 909's own pitch). accent (PUNCH) on a trig: as the
 * 909's accent, it raises the analog voices' envelope start voltages (louder, and on the BD and SD also longer);
 * the sampled hats and cymbals play +4.5 dB louder.
 */
#ifndef T9_H
#define T9_H

#include <stdint.h>

#define T9_BANK_SIZE 0x2c000u                              /* the sample bank (t9.c): 180224 bytes */
#define T9_BLOCK 32

enum { T9_BD, T9_SD, T9_HH, T9_RC, T9_TOM, T9_CY, T9_KINDS };

struct t9_knobs {
	int32_t note;                       /* semitones << 16 */
	uint8_t decay, color, shape, sweep, contour;
	uint8_t accent;                     /* PUNCH: the 909's accent, read at the trig */
	uint8_t fine;                       /* FINE, 0..127: the 909 kick's drive and snare's tone (SHAPE picks 909/808/606) */
};

/* the analog voices' parts (t9_analog.inc) */
struct t9_tri { int32_t v, dir; };                      /* triangle VCO, Q29 */
struct t9_bq { int32_t x1, x2, y1, y2; };               /* biquad states */
struct t9_svf { int32_t s1, s2; };                      /* state-variable filter states */
struct svfc { int32_t g, kg, h, k; };                   /* and its coefficients */
struct t9_tomcal;

struct t9_bdv {
	int32_t inc_b, inc_df, kp, tp, ka, att;            /* coefficients (from the knobs) */
	struct t9_tri t;
	int32_t vp, v1, env2, pulse, dif, nhp, nlp, coup;
	struct t9_bq sk;
};
struct t9_sdv {
	int32_t inc1, inc2, inc_b, k1, k2, ka4, ga, gb, gd;
	struct svfc lpc;
	struct t9_tri t1, t2;
	int32_t b, e1, e2, sm, e5, a, v4, vend, nhp, hpa, hpo;
	struct t9_svf lp;
	struct t9_bq hpb;
};
struct t9_tomv {
	const struct t9_tomcal *c;
	int32_t inc1, inc2, inc3, vdd, inv_vdd, kf, kf4, ks4, kod32, kd, nzl, tickl;
	struct t9_tri t1, t2, t3;
	int32_t vf, vs, od, e1, e3, e3n, ca, cb, tick, a, g;   /* e1, e3, e3n, ca, cb, tick: Q28, levels included */
	struct t9_bq nbp;
};
struct t9_rsv {
	struct svfc rc[3];
	int32_t g[3], w3, kv;
	struct t9_svf r[3];
	int32_t kick, env, lp;
	struct t9_bq hp;
};
struct t9_hcv {
	struct svfc bpc;
	int32_t kc, klast, kd, tiltmix;
	int32_t on[3], nb, ctl, er, ed, acc, hp1, tilt, tlp;
	struct t9_svf bp;
};

/* the 808's (t9_808.inc) */
struct t9_bd8v { struct t9_svf r; int32_t sx1, sy1, A, lp, hp, klp, vt, sigh; uint32_t n22, fb16, fatt16; };
struct t9_sd8v { struct svfc lc, hc; struct t9_svf lo, hi; struct t9_bq hpb; int32_t prev, cap, sm, hp2, lp, vt, snap, tone; };
struct t9_tom8cal;
struct t9_bank8 { uint32_t ph[6], inc[6]; };
struct t9_cb8v { struct t9_bank8 bank; int32_t ef, es, lp; struct t9_bq bp1, bp2; };
struct t9_hh8v { struct t9_bank8 bank; int32_t e, tune, sx1, sy1; uint32_t delay; uint8_t open; struct t9_bq bp, hp; };
struct t9_cy8v { struct t9_bank8 bank; int32_t lr, el, hf, ht, kd, tone, sx1, sy1; struct t9_bq bp1, bp2, hp1, hp2; };
struct t9_cp8v { int32_t vt, eb, r1, r2, bhp, lp; struct t9_bq bp, rb; };
struct t9_rs8v { struct svfc lc, hc; struct t9_svf lo, hi; int32_t vt, g, fade; struct t9_bq hp; };
struct t9_ma8v { int32_t gate, a, hp1, lp; struct t9_bq hp2; };
/* the 606's (t9_606.inc) */
struct t9_bd6v { struct svfc bc, cc; struct t9_svf b, c; int32_t vt, lp; };
struct t9_sd6v { struct t9_svf r; struct t9_bq hp; uint32_t f16, n22; int32_t vt, dif, prev, gl, cap, sm, kd, hp2, lp; };
struct t9_hh6v { struct t9_bank8 bank; int32_t e, k, tune, h1; uint8_t open; struct t9_bq bp, hp; };
struct t9_cy6v { struct t9_bank8 bank; int32_t ea, ebf, ebs, es, ka, kf, ks, ld; struct t9_bq bp, hb, hd; };
struct t9_tom6cal;
struct t9_tom6v { const struct t9_tom6cal *c; struct svfc gc; struct t9_svf r, g; struct t9_bq pn; uint32_t f16, n22;
	int32_t vt, dif, prev, gl, cap, nh, nl, lp; };
struct t9_tom8v { const struct t9_tom8cal *c; uint32_t f16; int32_t vt, dif, prev, A, cap, nhp, nlp, olp; struct t9_svf r; struct t9_bq pn; };

struct t9_voice {
	uint8_t kind, sub, active;          /* machine, sub-voice (from SHAPE at the trig), sounding */
	uint8_t fam;                        /* 0 909, 1 808, 2 606 (from SHAPE at the trig) */
	uint32_t rng;
	int32_t t;                          /* samples since the trig */
	int32_t acc;                        /* output gain, Q12 (the sample voices' accent) */
	int32_t last, dc_off, dc_n;         /* retrigger declick: last output, the step to fade, where in the fade */
	struct t9_knobs kn;                 /* the knobs the coefficients were last made from */
	uint8_t start;                      /* a note starts in this block */
	union {
		struct t9_bdv bd;
		struct t9_sdv sd;
		struct t9_tomv tom;
		struct t9_rsv rs;
		struct t9_hcv hc;
		struct t9_bd8v bd8;
		struct t9_sd8v sd8;
		struct t9_tom8v tom8;
		struct t9_cb8v cb8;
		struct t9_hh8v hh8;
		struct t9_cy8v cy8;
		struct t9_cp8v cp8;
		struct t9_rs8v rs8;
		struct t9_ma8v ma8;
		struct t9_bd6v bd6;
		struct t9_sd6v sd6;
		struct t9_tom6v tom6;
		struct t9_hh6v hh6;
		struct t9_cy6v cy6;
	} a;
	/* sample playback */
	int32_t aenv, ka, mix, asym, fa, lp1;
	const uint8_t *adpcm;
	uint32_t len, pos, step, dec_at;    /* length (samples), position (Q16), step (Q16), next sample to decode */
	int32_t pred, idx, s0, s1;          /* IMA state, last two decoded samples */
};

void t9_init(struct t9_voice *v, int kind, uint32_t seed);
void t9_update(struct t9_voice *v, const struct t9_knobs *k, int trig);
/* 32 samples, Q15 range (int32, may exceed +-32767 a little before the caller's clip) */
void t9_render(struct t9_voice *v, int32_t *out);

#endif
