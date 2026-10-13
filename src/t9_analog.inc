/* ---- the analog voices, modelled on the 909's circuit (research/909-circuits.md) ----------------------------- *
 * Included by t9.c. Each voice is the float prototype in proto/t9f.c in fixed point; t9_cal.h (written by
 * proto/gen_cal.py from the fits to the Mars hits) holds its part values and fitted constants.
 *
 * Units: volts are Q24 (the circuit's own voltages, +-128 V), simple envelopes Q31 (0..1), the triangle VCOs Q29
 * (+-1), filter coefficients Q30 (biquads) or Q28 (state-variable filters). The voices start on the trig's
 * 2 ms pulse as the 909's do: the VCOs are held in reset for its length, the clicks are made from it.
 * Accent (PUNCH) sets the envelopes' start voltages, as on the 909, instead of a gain after the voice.          */

#include "t9_cal.h"

#define TRIG_N 96                                          /* the trig pulse: 2 ms */
#define VOLT (1 << 24)
#define KNEE (VOLT / 50)                                   /* the VCA transistor's knee: 20 mV */

/* the VCA transistor: gain = V - Vth above the knee, 0 below it, a parabola between (smooth, as softplus) */
static inline s32 vca_g(s32 d)
{
	if (d >= KNEE)
		return d;
	if (d <= -KNEE)
		return 0;
	d = (d + KNEE) >> 4;
	return (s32)((u32)(d * d) / (u32)((4 * KNEE) >> 8));
}

/* an anti-parallel diode pair (t9_tables.h: u(x) over 0..8 V in 1/32 V steps, then to 128 V in 1/2 V steps) */
static inline s32 diode(const int16_t *tab, s32 x)
{
	u32 a = (u32)(x < 0 ? -x : x);
	s32 i, f, y;

	if (a < (8u << 24)) {
		i = (s32)(a >> 19);
		f = (s32)((a >> 4) & 0x7fff);
	} else {
		a -= 8u << 24;
		if (a >= (120u << 24))
			a = (120u << 24) - 1;
		i = 256 + (s32)(a >> 23);
		f = (s32)((a >> 8) & 0x7fff);
	}
	y = (tab[i] + (((tab[i + 1] - tab[i]) * f) >> 15)) << 9;
	return x < 0 ? -y : y;
}

/* triangle VCO, Q29: starts at 0 rising; up / dn: the slopes (4 f / SR, Q29) of its two halves */
static inline s32 tri_next(struct t9_tri *t, s32 up, s32 dn)
{
	s32 x = t->v;

	if (t->dir >= 0) {
		x += up;
		if (x > 0x20000000) {
			x = 0x40000000 - x;
			t->dir = -1;
		}
	} else {
		x -= dn;
		if (x < -0x20000000) {
			x = -0x40000000 - x;
			t->dir = 1;
		}
	}
	t->v = x;
	return x;
}

/* biquad, direct form I, coefficients b0 b1 b2 a1 a2 in Q30 */
static inline s32 bq_run(struct t9_bq *f, const s32 *c, s32 x)
{
	s32 y = q31bq(c, x, f->x1, f->x2, f->y1, f->y2) << 1;

	f->x2 = f->x1;
	f->x1 = x;
	f->y2 = f->y1;
	f->y1 = y;
	return y;
}

/* topology-preserving state-variable filter: coefficients g, k + g, h = 1 / (1 + g (k + g)), Q28 */

struct svfo { s32 lp, bp, hp; };
static inline void svf_run(struct t9_svf *f, const struct svfc *c, s32 x, struct svfo *o)
{
	s32 hp = q31(x - (q31(c->kg, f->s1) << 3) - f->s2, c->h) << 3, gh = q31(c->g, hp) << 3, gb;

	o->hp = hp;
	o->bp = gh + f->s1;
	f->s1 = gh + o->bp;
	gb = q31(c->g, o->bp) << 3;
	o->lp = gb + f->s2;
	f->s2 = gb + o->lp;
}

/* 1 / d for d in Q28 (1/8 .. 8): Q28. A 16-bit division, then one Newton step. */
COLD static s32 recip28(s32 d)
{
	s32 r = (s32)((0x80000000u / (u32)(d >> 12)) << 13), e;   /* 2^31 / (d 2^16) = 2^15 / d: Q15 -> Q28 */

	e = (2 << 28) - (q31(d, r) << 3);                          /* 2 - d r, Q28 */
	return q31(r, e) << 3;
}

/* sin and cos of w (Q30, 0 .. 1.2): Taylor series, Q30 */
COLD static void sincos30(s32 w, s32 *s, s32 *c)
{
	s32 w2 = q31(w, w) << 1, t;                               /* Q30 */

	t = (1 << 30) - (q31(w2, (1 << 30) / 42) << 1);            /* sin: w (1 - w2/6 (1 - w2/20 (1 - w2/42))) */
	t = (1 << 30) - (q31(w2, q31(t, (1 << 30) / 20) << 1) << 1);
	t = (1 << 30) - (q31(w2, q31(t, (1 << 30) / 6) << 1) << 1);
	*s = q31(w, t) << 1;
	t = (1 << 30) - (q31(w2, (1 << 30) / 56) << 1);            /* cos: 1 - w2/2 (1 - w2/12 (1 - w2/30 (1 - w2/56))) */
	t = (1 << 30) - (q31(w2, q31(t, (1 << 30) / 30) << 1) << 1);
	t = (1 << 30) - (q31(w2, q31(t, (1 << 30) / 12) << 1) << 1);
	*c = (1 << 30) - (q31(w2, q31(t, (1 << 30) / 2) << 1) << 1);
}

/* SVF coefficients for a cutoff given as a phase increment (2^32 f / SR, at most 18 kHz) and 1/Q (Q28) */
COLD static void svf_set(struct svfc *c, u32 inc, s32 kq)
{
	s32 w, s, co;

	if (inc > 1431655765u)                                     /* 16 kHz */
		inc = 1431655765u;
	w = q31((s32)(inc >> 1), 0x6487ed51) << 1;                 /* pi f / SR, Q30 */
	sincos30(w, &s, &co);
	c->g = q31(s, recip28(co >> 2)) << 1;                      /* tan, Q28 */
	c->k = kq;
	c->kg = kq + c->g;
	c->h = recip28((1 << 28) + (q31(c->g, c->kg) << 3));
}

/* per-sample decay factor exp(-1 / tau) for a time constant of tau16 / 16 samples: a series, Q31 */
COLD static s32 kexpo(u32 tau16)
{
	s32 x, x2;

	if (tau16 < 32)
		tau16 = 32;
	x = (s32)((0x80000000u / tau16) << 4) - 1;                 /* 1 / tau, Q31 */
	x2 = q31(x, x);
	return ONE31 - x + (x2 >> 1) - q31(x2, x) / 6;
}

/* a knob as a multiplier 2^((k - 64) / 32): 1/4 .. 4, Q16 */
COLD static u32 kscale(int knob)
{
	return pow2((16 << 16) + (knob - 64) * 2048);
}

/* accent: the envelope start voltages, a + (1 - a) x accent (a < 1: an unaccented hit starts lower) */
#define ACC(a, acc) ((acc) ? (1 << 30) : (a))

/* ---- BD -------------------------------------------------------------------------------------------------- *
 * VCO: a triangle whose frequency is linear in the pitch envelope (f = fb + df e^(-t/tau)), through the diode
 * pair; ENV1: a 62 ms hold, then the cap discharges to ground (DECAY) and into the VCA's base, so the tail stops at
 * the transistor's threshold. Click (ATTACK): the 2 ms trig pulse differentiated and rung by a 3.7 kHz low-pass,
 * plus noise low-passed at 513 Hz, on a 0.7 ms envelope.
 *   PITCH: fb (49.5 Hz) and the sweep   DECAY: the 909's DECAY   COLOR: ATTACK (0: off, 16..127: the 909's range)
 *   SHAPE: the diode drive (64: the 909; down to a sine, up to 10x: square)   SWEEP: sweep depth (64: 230 Hz)
 *   CONTOUR: the 909's TUNE, the sweep's time constant 4..40 ms                                                */

COLD static void bd_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_bdv *b = &v->a.bd;
	u32 p, rd;

	b->inc_b = (s32)(inc_of(BD_FB_HZ16, k->note) >> 1);
	b->inc_df = (s32)ratio(inc_of(BD_DF_HZ16, k->note) >> 1, kscale(k->sweep));
	b->kp = kexpo(kexp(k->contour, 4 * 48 * 16, 40 * 48 * 16));
	if (k->shape <= 64)
		b->tp = BD_TP_SINE + ((BD_TP - BD_TP_SINE) >> 6) * k->shape;
	else
		b->tp = (s32)ratio(BD_TP, pow2((16 << 16) + (k->shape - 64) * 3456));
	p = pow2((16 << 16) + k->decay * 3428);                   /* 10^(2 DECAY), Q16: the 1 MOhm audio-taper pot */
	rd = 47000 + (((p - 65536) >> 8) * 10101 >> 8);
	b->ka = (s32)((BD_KA / rd) << BD_KA_SH);
	b->att = k->color < 16 ? (BD_ATT0 >> 4) * k->color : BD_ATT0 + (BD_ATT1 - BD_ATT0) / 111 * (k->color - 16);
	if (trig) {
		b->t.v = 0;
		b->dif = b->nhp = b->nlp = b->coup = 0;
		b->sk.x1 = b->sk.x2 = b->sk.y1 = b->sk.y2 = 0;
		b->t.dir = 1;
		b->vp = ONE31;
		b->v1 = q31(BD_VSTART, ACC(BD_ACC, k->accent)) << 1;
		if (b->v1 > BD_CLAMP)
			b->v1 = BD_CLAMP;
		b->pulse = k->accent ? BD_PULSE_ACC : BD_PULSE;
		b->env2 = ONE31;
		v->t = 0;
		v->active = 1;
	}
}

static void bd_render(struct t9_voice *v, s32 *out)
{
	struct t9_bdv *b = &v->a.bd;
	s32 i, y, u, x, c, n;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		u = 0;
		if (v->t >= TRIG_N) {
			s32 inc = b->inc_b + q31(b->inc_df, b->vp);
			b->vp = q31(b->vp, b->kp);
			x = q31(tri_next(&b->t, inc, inc) << 1, b->tp) << 1;
			u = diode(t9_diode_vco, x);
			u += q31(q31(u << 7, u), BD_H2);
			b->env2 = q31(b->env2, BD_KENV2);
		}
		if (v->t > BD_HOLD)
			b->v1 -= q31x2(b->v1, b->ka, b->v1 > BD_THR ? b->v1 - BD_THR : 0, BD_KB);
		y = q31(q31(u << 7, vca_g(b->v1 - BD_THR)), BD_BODY) << 3;
		/* the click, while its envelope is audible (it is gone ~20 ms after the trig) */
		if (b->env2 >= 0x8000) {
			x = v->t < TRIG_N ? b->pulse : 0;
			b->dif += q31(x - b->dif, BD_DIF);
			c = q31(bq_run(&b->sk, bd_sk, x - b->dif), BD_CLICK);
#if BD_NZ
			n = noise15(&v->rng) << 9;
			b->nhp += q31(n - b->nhp, BD_NHP);
			b->nlp += q31(n - b->nhp - b->nlp, BD_NLP);
			c += q31(b->nlp, BD_NZ);
#else
			(void)n;
#endif
			y += q31(q31(c << 3, b->env2), b->att) << 1;
		}
		b->coup += q31(y - b->coup, BD_COUP);
		out[i] = q31(y - b->coup, BD_OUT);
	}
	if (v->t > BD_HOLD && b->v1 < BD_THR - KNEE && b->env2 < 0x10000)
		v->active = 0;
}

/* ---- SD -------------------------------------------------------------------------------------------------- *
 * Two triangle VCOs (Mars: 198 and 315 Hz) through diode pairs, each on its own pure-exponential VCA, summed
 * through a soft clip (which makes the Mars hits' 2 f1 - f2 line at ~80 Hz); the trig
 * bends both up by a constant number of Hz (5 ms). Snappy: noise, high-passed, through a resonant low-pass, then
 * (a) high-passed at 1.5 kHz on ENV4: a 24 ms hold, then the cap heads below the VCA's threshold (the 909's TONE
 * sets how fast), so the snappy stops dead; (b) a 4 kHz high-pass on a 2 ms burst.
 *   PITCH: tune   DECAY: the 909's TONE (snappy length)   COLOR: SNAPPY   SHAPE: the noise's low-pass (brightness)
 *   SWEEP: the bend (64: the 909)   CONTOUR: the drum's decay (64: the 909)                                    */

COLD static void sd_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_sdv *s = &v->a.sd;
	u32 sc = kscale(k->contour), rdec;

	s->inc1 = (s32)(inc_of(SD_F1_HZ16, k->note) >> 1);
	s->inc_b = (s32)ratio(inc_of(SD_BEND_HZ16, 60 << 16) >> 1, kscale(k->sweep));
	s->inc2 = q31(s->inc1 << 2, SD_R2);
	s->k1 = kexpo(ratio(SD_T1_16, sc));
	s->k2 = kexpo(ratio(SD_T2_16, sc));
	rdec = 100000 + 500000 / 127 * k->decay;
	s->ka4 = (s32)((SD_KA4 / rdec) << SD_KA4_SH);
	s->ga = q31(SD_SGAIN, ONE31 / 127 * k->color);             /* SNAPPY x the paths' gains, Q24 */
	s->gb = q31(SD_BGAIN, ONE31 / 127 * k->color);
	svf_set(&s->lpc, ratio(inc_of(SD_LPF_HZ16, 60 << 16), kscale(k->shape)), SD_LPK);
	if (trig) {
		s32 a = ACC(SD_ACC, k->accent);
		s->t1.v = s->t2.v = 0;
		s->t1.dir = s->t2.dir = 1;
		s->b = ONE31;
		s->e1 = s->e2 = ONE31;
		s->sm = 0;
		s->e5 = ONE31;
		s->a = a;
		s->gd = q31(SD_DOUT, a) << 1;                  /* the drum: clip gain x accent x level x output */
		s->v4 = q31(SD_V4, a) << 1;
		s->vend = q31(SD_VEND, a) << 1;
		s->nhp = s->hpa = s->hpo = 0;
		s->lp.s1 = s->lp.s2 = 0;
		s->hpb.x1 = s->hpb.x2 = s->hpb.y1 = s->hpb.y2 = 0;
		v->t = 0;
		v->active = 1;
	}
}

static void sd_render(struct t9_voice *v, s32 *out)
{
	struct t9_sdv *s = &v->a.sd;
	s32 i, drum, x, sa, sb, y;
	struct svfo o;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		drum = 0;
		if (v->t >= TRIG_N)
			s->e5 = q31(s->e5, SD_K5);
		if (v->t >= TRIG_N && (s->e1 >= 0x8000 || s->e2 >= 0x8000)) {
			s32 i1 = s->inc1, i2 = s->inc2, u1, u2;
			if (s->b >= 0x8000) {                          /* the bend (gone ~60 ms after the trig) */
				i1 += q31(s->inc_b, s->b);
				i2 = q31(i1 << 2, SD_R2);              /* SD_R2: Q29 */
				s->b = q31(s->b, SD_KB);
			}
			u1 = diode(t9_diode_vco, q31(tri_next(&s->t1, i1, i1) << 1, SD_TP) << 1);
			u2 = diode(t9_diode_vco, q31(tri_next(&s->t2, i2, i2) << 1, SD_TP) << 1);
			drum = q31x2(u1, s->e1, q31(u2, s->e2), SD_L2 << 1);   /* SD_L2: Q30, < 1 */
			if (v->t < TRIG_N + 960) {                     /* the VCAs' 1 ms attack (done after 20 ms) */
				s->sm = ONE31 - q31(ONE31 - s->sm, SD_KS);
				drum = q31(drum, s->sm);
			}
			/* the VCAs' summing stage clips softly (the 2 f1 - f2 line): a diode pair; its gain, the accent
			 * (after the clip, so an accented hit keeps its ~5 dB over a plain one) and the drum's level are
			 * one multiplier at the output */
			drum = diode(t9_diode_vco, q31(drum << 2, SD_DSAT) << 5);
			s->e1 = q31(s->e1, s->k1);
			s->e2 = q31(s->e2, s->k2);
		}
		if (v->t > SD_HOLD4)
			s->v4 -= q31x2(s->v4 - s->vend, s->ka4, s->v4 > SD_THR ? s->v4 - SD_THR : 0, SD_KB4);
		y = 0;
		if (s->v4 > SD_THR - KNEE || s->e5 >= 0x8000) {    /* a snappy path is open */
			x = noise15(&v->rng) << 7;                 /* 0.25 V */
			s->nhp += q31(x - s->nhp, SD_NHP);
			svf_run(&s->lp, &s->lpc, x - s->nhp, &o);
			x = o.lp;
			s->hpa += q31(x - s->hpa, SD_HPA);
			sa = q31((x - s->hpa) << 4, vca_g(s->v4 - SD_THR)) << 3;    /* V^2: Q24 << 4 x Q24 >> 31 << 3 */
			sb = s->e5 >= 0x8000 ? q31(bq_run(&s->hpb, sd_hpb, x), q31(s->e5, s->a) << 1) : 0;
			y = q31x2(sa, s->ga, sb, s->gb) << 7;      /* the gains x SNAPPY: Q24 */
		}
		s->hpo += q31(y - s->hpo, SD_HPO);
		out[i] = q31x2(drum, s->gd, y - s->hpo, SD_OUT);
	}
	if (v->t > SD_HOLD4 && s->v4 < SD_THR - KNEE && s->e1 < 0x40000 && s->e2 < 0x40000)
		v->active = 0;
}

/* ---- toms ------------------------------------------------------------------------------------------------ *
 * Three triangle VCOs (1 : ~0.62 : ~1.7) on one pair of rails: the trig pushes only the negative rail (a fast and
 * a slow cap), so the falling slopes run faster than the rising ones (the pitch dive, and its even harmonics).
 * VCO1 is overdriven into the diodes for its first ~20 ms (near-square). VCAs: VCO1 on ENV1 (plus a share of
 * ENV2), VCO2 on ENV2 (swells in, then DECAY), VCO3 and band-passed noise on the short ENV3, plus a noise tick.
 *   PITCH: tune   DECAY: the 909's DECAY   COLOR: noise (64: the 909, 0: none)   SHAPE: 0 LT, 1 MT, 2 HT
 *   SWEEP: the pitch dive's depth   CONTOUR: its time                                                         */

COLD static void tom_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_tomv *m = &v->a.tom;
	const struct t9_tomcal *c;
	u32 sw;

	if (trig)
		v->sub = k->shape > 2 ? 2 : k->shape;
	c = &tom_cal[v->sub];
	sw = kscale(k->sweep);
	m->c = c;
	m->inc1 = (s32)(inc_of(c->f1_hz16, k->note) >> 1);
	m->vdd = (s32)ratio(c->vdd, pow2((16 << 16) + (k->note - (60 << 16)) / 12));
	m->inv_vdd = recip28(m->vdd << 4) << 2;                    /* 1 / VDD, Q30 */
	m->kf = kexpo(ratio(c->tfast16, kscale(k->contour)));
	m->kd = (s32)(ONE31 / (kexp(k->decay, c->t2min16, c->t2max16) >> 4));   /* Euler: 1 / tau */
	m->inc2 = q31(m->inc1 << 2, c->r2);                        /* r2, r3: Q29 */
	m->inc3 = q31(m->inc1 << 2, c->r3);
	m->nzl = (s32)((u32)c->nz / 64 * k->color);                /* COLOR: 64 = the fit, 0 = none (Q28) */
	m->tickl = (s32)((u32)c->tick / 64 * k->color);
	m->kf4 = q31(q31(m->kf, m->kf), q31(m->kf, m->kf));        /* 4 samples' worth */
	m->ks4 = q31(q31(c->ks, c->ks), q31(c->ks, c->ks));
	m->kod32 = c->kod;
	{
		int j;
		for (j = 0; j < 5; j++)
			m->kod32 = q31(m->kod32, m->kod32);        /* 32 samples' worth */
	}

	if (trig) {
		m->t1.v = m->t2.v = m->t3.v = 0;
		m->t1.dir = m->t2.dir = m->t3.dir = 1;
		m->vf = (s32)ratio(c->vfast, sw);
		m->vs = (s32)ratio(c->vslow, sw);
		m->od = ONE31;
		m->e1 = c->l1;                                 /* Q28: the levels ride on the envelopes */
		m->e3 = c->l3;
		m->e3n = m->nzl;
		m->ca = c->l2;
		m->cb = 0;
		m->tick = m->tickl;
		m->a = ACC(c->acc, k->accent);
		m->g = q31(c->out, m->a) << 1;
		m->nbp.x1 = m->nbp.x2 = m->nbp.y1 = m->nbp.y2 = 0;
		v->t = 0;
		v->active = 1;
	}
}

/* VCO1's overdrive as a gain into the diodes: (rs + rl) / (rs / d + rl), d = 1 + (OD - 1) x the timer (once a block) */
COLD static s32 tom_drive(const struct t9_tomcal *c, s32 od)
{
	s32 d = (1 << 24) + q31(c->od_m1, od);                     /* Q24 */
	s32 den = (s32)(((u32)10000 << 12) / (u32)(d >> 12)) + 2200;   /* rs / d + rl, ohms */
	return (s32)(((u32)12200 << 16) / (u32)den) << 8;          /* Q24 */
}

static void tom_render(struct t9_voice *v, s32 *out)
{
	struct t9_tomv *m = &v->a.tom;
	const struct t9_tomcal *c = m->c;
	s32 i, y, nz, tp1 = c->tp, dn1 = 0, dn2 = 0, dn3 = 0;

	if (v->t >= TRIG_N) {
		tp1 = q31(c->tp << 2, tom_drive(c, m->od)) << 5;    /* VCO1's drive x its overdrive, once a block */
		m->od = q31(m->od, m->kod32);                      /* the overdrive timer: once a block */
	}
	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		nz = 0;
		if (m->e3 >= 0x8000 || m->tick >= 0x8000)           /* the noise: only while ENV3 or the tick lasts */
			nz = bq_run(&m->nbp, c->nbp, noise15(&v->rng) << 9);
		y = 0;
		if (v->t >= TRIG_N) {
			s32 u1, u2, u3;
			if (!(i & 3)) {                                /* the pitch envelope: every 4 samples */
				s32 r = q31(m->vdd + m->vf + m->vs, m->inv_vdd) << 5;   /* |VSS| / VDD, Q28 */
				dn1 = q31(m->inc1 << 3, r);
				dn2 = q31(m->inc2 << 3, r);
				dn3 = q31(m->inc3 << 3, r);
				m->vf = q31(m->vf, m->kf4);
				m->vs = q31(m->vs, m->ks4);
			}
			u1 = diode(t9_diode_vco, q31(tri_next(&m->t1, m->inc1, dn1) << 1, tp1) << 1);
			u2 = diode(t9_diode_vco, q31(tri_next(&m->t2, m->inc2, dn2) << 1, c->tp) << 1);
			tri_next(&m->t3, m->inc3, dn3);
			u3 = m->e3 >= 0x8000 ? diode(t9_diode_vco, q31(m->t3.v << 1, c->tp) << 1) : 0;
			/* ENV2 (cb, scaled by VCO2's level) swells in from C_A; ENV1 already carries VCO1's level, ENV3
			 * VCO3's and the noise's: the sum is Q21 */
			m->cb += q31(m->ca - m->cb, c->kr);
			m->ca -= q31(m->ca, m->kd);
			y = q31(u1, m->e1 + (q31(m->cb, c->sh) << 3)) + q31(u2, m->cb) + q31x2(u3, m->e3, nz, m->e3n);
			m->e1 = q31(m->e1, c->k1);
			m->e3 = q31(m->e3, c->k3);
			m->e3n = q31(m->e3n, c->k3);
			m->tick = q31(m->tick, TOM_KTICK);
		}
		out[i] = q31x2(y << 3, m->g, q31(nz, m->tick) << 3, c->out);   /* Q21 -> Q24, x the accent and the output gain */
	}
	if (v->t > TRIG_N && m->e1 < 0x4000 && m->ca < 0x2000 && m->cb < 0x2000)   /* Q28, the levels in: < -80 dB */
		v->active = 0;
}

/* ---- rim shot / clap ------------------------------------------------------------------------------------- *
 * Rim: three bridged-T resonators (~220 / 480 / 1030 Hz) kicked by the trig's edge, their outputs limited at the
 * op-amps' rails and summed into a diode pair (the square-ish crack), a pure-exponential VCA, then a resonant
 * 229 Hz high-pass.
 *   SHAPE: 0 rim, 1 clap   PITCH: tune   DECAY: the VCA   COLOR: drive (the rings' level)   SWEEP: the top ring
 * Clap: noise through a ~1 kHz band-pass, then (a) the hands: an exponential VCA on a control that jumps at each of
 * four bursts (Mars: 0, 11, 24, 30 ms) and runs down between them, then a high-pass tilt; (b) the reverb: the
 * band-passed noise low-passed at ~400 Hz on an envelope that swells in.
 *   PITCH: tune   DECAY: the reverb's length   COLOR: tone (band width, brightness)   SWEEP: the bursts' spacing
 *   CONTOUR: the bursts' length                                                                                */

COLD static void rc_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	if (trig)
		v->sub = k->shape ? 1 : 0;
	if (v->sub == 0) {
		struct t9_rsv *r = &v->a.rs;
		u32 g = kscale(k->color);
		int j;
		for (j = 0; j < 3; j++) {
			s32 w, sn, cs;
			u32 inc = inc_of(rs_f_hz16[j], k->note);
			if (inc > 357913941u)                          /* 4 kHz at most: the Chamberlin form stays exact */
				inc = 357913941u;
			w = q31((s32)(inc >> 1), 0x6487ed51) << 1;    /* pi f / SR, Q30 */
			sincos30(w, &sn, &cs);
			r->rc[j].g = sn << 2;                          /* f = 2 sin(pi f / SR), Q31 */
			r->rc[j].k = rs_kq[j] << 3;                    /* 1 / Q, Q31 */
			r->g[j] = (s32)ratio(rs_g[j], g);              /* Q24 */
		}
		r->w3 = (s32)ratio(RS_W3, kscale(k->sweep));
		r->kv = kexpo(ratio(RS_TV16, kscale(k->decay)));
		if (trig) {
			for (j = 0; j < 3; j++)
				r->r[j].s1 = r->r[j].s2 = 0;
			r->kick = RS_KICK;
			r->env = ACC(RS_ACC, k->accent);
			r->lp = 0;
			r->hp.x1 = r->hp.x2 = r->hp.y1 = r->hp.y2 = 0;
			v->t = 0;
			v->active = 1;
		}
	} else {
		struct t9_hcv *h = &v->a.hc;
		u32 sp = kscale(k->sweep), ln = kscale(k->contour), dc = kscale(k->decay), tm;
		svf_set(&h->bpc, inc_of(HC_BPF_HZ16, k->note), (s32)ratio(HC_BPK, pow2((16 << 16) + (k->color - 64) * 1365)));
		h->kc = kexpo(ratio(HC_CTL16, ln));
		h->klast = kexpo(ratio(HC_LAST16, dc));
		h->kd = kexpo(ratio(HC_TAIL16, dc));
		tm = ratio(HC_TILTMIX, pow2((16 << 16) + (64 - k->color) * 2048));   /* brighter: less of the low side */
		h->tiltmix = tm > (1u << 30) ? 1 << 30 : (s32)tm;
		if (trig) {
			int j;
			for (j = 0; j < 3; j++)
				h->on[j] = (s32)ratio(hc_on[j], sp);
			h->nb = 0;
			h->ctl = ONE31;
			h->er = h->ed = ONE31;
			h->acc = ACC(HC_ACC, k->accent);
			h->bp.s1 = h->bp.s2 = 0;
			h->hp1 = h->tilt = h->tlp = 0;
			v->t = 0;
			v->active = 1;
		}
	}
}

static void rim_render(struct t9_voice *v, s32 *out)
{
	struct t9_rsv *r = &v->a.rs;
	s32 i, j, x, vv[3];

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		if (v->t >= TRIG_N)
			r->kick = q31(r->kick, RS_KK);
		for (j = 0; j < 3; j++) {                          /* bridged-T, excited on (+): in + band-pass */
			struct t9_svf *f = &r->r[j];               /* Chamberlin: s1 band-pass, s2 low-pass (volts) */
			s32 bp = f->s1;
			f->s2 += q31(bp, r->rc[j].g);
			bp += q31(r->kick - f->s2 - q31(bp, r->rc[j].k), r->rc[j].g);
			bp = bp > RS_RAIL ? RS_RAIL : bp < -RS_RAIL ? -RS_RAIL : bp;
			f->s1 = bp;
			x = r->kick + (q31(bp, r->g[j]) << 7);
			vv[j] = x > RS_RAIL ? RS_RAIL : x < -RS_RAIL ? -RS_RAIL : x;
		}
		x = q31x2(vv[0] + vv[1], RS_W12, vv[2], r->w3) << 1;              /* weights Q30 */
		x = diode(t9_diode_rim, x);
		x = q31(x, r->env) * 40;                           /* x 20 x the VCA (env: Q30) */
		r->env = q31(r->env, r->kv);
		r->lp += q31(x - r->lp, RS_LP);
		out[i] = q31(bq_run(&r->hp, rs_hp, r->lp), RS_OUT);
	}
	if (v->t > TRIG_N && r->env < 0x200000)
		v->active = 0;
}

static void clap_render(struct t9_voice *v, s32 *out)
{
	struct t9_hcv *h = &v->a.hc;
	s32 i, x, b, g, tail, z, zi;
	struct svfo o;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		svf_run(&h->bp, &h->bpc, noise15(&v->rng) << 9, &o);
		x = q31(o.bp, h->bpc.k) << 3;                      /* peak gain 1 */
		if (h->nb < 3 && v->t >= h->on[h->nb]) {
			h->nb++;
			h->ctl = ONE31;
		}
		/* the exponential converter: (e^(D ctl) - 1) / (e^D - 1), from a table over ctl */
		z = h->ctl >> 23;
		zi = (h->ctl >> 8) & 0x7fff;
		g = t9_hc_exp[z] + (((t9_hc_exp[z + 1] - t9_hc_exp[z]) * zi) >> 15);   /* Q15 */
		g = q31(g << 16, h->acc) << 1;                     /* Q31 */
		h->ctl = q31(h->ctl, h->nb == 3 ? h->klast : h->kc);
		h->hp1 += q31(x - h->hp1, HC_BHP);
		b = q31(x - h->hp1, g);
		h->tilt += q31(b - h->tilt, HC_TILT);
		b = b - h->tilt + (q31(h->tilt, h->tiltmix) << 1);
		h->tlp += q31(x - h->tlp, HC_TLP);
		tail = q31(h->tlp, h->ed - h->er);
		h->er = q31(h->er, HC_KR);
		h->ed = q31(h->ed, h->kd);
		out[i] = q31(q31x2(b, HC_BGAIN, tail, HC_TGAIN) << 3, HC_OUT);    /* gains Q28 */
	}
	if (h->nb == 3 && h->ctl < 0x100000 && h->ed < 0x100000)
		v->active = 0;
}
