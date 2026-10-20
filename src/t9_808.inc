/* ---- the TR-808 voices (research/808-circuits.md in 909-port), in the same fixed point as t9_analog.inc ------ *
 * Included by t9.c after t9_analog.inc. Each voice is the float prototype in proto/t9f.c (bd8_f, sd8_f, tom8_f);
 * t9_cal808.h (proto/gen_cal808.py, from the fits to the 808 From Mars hits) holds the fitted constants.
 * The 808's trigger is a 1 ms pulse at 4 V, 14 V accented (the fits give the unaccented level the hits had).
 * Resonators whose pitch moves with the ring (the kick's sigh, the toms' diode lift) recompute their filter
 * every sample: g = pi f / SR (f < 1 kHz: tan's error < 0.01 %), k = 1 / (pi f tau), h = 1 / (1 + g (k + g)).  */

#include "t9_cal808.h"

#define TRIG8_N 48                                         /* the 808's 1 ms trigger */
#define G16 1098                                           /* pi / 48000 x 2^28 / 16: g (Q28) per Hz16 */

/* a resonator retuned this sample: f16 (Hz x 16), n22 = 2^26 / (pi tau) (the ring's time constant) */
static inline s32 res_run(struct t9_svf *r, u32 f16, u32 n22, s32 x)
{
	struct svfc c;
	struct svfo o;

	c.g = (s32)(f16 * G16);
	c.k = (s32)((n22 / f16) << 6);
	c.kg = c.k + c.g;
	c.h = recip28((1 << 28) + (q31(c.g, c.kg) << 3));
	svf_run(r, &c, x, &o);
	return o.bp;
}

/* one-pole low-pass: s += a (x - s), a Q31 */
static inline s32 op_lp8(s32 *s, s32 a, s32 x)
{
	*s += q31(x - *s, a);
	return *s;
}

static inline s32 abs8(s32 x) { return x < 0 ? -x : x; }

/* one-pole coefficient 1 - e^-w, w = 2 pi hz / SR (hz < 7 kHz): its series to w^4, Q31 */
COLD static s32 pole31(u32 hz)
{
	s32 w = (s32)(hz * 281135u), w2 = q31(w, w), w3 = q31(w2, w), w4 = q31(w3, w);

	return w - (w2 >> 1) + w3 / 6 - w4 / 24;
}

/* a ratio 2^((note - 60) / 12), Q16 */
COLD static u32 transpose(s32 note)
{
	return pow2((16 << 16) + (note - (60 << 16)) / 12);
}

/* ---- 808 BD ---------------------------------------------------------------------------------------------- *
 * The trig through a shelf (zero 106 Hz, pole 2.36 kHz) and a diode that clips its negative side rings a
 * bridged-T resonator: ~133 Hz during the attack window, then the settled pitch plus the "sigh" that rises with
 * the ring's amplitude (softplus). Output = drive + gain x band-pass, TONE's low-pass, the 8 Hz coupling cap.
 *   DECAY: the ring time (the 808's DECAY, 18 ms .. 1 s)   COLOR: TONE   PITCH: transposes the drum
 *   PUNCH: accent (the trig at 14 V instead of the unaccented level)                                          */
COLD static void bd8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_bd8v *b = &v->a.bd8;
	int pos = k->decay * 5, i = pos / 127, f = pos - i * 127;
	u32 tau16, tr = transpose(k->note);

	if (i >= 5) {                                         /* DECAY: the measured A..F times, exponential between */
		i = 5;
		f = 0;
	}
	tau16 = f ? kexp(f, (u32)bd8_tau_ms[i] << 4, (u32)bd8_tau_ms[i + 1] << 4) : (u32)bd8_tau_ms[i] << 4;
	b->n22 = (1335088429u / tau16) << 8;                     /* 2^26 / (pi tau), tau = tau16 / 16 ms */
	b->fb16 = ratio(BD8_FB16, tr);
	b->fatt16 = ratio(BD8_FATT16, tr);
	b->sigh = ratio(BD8_SIGH, tr);
	b->klp = pole31(kexp(k->color, BD8_TF_LO, BD8_TF_HI));
	if (trig) {
		b->vt = k->accent ? 14 * VOLT : BD8_VT;
		v->t = 0;
		v->active = 1;
	}
}

static void bd8_render(struct t9_voice *v, s32 *out)
{
	struct t9_bd8v *b = &v->a.bd8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x = v->t < TRIG8_N ? b->vt : 0, y, bp, z, sp, idx;
		u32 f16;

		y = (q31(bd8_shelf[0], x) + q31(bd8_shelf[1], b->sx1) - q31(bd8_shelf[2], b->sy1)) << 1;
		b->sx1 = x;
		b->sy1 = y;
		if (y < -BD8_CLIP)
			y = -BD8_CLIP;
		x = q31(y, BD8_EG) << 3;
		if (v->t < BD8_WIN) {
			f16 = b->fatt16;
		} else {                                          /* the sigh: fb + (fb sc / sa) softplus(sa (A - sk)) */
			z = q31(b->A - BD8_SK, BD8_SA) << 3;
			idx = (z >> 21) + 64;
			if (idx < 0)
				sp = 0;
			else if (idx >= 128)
				sp = (z >> 12) + 0;                       /* softplus(z) = z above 8 V (Q12) */
			else
				sp = t9_softplus[idx] + (((t9_softplus[idx + 1] - t9_softplus[idx]) * ((z >> 5) & 0xffff)) >> 16);
			f16 = b->fb16 + ((u32)b->sigh * (u32)sp >> 20);
		}
		bp = res_run(&b->r, f16, b->n22, x);
		b->A = abs8(bp) > q31(b->A, BD8_KA) ? abs8(bp) : q31(b->A, BD8_KA);
		y = x + (q31(bp, BD8_BPG) << 3);
		y = op_lp8(&b->lp, b->klp, y);
		op_lp8(&b->hp, BD8_COUP, y);
		out[i] = q31(y - b->hp, BD8_OUT);
	}
	if (v->t > BD8_WIN && b->A < VOLT / 4096 && abs8(b->lp) < VOLT / 4096)
		v->active = 0;
}

/* ---- 808 SD ---------------------------------------------------------------------------------------------- *
 * Two fixed resonators (~175 / 349 Hz): the low one kicked by the trig's edges, the high one by the pulse; TONE
 * crossfades low -> 0.24 low + 0.52 high. Snappy: a cap charged to SNAPPY x (Vtrig - 0.6), a rounded onset,
 * decaying into the VCA's threshold, on white noise -> HP 1.1 kHz -> HP 530 Hz. The mix's low-pass.
 *   COLOR: SNAPPY   SWEEP: TONE   PITCH: transposes the drum   PUNCH: accent                                  */
COLD static void sd8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_sd8v *s = &v->a.sd8;
	u32 tr = transpose(k->note);

	svf_set(&s->lc, ratio(SD8_LO_INC, tr), SD8_LO_K);
	svf_set(&s->hc, ratio(SD8_HI_INC, tr), SD8_HI_K);
	s->tone = (s32)((u32)k->sweep * (ONE31 / 127));
	s->snap = k->color;
	if (trig) {
		s32 vt = k->accent ? 14 * VOLT : SD8_VT;
		s->vt = vt;
		s->cap = (vt - VOLT * 6 / 10) / 127 * s->snap;
		s->sm = 0;
		v->t = 0;
		v->active = 1;
	}
}

static void sd8_render(struct t9_voice *v, s32 *out)
{
	struct t9_sd8v *s = &v->a.sd8;
	struct svfo o;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 pulse = v->t < TRIG8_N ? s->vt : 0, edge = pulse - s->prev, l, h, drum, nz, env, y;

		s->prev = pulse;
		svf_run(&s->lo, &s->lc, (q31(edge, SD8_EG) << 3) + (q31(pulse, SD8_PLO) << 3), &o);
		l = o.bp;
		svf_run(&s->hi, &s->hc, q31(pulse, SD8_PHI) << 3, &o);
		h = o.bp;
		drum = l + q31(q31(l, 515396076) + q31(h, 1116691497) - l, s->tone);   /* 0.24, 0.52 */
		s->sm = ONE31 - q31(ONE31 - s->sm, SD8_KS);
		env = q31(s->cap, s->sm);
		s->cap = q31(s->cap, SD8_KD);
		nz = bq_run(&s->hpb, sd8_hp1, noise15(&v->rng) << 9);
		nz -= op_lp8(&s->hp2, SD8_HP2, nz);
		y = (q31(drum, SD8_DG) << 7) + (q31(q31(nz, vca_g(env - VOLT * 6 / 10)) << 7, SD8_SW) << 7);
		y = op_lp8(&s->lp, SD8_LP, y);
		out[i] = q31(y, SD8_OUT);
	}
	if (v->t > 48000)
		v->active = 0;
}

/* ---- 808 toms and congas --------------------------------------------------------------------------------- *
 * One bridged-T resonator with regeneration (time constant fixed by the parts), kicked by the differentiated
 * trig (its negative spike clamped); anti-parallel diodes lift the pitch while the ring is large. Toms add a low
 * rumble: pink noise through a VCA on a cap. The mix amp's pole.
 *   SHAPE: low / mid / high tom, low / mid / high conga   PITCH: tuning (semitones)   PUNCH: accent          */
COLD static void tom8_update(struct t9_voice *v, const struct t9_knobs *k, int trig, int part)
{
	struct t9_tom8v *t = &v->a.tom8;

	t->c = &tom8_cal[part];
	/* f at note 60: the settled pitch at mid tuning, f0 x span^(1/2) */
	t->f16 = ratio(ratio(t->c->f0_16, pow2((16 << 16) + (lg(t->c->span16) - (16 << 16)) / 2)), transpose(k->note));
	if (trig) {
		t->vt = k->accent ? 14 * VOLT : 4 * VOLT;
		t->cap = t->vt - VOLT * 6 / 10;
		v->t = 0;
		v->active = 1;
	}
}

static void tom8_render(struct t9_voice *v, s32 *out)
{
	struct t9_tom8v *t = &v->a.tom8;
	const struct t9_tom8cal *c = t->c;
	u32 n22 = (u32)c->tau_k << 10;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 pulse = v->t < TRIG8_N ? t->vt : 0, x, bp, nz, y, u, idx, lift;
		u32 f16;

		t->dif = q31(t->dif, c->ke) + (pulse - t->prev);
		t->prev = pulse;
		x = t->dif < -VOLT * 6 / 10 ? -VOLT * 6 / 10 : t->dif;
		u = t->A - c->knee;                               /* the diodes: f0 (1 + (lift - 1) (1 - e^-(A - knee) slope)) */
		if (u <= 0) {
			lift = 0;
		} else {
			u = q31(u, c->slope) << 3;                    /* Q24 */
			idx = u >> 20;                                /* steps of 1/16 */
			lift = idx >= 128 ? 32767 : t9_oneminusexp[idx] + (((t9_oneminusexp[idx + 1] - t9_oneminusexp[idx]) * ((u >> 4) & 0xffff)) >> 16);
		}
		f16 = t->f16 + (u32)(q31((s32)(t->f16 << 12), q31(c->lift, lift << 16) << 3) >> 12);
		bp = res_run(&t->r, f16, n22, q31(x, c->eg) << 3);
		t->A = abs8(bp) > q31(t->A, 2136283035) ? abs8(bp) : q31(t->A, 2136283035);   /* 4 ms */
		nz = bq_run(&t->pn, tom8_pn, noise15(&v->rng) << 9);
		nz -= op_lp8(&t->nhp, c->rhp, nz);
		nz = op_lp8(&t->nlp, c->rlp, nz);
		y = (q31(bp, c->bpg) << 3) + (q31(q31(nz, vca_g(t->cap - VOLT * 6 / 10)) << 7, c->rum) << 7);
		t->cap = q31(t->cap, c->kr);
		y = op_lp8(&t->olp, c->olp, y);
		out[i] = q31(y, c->out);
	}
	if (v->t > 24000 && t->A < VOLT / 4096)
		v->active = 0;
}

/* ---- the metal bank: six free-running squares (205.3 / 369.6 / 304.4 / 522.7 / 800 / 540 Hz, 47.98 % duty), never
 * reset by a trig; each voice runs its own, from random phases. A square is +-1 (Q24).                          */
COLD static void bank8_init(struct t9_bank8 *b, u32 *rng, s32 tune30)
{
	int i;

	for (i = 0; i < 6; i++) {
		b->ph[i] = (u32)noise15(rng) << 16;
		b->inc[i] = (u32)q31((s32)(bank8_inc[i] >> 1), tune30) << 2;
	}
}

static inline s32 sq8(struct t9_bank8 *b, int i)
{
	b->ph[i] += b->inc[i];
	return b->ph[i] < BANK8_DUTY ? VOLT : -VOLT;
}

static inline s32 bank8_sum(struct t9_bank8 *b)
{
	return (sq8(b, 0) + sq8(b, 1) + sq8(b, 2) + sq8(b, 3) + sq8(b, 4) + sq8(b, 5)) / 6;
}

/* the swing VCA: its negative rail lower (asym, Q31), then the envelope's gain (volts above the threshold) */
static inline s32 swing(s32 x, s32 asym, s32 g)
{
	return q31(x > 0 ? x : q31(x, asym), g) << 7;
}

/* a first-order shelf (b0 b1 a1, Q30) */
static inline s32 shelf_run(s32 *x1, s32 *y1, const s32 *c, s32 x)
{
	s32 y = (q31(c[0], x) + q31(c[1], *x1) - q31(c[2], *y1)) << 1;

	*x1 = x;
	*y1 = y;
	return y;
}

#define VT8(acc) ((acc) ? 14 * VOLT : 7 * VOLT)            /* the metal voices' trig: 7 + 0.7 (Vtrig - 4) */

/* ---- 808 cowbell (PERC SHAPE 6) -------------------------------------------------------------------------- *
 * Oscillators 5 and 6 through the swing VCA on a two-cap envelope (fast + slow), then the band-pass, the output
 * pole.   PITCH: transposes the two oscillators   PUNCH: accent                                              */
COLD static void cb8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_cb8v *c = &v->a.cb8;
	u32 tr = transpose(k->note);

	if (!c->bank.inc[0])
		bank8_init(&c->bank, &v->rng, 1 << 30);
	c->bank.inc[4] = ratio(CB8_INC5, tr);
	c->bank.inc[5] = ratio(CB8_INC6, tr);
	if (trig) {
		s32 a = VT8(k->accent) - VOLT * 6 / 10;
		c->es = q31(a, CB8_SHARE);
		c->ef = a - c->es;
		v->t = 0;
		v->active = 1;
	}
}

static void cb8_render(struct t9_voice *v, s32 *out)
{
	struct t9_cb8v *c = &v->a.cb8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x, y;

		sq8(&c->bank, 0); sq8(&c->bank, 1); sq8(&c->bank, 2); sq8(&c->bank, 3);
		x = sq8(&c->bank, 4) + (q31(sq8(&c->bank, 5), CB8_W6) << 1);
		y = swing(x, CB8_ASYM, vca_g(c->ef + c->es - CB8_THR));
		y = bq_run(&c->bp2, cb8_bp, bq_run(&c->bp1, cb8_bp, y));
		c->ef = q31(c->ef, CB8_KF);
		c->es = q31(c->es, CB8_KS);
		out[i] = q31(op_lp8(&c->lp, CB8_LP, y), CB8_OUT);
	}
	if (c->ef + c->es < CB8_THR - KNEE && v->t > 4800)
		v->active = 0;
}

/* ---- 808 hats (METAL SHAPE 2 closed, 3 open) ------------------------------------------------------------- *
 * The bank's sum through the 7 kHz band-pass, the swing VCA on the hat's envelope, the high-pass (CH / OH) and
 * the output stage's shelf. CH: one decay. OH: a slow leak, and after DECAY's delay (41 ms .. 0.86 s) a dump.
 *   DECAY: the open hat's length   PITCH: the bank's tuning (semitones)   PUNCH: accent                      */
COLD static void hh8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_hh8v *h = &v->a.hh8;
	s32 tune = (s32)ratio(HH8_TUNE >> 2, transpose(k->note)) << 2;

	if (!h->bank.inc[0] || h->tune != tune) {
		u32 ph[6];
		int i;
		for (i = 0; i < 6; i++)
			ph[i] = h->bank.ph[i];
		bank8_init(&h->bank, &v->rng, tune);
		if (h->tune)
			for (i = 0; i < 6; i++)
				h->bank.ph[i] = ph[i];
		h->tune = tune;
	}
	h->delay = kexp(k->decay, 41 * 48, 860 * 48);
	if (trig) {
		h->open = v->sub;
		h->e = VT8(k->accent) - VOLT * 7 / 10;
		v->t = 0;
		v->active = 1;
	}
}

static void hh8_render(struct t9_voice *v, s32 *out)
{
	struct t9_hh8v *h = &v->a.hh8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x = bq_run(&h->bp, hh8_bp, bank8_sum(&h->bank)), y;

		y = swing(x, HH8_ASYM, vca_g(h->e - HH8_THR));
		y = bq_run(&h->hp, h->open ? hh8_hpo : hh8_hpc, y);
		h->e = q31(h->e, h->open ? ((u32)v->t < h->delay ? HH8_KL : HH8_KD) : HH8_KC);
		out[i] = q31(shelf_run(&h->sx1, &h->sy1, hh8_shelf, y), HH8_OUT);
	}
	if (h->e < HH8_THR - KNEE)
		v->active = 0;
}

/* ---- 808 cymbal (CHORD SHAPE 2) -------------------------------------------------------------------------- *
 * The bank's sum into two band-passes (~3.9 / 6.8 kHz). Low band: a VCA on a swelling envelope, HP 2.2 kHz.
 * High band: a VCA on a fast drop plus a tail, HP 8.8 kHz. TONE crossfades high <-> low; the output shelf.
 *   DECAY: the low band's decay (0.3 .. 2.5 s)   COLOR: TONE   PITCH: the bank's tuning   PUNCH: accent      */
COLD static void cy8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_cy8v *c = &v->a.cy8;

	if (!c->bank.inc[0])
		bank8_init(&c->bank, &v->rng, (s32)ratio(CY8_TUNE >> 2, transpose(k->note)) << 2);
	c->kd = kexpo(kexp(k->decay, 300 * 48 * 16, 2500 * 48 * 16));
	c->tone = (s32)((u32)k->color * (ONE31 / 127));
	if (trig) {
		s32 a = VT8(k->accent) - VOLT * 73 / 100;
		c->lr = a;
		c->ht = q31(a, CY8_HSHARE);
		c->hf = a - c->ht;
		v->t = 0;
		v->active = 1;
	}
}

static void cy8_render(struct t9_voice *v, s32 *out)
{
	struct t9_cy8v *c = &v->a.cy8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 s = bank8_sum(&c->bank), lo = bq_run(&c->bp1, cy8_bp1, s), hi = bq_run(&c->bp2, cy8_bp2, s), y;

		c->el += q31(c->lr - c->el, ONE31 - CY8_KR);       /* the swell: a rise toward a decaying target */
		c->lr = q31(c->lr, c->kd);
		hi = bq_run(&c->hp2, cy8_hp2, swing(hi, CY8_ASYM, vca_g(c->hf + c->ht - CY8_THR)));
		lo = bq_run(&c->hp1, cy8_hp1, swing(lo, CY8_ASYM, vca_g(c->el - CY8_THR)));
		y = (q31(hi, CY8_GHI) << 7) + q31((q31(lo, CY8_GLO) << 7) - (q31(hi, CY8_GHI) << 7), c->tone);
		c->hf = q31(c->hf, CY8_KH);
		c->ht = q31(c->ht, CY8_KT);
		out[i] = q31(shelf_run(&c->sx1, &c->sy1, cy8_shelf, y), CY8_OUT);
	}
	if (c->el < CY8_THR - KNEE && c->hf + c->ht < CY8_THR - KNEE && v->t > 4800)
		v->active = 0;
}

/* ---- 808 hand clap (PERC SHAPE 3) ------------------------------------------------------------------------ *
 * White noise through the band-pass; bursts (an exponential gain slammed back at each onset) high-passed; a
 * reverb path on a rise-then-decay envelope, band-passed around 1 kHz.   PUNCH: accent                      */
COLD static void cp8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_cp8v *c = &v->a.cp8;

	if (trig) {
		c->vt = k->accent ? 14 * VOLT : 4 * VOLT;
		c->r1 = c->vt;
		c->r2 = 0;
		v->t = 0;
		v->active = 1;
	}
}

static void cp8_render(struct t9_voice *v, s32 *out)
{
	struct t9_cp8v *c = &v->a.cp8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 nz = bq_run(&c->bp, cp8_bp, noise15(&v->rng) << 9), b, r, y;

		if (v->t == 0 || v->t == CP8_O2 || v->t == CP8_O3 || v->t == CP8_O4)
			c->eb = c->vt;
		c->r2 += q31(c->r1 - c->r2, ONE31 - CP8_KR1);
		c->r1 = q31(c->r1, CP8_KR2);
		b = q31(nz, c->eb / 14) << 7;
		b -= op_lp8(&c->bhp, CP8_BHP, b);
		r = bq_run(&c->rb, cp8_rb, q31(nz, vca_g(c->r2 - CP8_RTHR) / 14) << 7);
		y = (q31(b, CP8_BG) << 7) + (q31(r, CP8_RG) << 7);
		c->eb = q31(c->eb, CP8_KB);
		out[i] = q31(op_lp8(&c->lp, CP8_LP, y), CP8_OUT);
	}
	if (v->t > 24000)
		v->active = 0;
}

/* ---- 808 rim shot (PERC SHAPE 2) and claves (PERC SHAPE 4) ----------------------------------------------- *
 * RS: two resonators (~430 Hz, ~1.7 kHz) kicked by the trig and clipped at the rails, mixed into a half-wave
 * switch on a short envelope, then the high-pass. CL: one ~2.6 kHz resonator, high Q, kicked by a spike. Both:
 * the output gate, then a fade.   PITCH: transposes the resonators   PUNCH: accent                           */
COLD static void rs8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_rs8v *r = &v->a.rs8;
	u32 tr = transpose(k->note);

	if (v->sub == 4) {
		svf_set(&r->hc, ratio(CL8_INC, tr), CL8_K);
	} else {
		svf_set(&r->lc, ratio(RS8_LO_INC, tr), RS8_LO_K);
		svf_set(&r->hc, ratio(RS8_HI_INC, tr), RS8_HI_K);
	}
	if (trig) {
		r->vt = k->accent ? 14 * VOLT : v->sub == 4 ? CL8_VT : RS8_VT;
		r->g = r->vt;
		r->fade = ONE31;
		v->t = 0;
		v->active = 1;
	}
}

static void rs8_render(struct t9_voice *v, s32 *out)
{
	struct t9_rs8v *r = &v->a.rs8;
	struct svfo o;
	int i, cl = v->sub == 4;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 y;

		if (cl) {
			svf_run(&r->hi, &r->hc, v->t == 0 ? q31(r->vt, CL8_KICK) << 3 : 0, &o);
			y = q31(o.bp, CL8_OUT);
			if (v->t > CL8_GATE)
				r->fade = q31(r->fade, CL8_KFADE);
		} else {
			s32 kick = v->t < TRIG8_N ? q31(r->vt, RS8_KICK) << 3 : 0, l, h, d;
			svf_run(&r->lo, &r->lc, kick, &o);
			l = o.bp > RS8_RAIL ? RS8_RAIL : o.bp < -RS8_RAIL ? -RS8_RAIL : o.bp;
			svf_run(&r->hi, &r->hc, kick, &o);
			h = o.bp > RS8_RAIL ? RS8_RAIL : o.bp < -RS8_RAIL ? -RS8_RAIL : o.bp;
			d = (q31(h, RS8_WH) << 1) + (q31(l, RS8_WL) << 1);
			d = d > 0 ? q31(d, vca_g(r->g - VOLT * 6 / 10) / 14) << 7 : 0;
			r->g = q31(r->g, RS8_KG);
			y = q31(bq_run(&r->hp, rs8_hp, d), RS8_OUT);
			if (v->t > RS8_GATE)
				r->fade = q31(r->fade, RS8_KFADE);
		}
		out[i] = q31(y, r->fade);
	}
	if (r->fade < (ONE31 >> 12))
		v->active = 0;
}

/* ---- 808 maracas (PERC SHAPE 5) -------------------------------------------------------------------------- *
 * White noise through a VCA that ramps in while the gate is open and releases after; HP, the 10 kHz HP, LP.
 *   DECAY: the gate (64: as the 808)   PUNCH: accent (a longer gate, as the 808's)                          */
COLD static void ma8_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_ma8v *m = &v->a.ma8;

	if (trig) {
		m->gate = (s32)ratio(MA8_GATE + (k->accent ? MA8_GACC : 0), kscale(k->decay));
		m->a = 0;
		v->t = 0;
		v->active = 1;
	}
}

static void ma8_render(struct t9_voice *v, s32 *out)
{
	struct t9_ma8v *m = &v->a.ma8;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x;

		m->a = v->t < m->gate ? ONE31 - q31(ONE31 - m->a, MA8_KR) : q31(m->a, MA8_KL);
		x = q31(noise15(&v->rng) << 9, m->a);
		x -= op_lp8(&m->hp1, MA8_HP1, x);
		x = bq_run(&m->hp2, ma8_hp2, x);
		out[i] = q31(op_lp8(&m->lp, MA8_LP, x), MA8_OUT);
	}
	if (v->t > m->gate && m->a < (ONE31 >> 14))
		v->active = 0;
}
