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

#define T9_BLOCK 32

enum { T9_BD, T9_SD, T9_HH, T9_RC, T9_TOM, T9_CY, T9_KINDS };

struct t9_knobs {
	int32_t note;                       /* semitones << 16 */
	uint8_t decay, color, shape, sweep, contour;
	uint8_t accent;                     /* PUNCH: the 909's accent, read at the trig */
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

struct t9_voice {
	uint8_t kind, sub, active;          /* machine, sub-voice (from SHAPE at the trig), sounding */
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
