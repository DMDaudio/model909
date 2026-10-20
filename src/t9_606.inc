/* ---- the TR-606 voices (research/606-circuits.md in 909-port), in the same fixed point as t9_808.inc --------- *
 * Included by t9.c after t9_808.inc. Each voice is the float prototype in proto/t9f.c (bd6_f, sd6_f, tom6_f,
 * hh6_f, cy6_f); t9_cal606.h (proto/gen_cal606.py, from the fits to the 606 From Mars hits) holds the constants.
 * The 606's trig bus: ~1 ms at 4.8 V, 14.2 V accented. The 606 has no tone or decay knobs: here PITCH transposes and
 * DECAY scales the ring and envelope times (64: as the 606).                                                     */

#include "t9_cal606.h"

#define VT6(acc) ((acc) ? VOLT / 10 * 142 : VOLT / 10 * 48)

/* DECAY scales a time constant by kscale(decay) (1/4 .. 4); a damping (1 / tau) by its inverse, kscale(128 - decay) */
#define UNSCALE(x, decay) ((s32)ratio((u32)(x), kscale(128 - (decay))))

/* ---- 606 BD (KICK SHAPE 2) -------------------------------------------------------------------------------- *
 * Two fixed resonators, body ~63 Hz and click ~146 Hz, both rung by the ~1 ms rectangle, which also feeds through
 * (low-passed). No pitch drop.   PITCH: transposes   DECAY: the ring times   PUNCH: accent                       */
COLD static void bd6_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_bd6v *b = &v->a.bd6;
	u32 tr = transpose(k->note);

	svf_set(&b->bc, ratio(BD6_B_INC, tr), UNSCALE(BD6_B_K, k->decay));
	svf_set(&b->cc, ratio(BD6_C_INC, tr), UNSCALE(BD6_C_K, k->decay));
	if (trig) {
		b->vt = VT6(k->accent);
		v->t = 0;
		v->active = 1;
	}
}

static void bd6_render(struct t9_voice *v, s32 *out)
{
	struct t9_bd6v *b = &v->a.bd6;
	struct svfo o;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x = v->t < TRIG8_N ? q31(b->vt, BD6_EG) << 3 : 0, y;

		svf_run(&b->b, &b->bc, x, &o);
		y = q31(o.bp, BD6_WB) << 3;
		svf_run(&b->c, &b->cc, x, &o);
		y += (q31(o.bp, BD6_WC) << 3) + (q31(op_lp8(&b->lp, BD6_LP, x), BD6_WF) << 3);
		out[i] = q31(y, BD6_OUT);
	}
	if (v->t > 48000 / 2)
		v->active = 0;
}

/* ---- 606 SD (SNARE SHAPE 2) ------------------------------------------------------------------------------- *
 * One resonator (~208 Hz) kicked by the differentiated rectangle, its pitch lifted at the start and gliding down;
 * noise on a cap charged to Vtrig - 0.7 with a rounded onset, through the swing VCA, high-passed, mixed in strongly.
 *   PITCH: transposes   DECAY: the ring and the noise   PUNCH: accent (much more noise, as the 606)              */
COLD static void sd6_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_sd6v *s = &v->a.sd6;

	s->f16 = ratio(SD6_F16, transpose(k->note));
	s->n22 = (u32)UNSCALE(SD6_N22, k->decay);
	s->kd = kexpo(ratio(SD6_NTAU16, kscale(k->decay)));
	if (trig) {
		s->vt = VT6(k->accent);
		s->cap = s->vt - VOLT * 7 / 10;
		s->sm = 0;
		s->gl = ONE31;
		v->t = 0;
		v->active = 1;
	}
}

static void sd6_render(struct t9_voice *v, s32 *out)
{
	struct t9_sd6v *s = &v->a.sd6;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 pulse = v->t < TRIG8_N ? s->vt : 0, bp, nz, env, y;
		u32 f16 = s->f16 + (u32)(q31((s32)(s->f16 << 12), q31(SD6_GL, s->gl) << 3) >> 12);

		s->dif = q31(s->dif, SD6_KE) + (pulse - s->prev);
		s->prev = pulse;
		s->gl = q31(s->gl, SD6_KG);
		bp = res_run(&s->r, f16, s->n22, q31(s->dif, SD6_EG) << 3);
		s->sm = ONE31 - q31(ONE31 - s->sm, SD6_KS);
		env = q31(s->cap, s->sm);
		s->cap = q31(s->cap, s->kd);
		nz = bq_run(&s->hp, sd6_hp, noise15(&v->rng) << 9);
		nz -= op_lp8(&s->hp2, SD6_HP2, nz);
		y = (q31(bp, SD6_WT) << 7) + (q31(q31(nz, vca_g(env - SD6_THR)) << 7, SD6_WN) << 7);
		out[i] = q31(op_lp8(&s->lp, SD6_LP, y), SD6_OUT);
	}
	if (v->t > 24000)
		v->active = 0;
}

/* ---- 606 toms (TONE SHAPE 9 low, 10 high) ----------------------------------------------------------------- *
 * One resonator kicked by the differentiated rectangle; the low tom's starts ~1.4x high and glides down (the same at
 * every accent). The shared rumble under it; the other tom's resonator rung faintly (the 606's crosstalk).
 *   PITCH: tuning (semitones)   DECAY: the ring time   PUNCH: accent                                             */
COLD static void tom6_update(struct t9_voice *v, const struct t9_knobs *k, int trig, int part)
{
	struct t9_tom6v *t = &v->a.tom6;
	const struct t9_tom6cal *c = &tom6_cal[part];
	u32 tr = transpose(k->note);

	t->c = c;
	t->f16 = ratio(c->f16, tr);
	t->n22 = (u32)UNSCALE(c->n22, k->decay);
	svf_set(&t->gc, ratio(c->ginc, tr), c->gk);
	if (trig) {
		t->vt = VT6(k->accent);
		t->cap = t->vt - VOLT * 7 / 10;
		t->gl = ONE31;
		v->t = 0;
		v->active = 1;
	}
}

static void tom6_render(struct t9_voice *v, s32 *out)
{
	struct t9_tom6v *t = &v->a.tom6;
	const struct t9_tom6cal *c = t->c;
	struct svfo o;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 pulse = v->t < TRIG8_N ? t->vt : 0, y, nz;
		u32 f16 = t->f16 + (u32)(q31((s32)(t->f16 << 12), q31(c->gl, t->gl) << 3) >> 12);

		t->dif = q31(t->dif, c->ke) + (pulse - t->prev);
		t->prev = pulse;
		t->gl = q31(t->gl, c->kg);
		y = res_run(&t->r, f16, t->n22, q31(t->dif, c->eg) << 3);
		svf_run(&t->g, &t->gc, v->t == 0 ? c->ghost : 0, &o);
		y += o.bp;
		nz = bq_run(&t->pn, tom8_pn, noise15(&v->rng) << 9);
		nz -= op_lp8(&t->nh, c->rhp, nz);
		nz = op_lp8(&t->nl, c->rlp, nz);
		y += q31(q31(nz, vca_g(t->cap - VOLT * 6 / 10)) << 7, c->rum) << 7;
		t->cap = q31(t->cap, c->kr);
		out[i] = q31(op_lp8(&t->lp, c->lp, y), c->out);
	}
	if (v->t > 24000)
		v->active = 0;
}

/* ---- the 606's metal: its own bank (203 / 440 / 627 / 246 / 308 / 367 Hz, late units) and the swing VCA as a
 * clipper: the input driven hard between rails that follow the envelope above the threshold (+e, -asym e).        */
COLD static void bank6_init(struct t9_bank8 *b, u32 *rng, s32 tune30)
{
	int i;

	for (i = 0; i < 6; i++) {
		b->ph[i] = (u32)noise15(rng) << 16;
		b->inc[i] = (u32)q31((s32)(bank6_inc[i] >> 1), tune30) << 2;
	}
}

static inline s32 swing6(s32 x, s32 env, s32 thr, s32 inv_drive, s32 drive8, s32 asym)
{
	s32 e = env - thr, lim, low;

	if (e <= 0)
		return 0;
	lim = q31(e, inv_drive);                               /* e / drive: past it the output sits on a rail */
	low = q31(lim, asym);
	x = x > lim ? lim : x < -low ? -low : x;
	return (x >> 8) * drive8;
}

/* ---- 606 hats (METAL SHAPE 4 closed, 5 open, full kit) ---------------------------------------------------- *
 * The bank through the 7 kHz band-pass, the swing VCA on the hat's envelope (charged to Vtrig - 0.7, so accent sets
 * the length against the threshold), a 2nd-order and a 1st-order high-pass.
 *   DECAY: the envelope's time (64: as the 606)   PITCH: the bank's tuning   PUNCH: accent (much longer, as the 606) */
COLD static void hh6_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_hh6v *h = &v->a.hh6;
	s32 tune = (s32)ratio(HH6_TUNE >> 2, transpose(k->note)) << 2;

	if (!h->bank.inc[0] || h->tune != tune) {
		u32 ph[6];
		int i;
		for (i = 0; i < 6; i++)
			ph[i] = h->bank.ph[i];
		bank6_init(&h->bank, &v->rng, tune);
		if (h->tune)
			for (i = 0; i < 6; i++)
				h->bank.ph[i] = ph[i];
		h->tune = tune;
	}
	if (trig) {
		h->open = v->sub;
		h->e = VT6(k->accent) - VOLT * 7 / 10;
		v->t = 0;
		v->active = 1;
	}
	h->k = kexpo(ratio(h->open ? HH6_OH16 : HH6_CH16, kscale(k->decay)));
}

static void hh6_render(struct t9_voice *v, s32 *out)
{
	struct t9_hh6v *h = &v->a.hh6;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x = bq_run(&h->bp, hh6_bp, bank8_sum(&h->bank)), y;

		y = bq_run(&h->hp, hh6_hp, swing6(x, h->e, h->open ? HH6_THR : HH6_THRC, HH6_INVDRIVE, HH6_DRIVE8, HH6_ASYM));
		y -= op_lp8(&h->h1, HH6_HP1, y);
		h->e = q31(h->e, h->k);
		out[i] = q31(y, HH6_OUT);
	}
	if (h->e < (h->open ? HH6_THR : HH6_THRC) + VOLT / 1000)
		v->active = 0;
}

/* ---- 606 cymbal (CHORD SHAPE 3, full kit) ------------------------------------------------------------------- *
 * The bank through the 3.44 kHz band-pass only. VCA 2 on EG-B (a fast drop, then a slower share) + 1/2 EG-A: its
 * output high-passed at ~8.5 kHz (bright, short); chopped again by VCA 1 on the smoothed EG-A, high-passed and
 * low-passed (dark, long).   DECAY: the envelopes' times   PITCH: the bank   PUNCH: accent (longer, as the 606)   */
COLD static void cy6_update(struct t9_voice *v, const struct t9_knobs *k, int trig)
{
	struct t9_cy6v *c = &v->a.cy6;
	u32 ks = kscale(k->decay);

	if (!c->bank.inc[0])
		bank6_init(&c->bank, &v->rng, (s32)ratio(CY6_TUNE >> 2, transpose(k->note)) << 2);
	c->ka = kexpo(ratio(CY6_TEA16, ks));
	c->kf = kexpo(ratio(CY6_TBF16, ks));
	c->ks = kexpo(ratio(CY6_TBS16, ks));
	if (trig) {
		s32 a = VT6(k->accent) - VOLT * 8 / 10;
		c->ea = a;
		c->ebs = q31(a, CY6_BFL);
		c->ebf = a - c->ebs;
		v->t = 0;
		v->active = 1;
	}
}

static void cy6_render(struct t9_voice *v, s32 *out)
{
	struct t9_cy6v *c = &v->a.cy6;
	int i;

	for (i = 0; i < T9_BLOCK; i++, v->t++) {
		s32 x = bq_run(&c->bp, cy6_bp, bank8_sum(&c->bank)), v2, v1, y;

		v2 = swing6(x, c->ebf + c->ebs + (c->ea >> 1), CY6_THR, CY6_INVDR2, CY6_DR2_8, CY6_ASYM);
		c->es += q31(c->ea - c->es, ONE31 - CY6_KM);
		v1 = swing6(v2, c->es, CY6_THR, CY6_INVDR1, CY6_DR1_8, CY6_ASYM);
		y = (q31(bq_run(&c->hb, cy6_hb, v2), CY6_WB) << 7)
		  + (q31(op_lp8(&c->ld, CY6_LD, bq_run(&c->hd, cy6_hd, v1)), CY6_WD) << 7);
		c->ea = q31(c->ea, c->ka);
		c->ebf = q31(c->ebf, c->kf);
		c->ebs = q31(c->ebs, c->ks);
		out[i] = q31(y, CY6_OUT);
	}
	if (c->ea < CY6_THR && c->ebf + c->ebs < CY6_THR && v->t > 4800)
		v->active = 0;
}
