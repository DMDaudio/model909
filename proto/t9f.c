/*
 * Float prototypes of the TR-909 analog voices, built from the circuit (research/909-circuits.md), for fitting
 * against recordings on the Mac before the fixed-point port. Each voice: void xx_f(const double *p, int n, float
 * *out); p's layout is in proto/voices.py (P_* names there match the comments here). 48 kHz. Not for the firmware.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>

#define FS 48000.0
#define PI 3.14159265358979323846
#define TRIG 0.002                               /* the 2 ms trigger pulse */

/* ---- shared parts ------------------------------------------------------------------------------------------ */

static uint32_t rng;
void noise_seed(uint32_t s) { rng = s; }
static double noise(void)                        /* white, -1..1 */
{
	rng = rng * 1664525u + 1013904223u;
	return (double)(int32_t)rng / 2147483648.0;
}

static double rc(double tau)                     /* per-sample factor of an RC decay */
{
	return tau > 0 ? exp(-1.0 / (tau * FS)) : 0.0;
}

/* the VCA transistor: gain ∝ base current: V - Vth above a 20 mV knee, 0 below it, a parabola between (as the
 * firmware: src/t9_analog.inc vca_g) */
static double vca(double v, double vth)
{
	double d = v - vth, k = 0.02;
	return d >= k ? d : d <= -k ? 0 : (d + k) * (d + k) / (4 * k);
}

/* anti-parallel silicon diode pair fed from x through rs, loaded by rl: (x-u)/rs = u/rl + 2 Is sinh(u/nVt) */
static double diode_pair(double x, double rs, double rl)
{
	const double is = 2.5e-9, nvt = 1.9 * 0.02585;
	double u = x * rl / (rs + rl);
	int k;
	if (u > 0.75) u = 0.75;
	if (u < -0.75) u = -0.75;
	for (k = 0; k < 20; k++) {
		double s = sinh(u / nvt), c = cosh(u / nvt);
		double f = (x - u) / rs - u / rl - 2 * is * s;
		double df = -1 / rs - 1 / rl - 2 * is * c / nvt;
		double du = f / df;
		if (du > 0.05) du = 0.05;
		if (du < -0.05) du = -0.05;
		u -= du;
		if (fabs(du) < 1e-10)
			break;
	}
	return u;
}

/* biquads (RBJ), direct form I */
struct bq { double b0, b1, b2, a1, a2, x1, x2, y1, y2; };
static void bq_lp(struct bq *f, double f0, double q)
{
	double w = 2 * PI * f0 / FS, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
	memset(f, 0, sizeof *f);
	f->b0 = (1 - c) / 2 / a0; f->b1 = (1 - c) / a0; f->b2 = f->b0; f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0;
}
static void bq_hp(struct bq *f, double f0, double q)
{
	double w = 2 * PI * f0 / FS, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
	memset(f, 0, sizeof *f);
	f->b0 = (1 + c) / 2 / a0; f->b1 = -(1 + c) / a0; f->b2 = f->b0; f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0;
}
static void bq_bp(struct bq *f, double f0, double q)          /* peak gain 1 */
{
	double w = 2 * PI * f0 / FS, al = sin(w) / (2 * q), c = cos(w), a0 = 1 + al;
	memset(f, 0, sizeof *f);
	f->b0 = al / a0; f->b1 = 0; f->b2 = -al / a0; f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0;
}
static double bq(struct bq *f, double x)
{
	double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
	f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
	return y;
}
struct op { double a, s; };                                   /* one-pole */
static void op_set(struct op *f, double fc) { f->a = 1 - exp(-2 * PI * fc / FS); f->s = 0; }
static double op_lp(struct op *f, double x) { f->s += f->a * (x - f->s); return f->s; }
static double op_hp(struct op *f, double x) { f->s += f->a * (x - f->s); return x - f->s; }

/* TPT state-variable filter (band-pass out), with the integrator states clamped at the op-amp rails */
struct svf { double g, k, s1, s2, rail; };
static void svf_set(struct svf *f, double f0, double q, double rail)
{
	f->g = tan(PI * f0 / FS); f->k = 1 / q; f->s1 = f->s2 = 0; f->rail = rail;
}
static double svf_bp(struct svf *f, double x)
{
	double hp = (x - (f->k + f->g) * f->s1 - f->s2) / (1 + f->g * (f->k + f->g));
	double bp = f->g * hp + f->s1, lp;
	f->s1 = f->g * hp + bp;
	lp = f->g * bp + f->s2;
	f->s2 = f->g * bp + lp;
	if (f->rail > 0) {
		if (f->s1 > f->rail) f->s1 = f->rail;
		if (f->s1 < -f->rail) f->s1 = -f->rail;
	}
	return bp;
}

/* Chamberlin state-variable filter (band-pass out), its band-pass state clamped at the op-amp rails: cheaper than
 * the TPT form, and the same for these low frequencies (as the firmware's rim) */
struct cham { double f, q, lp, bp, rail; };
static void cham_set(struct cham *c, double f0, double q, double rail)
{
	c->f = 2 * sin(PI * f0 / FS); c->q = 1 / q; c->lp = c->bp = 0; c->rail = rail;
}
static double cham_bp(struct cham *c, double x)
{
	double hp;
	c->lp += c->f * c->bp;
	hp = x - c->lp - c->q * c->bp;
	c->bp += c->f * hp;
	if (c->rail > 0) {
		if (c->bp > c->rail) c->bp = c->rail;
		if (c->bp < -c->rail) c->bp = -c->rail;
	}
	return c->bp;
}

/* triangle VCO: -1..1, starts at 0 rising. Half a period up takes 1/(2 f_up), down 1/(2 f_down). */
struct tri { double v, dir; };
static double tri_step(struct tri *t, double f_up, double f_down)
{
	t->v += t->dir * 4 * (t->dir > 0 ? f_up : f_down) / FS;
	if (t->v > 1) { t->v = 2 - t->v; t->dir = -1; }
	if (t->v < -1) { t->v = -2 - t->v; t->dir = 1; }
	return t->v;
}

/* ---- BD -------------------------------------------------------------------------------------------------- *
 * p: 0 tune (pitch τ, s)  1 decay (0..1)  2 attack (0..1)  3 accent (0..1)  4 fb Hz  5 df Hz  6 tri peak V
 *    7 h2  8 click gain  9 noise gain  10 C8 scale  11 hold s  12 body gain  13 thr V  14 vstart V             */
void bd_f(const double *p, int n, float *out)
{
	double kpitch = rc(p[0]), decay = p[1], attack = p[2], acc = p[3], fb = p[4], df = p[5], tp = p[6];
	double rd = 47e3 + 1e6 * (pow(10, 2 * decay) - 1) / 99.0;          /* DECAY: 1 MΩ audio taper */
	double c8 = 0.33e-6 * p[10], thr = p[13];
	double v1 = fmin(p[14] * (0.55 + 0.45 * acc), 2.63);               /* the 2.6 V clamp */
	struct tri t = { 0, 1 };
	struct bq sk; struct op dif, nhp, nlp, coup;
	double vp = 1, env2 = 1, kenv2 = rc(0.0007);
	int i;
	bq_lp(&sk, 3720, 3.1);
	op_set(&dif, 1 / (2 * PI * 3.3e-9 * 100e3));
	op_set(&nhp, 26); op_set(&nlp, 513); op_set(&coup, 10);
	for (i = 0; i < n; i++) {
		double ts = i / FS, u = 0, y, pulse, click, nz;
		if (ts >= TRIG) {                                        /* the VCO is held in reset during the trigger */
			double f = fb + df * vp;
			vp *= kpitch;
			u = diode_pair(tp * tri_step(&t, f, f), 10e3, 2.25e3);
			u += p[7] * u * u;
		}
		if (ts > p[11])                                          /* ENV1: hold, then to ground and the base */
			v1 -= (v1 / (rd * c8) + fmax(0, v1 - thr) / (470e3 * c8)) / FS;
		y = p[12] * u * vca(v1, thr);
		pulse = ts < TRIG ? 0.6 + 0.4 * acc : 0;
		click = bq(&sk, op_hp(&dif, pulse));
		nz = op_lp(&nlp, op_hp(&nhp, noise()));
		if (ts >= TRIG)
			env2 *= kenv2;
		y += (p[8] * click + p[9] * nz) * env2 * (0.42 * attack + 0.18 * (1 - attack)) / 0.42;
		out[i] = (float)op_hp(&coup, y);
	}
}

/* ---- SD -------------------------------------------------------------------------------------------------- *
 * p: 0 tune (0..1)  1 tone (0..1)  2 snappy (0..1)  3 accent (0..1)  4 f1 at tune 0  5 bend Hz  6 bend τ
 *    7 τ VCO1  8 τ VCO2  9 VCO2 level  10 ENV4 start V  11 ENV4 hold s  12 ENV4 end V  13 noise LP f
 *    14 noise LP Q  15 snappy gain  16 burst gain  17 tri peak V  18 VCO2 ratio  19 thr V  20 drum gain
 *    21 drum-sum drive                                                                                       */
void sd_f(const double *p, int n, float *out)
{
	double f1s = p[4] * (1 + p[0]), kb = rc(p[6]), k1 = rc(p[7]), k2 = rc(p[8]), ks = rc(0.001);
	double a = 0.55 + 0.45 * p[3], thr = p[19];
	double c67 = 0.47e-6, rdec = 100e3 + 500e3 * p[1];
	double v4 = p[10] * a, vend = p[12] * a;
	struct tri t1 = { 0, 1 }, t2 = { 0, 1 };
	struct op nhp, hpa, hpo;
	struct bq lp, hpb;
	double b = 1, e1 = 1, e2 = 1, sm = 0, e5 = 1, k5 = rc(0.0001);
	int i;
	op_set(&nhp, 48); op_set(&hpa, 1520); op_set(&hpo, 7.2);
	bq_lp(&lp, p[13], p[14]); bq_hp(&hpb, 4050, 1.31);
	for (i = 0; i < n; i++) {
		double ts = i / FS, drum = 0, x, sa, sb;
		if (ts >= TRIG) {
			double f1 = f1s + p[5] * b, f2 = p[18] * f1;
			double u1 = diode_pair(p[17] * tri_step(&t1, f1, f1), 10e3, 2.2e3);
			double u2 = diode_pair(p[17] * tri_step(&t2, f2, f2), 10e3, 2.2e3);
			b *= kb;
			sm = 1 - (1 - sm) * ks;                              /* ~1 ms attack smoothing on both VCAs */
			drum = sm * (u1 * e1 + p[9] * u2 * e2);
			/* the two VCAs' currents meet in one summing stage that clips softly: the 2 f1 - f2 line (the
			 * accent after it, as the firmware) */
			drum = a * diode_pair(p[21] * drum, 10e3, 2.2e3) / (p[21] * 2.2 / 12.2);
			e1 *= k1; e2 *= k2;
		}
		x = bq(&lp, op_hp(&nhp, 0.25 * noise()));
		/* ENV4: flat while the hold keeps the accent switch on, then toward vend and into the base */
		if (ts > p[11])
			v4 -= ((v4 - vend) / (rdec * c67) + fmax(0, v4 - thr) / (220e3 * c67)) / FS;
		sa = op_hp(&hpa, x) * vca(v4, thr);
		if (ts >= TRIG)
			e5 *= k5;
		sb = bq(&hpb, x) * a * e5;
		out[i] = (float)(p[20] * drum + op_hp(&hpo, p[2] * (p[15] * sa + p[16] * sb)));
	}
}

/* ---- Toms ------------------------------------------------------------------------------------------------ *
 * p: 0 tune (0..1)  1 decay (0..1)  2 accent (0..1)  3 f1 at tune 0  4 fast V  5 fast τ  6 slow V  7 slow τ
 *    8 VCO2 ratio  9 VCO3 ratio  10 overdrive τ  11 overdrive gain  12 τ ENV1  13 ENV2 rise τ  14 ENV2 τ min
 *    15 ENV2 τ max  16 τ ENV3  17 VCO2 level  18 VCO3 level  19 noise level  20 tick level  21 tri peak V
 *    22 ENV2 share into VCO1  23 noise BP f  24 noise BP Q  25 VCO1 level                                   */
void tom_f(const double *p, int n, float *out)
{
	double vdd = 1.705 * (1 + p[0]), f1s = p[3] * (1 + p[0]), a = 0.45 + 0.55 * p[2];
	double kf = rc(p[5]), ks = rc(p[7]), kod = rc(p[10]), k1 = rc(p[12]), k3 = rc(p[16]), kt = rc(0.00047);
	double tda = p[14] * pow(p[15] / p[14], p[1]);      /* C_A's decay through DECAY: τ across the pot */
	double ca = 1, cb = 0, kr = 1 / (p[13] * FS), kd = 1 / (tda * FS);
	struct tri t1 = { 0, 1 }, t2 = { 0, 1 }, t3 = { 0, 1 };
	struct bq nbp;
	double vf = p[4], vs = p[6], od = 1, e1 = 1, e3 = 1, tick = 1, g1 = 1;
	int i;
	bq_bp(&nbp, p[23], p[24]);
	for (i = 0; i < n; i++) {
		double ts = i / FS, y = 0, nz = bq(&nbp, noise());
		if (ts >= TRIG) {
			double vss = vdd + vf + vs, r = vss / vdd, u1, u2, u3;
			if (i % 32 == 0) {                   /* VCO1's overdrive: 10k || 1k, as a gain (once a block) */
				double drive = 1 + (p[11] - 1) * od;
				g1 = 12.2e3 / (10e3 / drive + 2.2e3);
			}
			u1 = diode_pair(g1 * p[21] * tri_step(&t1, f1s, f1s * r), 10e3, 2.2e3);
			u2 = diode_pair(p[21] * tri_step(&t2, p[8] * f1s, p[8] * f1s * r), 10e3, 2.2e3);
			u3 = diode_pair(p[21] * tri_step(&t3, p[9] * f1s, p[9] * f1s * r), 10e3, 2.2e3);
			vf *= kf; vs *= ks; od *= kod;
			/* ENV2: C_A (accent, then DECAY) fills C_ENV2 through 100k: swells in, then follows C_A */
			cb += (ca - cb) * kr;
			ca -= ca * kd;
			y = p[25] * u1 * (e1 + p[22] * cb) + p[17] * u2 * cb + (p[18] * u3 + p[19] * nz) * e3;
			e1 *= k1; e3 *= k3;
			y *= a;
		}
		y += p[20] * nz * tick;
		if (ts >= TRIG)
			tick *= kt;
		out[i] = (float)y;
	}
}

/* ---- RS -------------------------------------------------------------------------------------------------- *
 * p: 0 accent (0..1)  1 kick V  2 kick τ  3 f1  4 f2  5 f3  6 Q1  7 Q2  8 Q3  9 G1  10 G2  11 G3 (ring gains)
 *    12 R3 (F3 resistor, Ω)  13 VCA τ  14 HP f  15 HP Q  16 LP f  17 rail V  18 F1/F2 resistor Ω            */
void rs_f(const double *p, int n, float *out)
{
	struct cham r1, r2, r3;
	struct bq hp; struct op lp;
	double kk = rc(p[2]), kv = rc(p[13]), kick = 0, env = 0.55 + 0.45 * p[0];
	const double rpar = 1 / (2 / 12e3 + 1 / 3.3e3);           /* the circuit's: as the firmware's diode table */
	double w12 = rpar / p[18], w3 = rpar / p[12];
	int i;
	cham_set(&r1, p[3], p[6], p[17]); cham_set(&r2, p[4], p[7], p[17]); cham_set(&r3, p[5], p[8], p[17]);
	bq_hp(&hp, p[14], p[15]); op_set(&lp, p[16]);
	for (i = 0; i < n; i++) {
		double ts = i / FS, v1, v2, v3, x, u;
		kick = ts < TRIG ? p[1] : kick * kk;                     /* the edge-differentiated trigger */
		if (ts < 1 / FS) kick = p[1];
		/* bridged-T with the excitation on (+): out = in + resonant band-pass */
		v1 = kick + p[9] * cham_bp(&r1, kick);
		v2 = kick + p[10] * cham_bp(&r2, kick);
		v3 = kick + p[11] * cham_bp(&r3, kick);
		v1 = fmax(-p[17], fmin(p[17], v1)); v2 = fmax(-p[17], fmin(p[17], v2)); v3 = fmax(-p[17], fmin(p[17], v3));
		x = (v1 + v2) * w12 + v3 * w3;                           /* the summing resistors' currents x rpar */
		u = diode_pair(x, rpar, 12.68e3);
		u = op_lp(&lp, u * vca(env + 0.6, 0.6) * 20);
		env *= kv;
		out[i] = (float)(2 * bq(&hp, u));
	}
}

/* ---- HC -------------------------------------------------------------------------------------------------- *
 * p: 0 accent (0..1)  1 BP f  2 BP Q  3..5 burst onsets 2-4 (s)  6 burst depth (nepers)  7 burst τ (control RC)
 *    8 burst gain  9 tail gain  10 tail rise τ  11 tail decay τ  12 tail LP f  13 burst HP f  14 last-burst τ
 *    15 tilt HP f  16 tilt mix (0 = only the HP, 1 = flat)                                                   */
void hc_f(const double *p, int n, float *out)
{
	struct bq bp; struct op hp1, tilt, tlp;
	double kc = rc(p[7]), klast = rc(p[14]), kr = rc(p[10]), kd = rc(p[11]);
	double ctl = 1, er = 1, ed = 1, acc = 0.7 + 0.3 * p[0];
	int i, nb = 0;
	bq_bp(&bp, p[1], p[2]);
	op_set(&hp1, p[13]); op_set(&tilt, p[15]); op_set(&tlp, p[12]);
	for (i = 0; i < n; i++) {
		double ts = i / FS, x = bq(&bp, noise()), b, g, tail;
		if (nb < 3 && ts >= p[3 + nb]) { nb++; ctl = 1; }
		/* exponential converter on an RC-recovering control: about linear in dB near the top of each burst,
		 * and fully off once the control has run down (the OTA's current goes to zero, no floor) */
		g = acc * (exp(p[6] * ctl) - 1) / (exp(p[6]) - 1);
		ctl *= nb == 3 ? klast : kc;
		b = op_hp(&hp1, x) * g;
		b = op_hp(&tilt, b) + p[16] * tilt.s;
		tail = op_lp(&tlp, x) * (ed - er);
		er *= kr; ed *= kd;
		out[i] = (float)(p[8] * b + p[9] * tail);
	}
}
