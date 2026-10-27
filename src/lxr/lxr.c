/*
 * LXR: machine 13 of the Model-TG Plaits Edition, drum voices after the Erica Synths LXR-02 firmware.
 *
 * Copyright (C) 2012-2026 Erica Synths (the LXR firmware this is derived from)
 * Copyright (C) 2026 the Model-TG Plaits Edition contributors (this port)
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
 * License version 3 as published by the Free Software Foundation, with the additional terms of the LXR firmware's
 * NOTICE.md (its section 7 terms: no claim of affiliation with Erica Synths; modified versions marked as such). It is
 * a modified version: a fixed-point port, for the Model:Cycles' ColdFire (no FPU), of the LXR's DrumVoice.c, Snare.c,
 * HiHat.c, CymbalVoice.c, Oscillator.c, Decay.c, SlopeEg2.c, snapEg.c and distortion.c
 * (https://github.com/erica-synths/lxr-desktop-app). It is not made, approved or supported by Erica Synths.
 * See src/lxr/LICENSE and src/lxr/NOTICE.md.
 *
 * What is kept from the LXR: the voice structures (an oscillator phase-modulated by a second one, the FM amount
 * following the pitch envelope, the snare's tone + sample-and-hold noise, the hi-hat / cymbal carrier modulated by a
 * sine and a noise oscillator), the envelopes (DecayEg's linear ramp through the (1+k)x/(1+k|x|) slope, SlopeEg2 with
 * its repeat mode for claps, the snap EG), the phase-offset transient and the distortion curve. What is ours: the
 * band-limited tables (computed by tools/gen_lxr.py), the clicks (made here, not the LXR's sampled transients, whose
 * source recordings carry their own terms), a state-variable filter for the noise in place of the LXR's ZDF filter,
 * the eight engines' settings and the three macros.
 *
 * Contract (as dx_fill / mi_fill): lxr_params(track, trackData) once a block, before lxr_fill(track, step, machine),
 * which writes 64 values at sampler_buf+32 (96 kHz, each 48 kHz sample twice, a sample << 15) and reads and clears
 * dr_trig[track]. step: the note, Q16, 1.0 = note 60. The amp stage (Model-TG: Attack, Filter, Resonance, Amp Decay)
 * follows. No floating point, no 64-bit arithmetic, no library call: built with gcc -S into lxr_gen.s.
 */
#include "lxr_tables.h"

typedef int s32;
typedef unsigned int u32;
typedef short s16;
typedef unsigned short u16;
typedef unsigned char u8;

#define OVL __attribute__((section(".lxr_rw")))   /* gen_lxr.py moves every section to .ovl */
#define NB 32                                   /* frames a block, 48 kHz */
#define FS 48000

extern unsigned char dr_trig[8];
extern s32 sampler_buf[];

enum { W_SINE, W_TRI, W_SAW, W_REC, W_NOISE };
enum { E_KICK, E_TOM, E_PERC, E_SNARE, E_CLAP, E_CHAT, E_OHAT, E_CYMB, N_ENG };

typedef struct {
	u32 phase, inc, base;                       /* base: the increment at pitch 1.0 */
	s32 wave, band, out;                        /* out: the noise's held value */
} Osc;

typedef struct {                                /* LXR DecayEg: value Q16, linear, through the slope */
	s32 value, decay, slope;                    /* slope: k, Q8 */
} DecayEg;

typedef struct {                                /* LXR SlopeEg2, at the block rate */
	s32 value, attack, decay, slope, inv, state, repeat, cnt;
} SlopeEg;

typedef struct {
	s32 lp, bp;                                 /* Chamberlin state-variable filter, Q15 states */
} Svf;

typedef struct {
	s32 eng;                                    /* the engine the state is for, + 1 (0: none) */
	s32 dial[4];                                /* Color, Shape, Sweep, Contour: 0..0x7f00 */
	Osc osc, mod, mod2;
	DecayEg pitch;
	SlopeEg vol;
	s32 snap;                                   /* snap EG, Q16 */
	u32 click;                                  /* the click's age, samples */
	Svf f;
	u32 seed;
	s32 hold;                                   /* last sample, for the second of each pair */
} Voice;

static Voice v_[6] OVL;

/* ---- helpers -------------------------------------------------------------------------------------------- */

static inline s32 clamp16(s32 x)
{
	return x > 32767 ? 32767 : (x < -32767 ? -32767 : x);
}

/* a (u32 increment) x b (Q12 ratio, 0..32 x): no 64-bit; held under Nyquist */
static u32 mul_q12(u32 a, u32 b)
{
	u32 hi = a >> 12;
	if (hi && b > 0x7fffffffu / hi)
		return 0x7fffffffu;
	return hi * b + (((a & 0xfff) * b) >> 12);
}

/* frequency (Hz, Q4) -> phase increment at 48 kHz: f * 2^32 / 48000 = f * 89478.485 */
static u32 hz_inc(u32 hz16)
{
	return (hz16 >> 4) * 89478u + (((hz16 & 15) * 89478u) >> 4) + (hz16 >> 5);
}

/* the band of the band-limited tables for an increment: octave k covers 27.5 * 2^k .. 2^(k+1) Hz */
static s32 band_of(u32 inc)
{
	s32 b = 0;
	u32 lim = 27u * 89478u * 2u;                /* ~ 2 x 27.5 Hz */
	while (b < LX_BANDS - 1 && inc >= lim) {
		lim <<= 1;
		b++;
	}
	return b;
}

static inline u32 rnd(u32 *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

/* one sample of an oscillator, phase-modulated by pm (a phase offset, 32 bits) */
/* The sine: the stock OS's own table in the internal SRAM (0x8000eee4, filled at boot and only read by
 * the stock voices: 257 longs, round(2^31 sin(2 pi i / 256)), Q31). Its high halves are a Q15 sine; read
 * there, interpolated, it costs what a table of ours did, but it never misses the data cache (the SRAM
 * answers in one cycle) and takes no cache line from the other voices. */
#define LX_SIN ((const s16 *)0x8000eee4)

static inline s32 sram_sin(u32 ph)
{
	u32 i = (ph >> 24) << 1;                /* 256 steps; the high half of long i */
	s32 frac = (ph >> 8) & 0xffff;
	s32 a = LX_SIN[i], b = LX_SIN[i + 2];
	return a + (((b - a) * (frac >> 1)) >> 15);
}

static inline s32 osc_tick(Osc *o, u32 pm, u32 *seed)
{
	u32 ph = o->phase + pm;
	s32 a, b, frac;
	u32 last = o->phase;
	o->phase += o->inc;
	switch (o->wave) {
	case W_SINE:
		return sram_sin(ph);
	case W_NOISE:                               /* LXR calcNoiseBlock: a new value each wrap */
		if (o->phase < last)
			o->out = (s32)(s16)(rnd(seed) >> 16);
		return o->out;
	default: {
		const s16 *t = lx_wav[o->wave - W_TRI][o->band];
		u32 i = ph >> 23;                       /* 512 entries */
		frac = (ph >> 9) & 0x3fff;              /* 14 bits: (b - a) x frac stays in 31 */
		a = t[i];
		b = t[(i + 1) & 511];
		return a + (((b - a) * frac) >> 14);
	}
	}
}

/* LXR DecayEg_calcSlopeValue / slopeEg2_calcSlopeValue: (1+k) v / (1+k|v|), v Q16, k Q8 */
static s32 slope(s32 v, s32 k)
{
	s32 num, den;
	if (v <= 0)
		return 0;
	num = (256 + k) * (v >> 4);                 /* Q12 x Q8 */
	den = 256 + ((k * (v >> 4)) >> 12);
	if (den <= 0)
		return v;
	num /= den;
	return num << 4;
}

/* LXR setSlope(data): amount = (data/127 - 0.5) x 1.999, k = 2 amount / (1 - amount), Q8 */
static s32 slope_k(s32 data)
{
	s32 a = ((data * 256) / 127 - 128) * 1999 / 1000;      /* Q8, -255..255 */
	s32 den = 256 - a;
	if (den < 2)
		den = 2;
	return (2 * a * 256) / den;
}

/* a time in ms -> a per-block step of a Q16 ramp (1500 blocks a second) */
static s32 step_ms(s32 ms)
{
	s32 blocks = ms * 3 / 2;
	return blocks < 1 ? 65536 : 65536 / blocks;
}

static s32 decay_calc(DecayEg *e)
{
	s32 v = e->value;
	v = v > 0 ? v - e->decay : 0;
	if (v < 0)
		v = 0;
	e->value = v;
	return slope(v, e->slope);
}

enum { EG_STOP, EG_A, EG_D, EG_REP };

static void vol_trigger(SlopeEg *e)
{
	if (!e->repeat) {
		e->state = EG_A;
	} else {
		e->state = EG_REP;
		e->value = 65536;
	}
	e->cnt = e->repeat;
}

static s32 vol_calc(SlopeEg *e)                 /* LXR slopeEg2_calc, Q16 */
{
	s32 v = e->value;
	switch (e->state) {
	case EG_REP:                                /* the attack time is the loop's decay */
		if (v > 0) {
			v -= e->attack;
		} else if (--e->cnt > 0) {
			v = 65536;
		} else {
			e->state = EG_D;
			v = 65536;
		}
		if (v < 0)
			v = 0;
		e->value = v;
		return slope(v, e->slope);
	case EG_A:
		v += e->attack;
		if (v >= 65536) {
			v = 65536;
			e->state = EG_D;
		}
		e->value = v;
		return slope(v, e->inv);
	case EG_D:
		if (v > 0) {
			v -= e->decay;
			if (v < 0)
				v = 0;
			e->value = v;
			return slope(v, e->slope);
		}
		e->value = 0;
		return 0;
	}
	return 0;
}

/* LXR distortion: (1+k) x / (1+k|x|), x Q15, k Q8 */
static inline s32 dist(s32 x, s32 k)
{
	s32 ax = x < 0 ? -x : x;
	return ((256 + k) * x) / (256 + ((k * ax) >> 15));
}

/* the noise filter: Chamberlin SVF, f = 2 sin(pi fc / fs) Q15, q (damping) Q15; returns lp, bp, hp by mode */
static inline s32 svf(Svf *s, s32 in, s32 f, s32 q, s32 mode)
{
	s32 hp;                                     /* states held to +-0xffff: f x state fits 31 bits */
	s->lp += (f * s->bp) >> 15;
	if (s->lp > 0xffff) s->lp = 0xffff; else if (s->lp < -0xffff) s->lp = -0xffff;
	hp = in - s->lp - ((q * s->bp) >> 15);
	if (hp > 0xffff) hp = 0xffff; else if (hp < -0xffff) hp = -0xffff;
	s->bp += (f * hp) >> 15;
	if (s->bp > 0xffff) s->bp = 0xffff; else if (s->bp < -0xffff) s->bp = -0xffff;
	return mode == 0 ? s->lp : (mode == 1 ? s->bp : hp);
}

static s32 svf_f(s32 hz)                         /* 2 sin(pi hz / 48000), Q15, hz <= 12000 */
{
	u32 ph = hz_inc((u32)hz << 4) >> 1;     /* pi hz / fs as a phase: hz/96000 of a turn */
	s32 s = 2 * sram_sin(ph);
	return s > 32767 ? 32767 : s;
}

/* ---- the engines' settings ------------------------------------------------------------------------------ */
/*      base Hz Q4, mod ratio Q12, waves, vol decay ms, vol slope, pitch slope, attack */
typedef struct {
	u16 base, ratio;
	u8 wave, modw, decay_s, slope;
	u16 decay_ms;
} Kit;

static const Kit kit[N_ENG] = {
	{  50 << 4, 6144, W_SINE,  W_SINE, 96, 100, 750 },   /* KICK  */
	{ 110 << 4, 6144, W_SINE,  W_SINE, 90,  90, 600 },   /* TOM   */
	{ 190 << 4, 9421, W_REC,   W_SAW,  80,  70, 300 },   /* PERC  */
	{ 185 << 4, 4096, W_TRI,   W_NOISE, 90, 80, 330 },   /* SNARE */
	{ 900 << 4, 4096, W_TRI,   W_NOISE, 80, 64, 280 },   /* CLAP  */
	{ 420 << 4, 6021, W_REC,   W_SINE, 64,  64,  70 },   /* CL HAT */
	{ 420 << 4, 6021, W_REC,   W_SINE, 64,  64, 650 },   /* OP HAT */
	{ 330 << 4, 5939, W_REC,   W_SINE, 64,  64, 1700 },  /* CYMBAL */
};

static void osc_set(Osc *o, u32 base, s32 wave)
{
	o->base = base;
	o->wave = wave;
}

static void osc_pitch(Osc *o, u32 ratio_q12)  /* the increment for this block, and its band */
{
	o->inc = mul_q12(o->base, ratio_q12);
	o->band = band_of(o->inc);
}

/* ---- lxr_params: the four dials of a track, every block --------------------------------------------------- */
void lxr_params(s32 track, const s16 *td)
{
	Voice *v = &v_[track];
	s32 i;
	for (i = 0; i < 4; i++) {
		s32 x = td[11 + i];
		v->dial[i] = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	}
}

/* Contour -> the engine: eight equal steps */
static s32 engine_of(s32 contour)
{
	s32 e = (contour >> 8) * N_ENG / 128;
	return e >= N_ENG ? N_ENG - 1 : e;
}

static void trigger(Voice *v, s32 e)
{
	const Kit *k = &kit[e];
	s32 c = v->dial[0] >> 8, s = v->dial[1] >> 8, w = v->dial[2] >> 8;
	u32 base = hz_inc(k->base);
	osc_set(&v->osc, base, k->wave);
	osc_set(&v->mod, mul_q12(base, k->ratio), k->modw);
	osc_set(&v->mod2, hz_inc(8000 << 4), W_NOISE);
	if (e >= E_CHAT) {                          /* metal: Color tunes the carrier and its modulator */
		u32 r = 4096 + c * 64;                  /* x1 .. x3 */
		v->osc.base = mul_q12(base, r);
		v->mod.base = mul_q12(v->mod.base, r);
	}
	if (e == E_SNARE || e == E_CLAP)
		v->mod.base = hz_inc((u32)(1500 + s * 90) << 4);     /* the noise's rate */
	/* phases: the LXR restarts the waves at their peak (offset mode: a click) */
	v->osc.phase = v->osc.wave == W_SINE ? (0x3ffu << 20) : (0xffu << 22);
	v->mod.phase = v->mod2.phase = 0;
	/* the pitch envelope: Color = amount (LXR: amount^2 x 32), Shape = its time, 4..400 ms */
	v->pitch.value = 65536;
	v->pitch.decay = step_ms(4 + s * s / 41);
	v->pitch.slope = slope_k(k->decay_s);
	/* the amp envelope: the engine's own decay (Amp Decay comes after); Clap: Color spreads its repeats */
	v->vol.attack = 65536;
	v->vol.decay = step_ms(k->decay_ms);
	v->vol.slope = slope_k(k->slope);
	v->vol.inv = -v->vol.slope;
	v->vol.repeat = 0;
	if (e == E_CLAP) {
		v->vol.repeat = 1 + w * 5 / 127;        /* Sweep: 1..6 claps */
		v->vol.attack = step_ms(6 + c / 6);     /* Color: 6..27 ms between them */
	}
	vol_trigger(&v->vol);
	v->snap = 65536;
	v->click = 0;
	v->f.lp = v->f.bp = 0;
}

/* ---- lxr_fill: one block ----------------------------------------------------------------------------------- */
void lxr_fill(s32 track, u32 step, s32 machine)
{
	Voice *v = &v_[track];
	s32 *out = sampler_buf + 8;
	s32 e = engine_of(v->dial[3]);
	s32 c = v->dial[0] >> 8, s = v->dial[1] >> 8, w = v->dial[2] >> 8;
	s32 i, pe, ve, amt, fm, gain, f = 0, q = 0, mix = 0, g1 = 0, g2 = 0, drive;
	u32 ratio;
	(void)machine;
	if (!step)
		step = 0x10000;
	if (v->eng != e + 1) {                      /* a new engine: from rest */
		v->eng = e + 1;
		v->vol.state = EG_STOP;
		v->vol.value = 0;
		v->seed = 0x2545f491u * (u32)(track + 1);
	}
	if (dr_trig[track]) {
		dr_trig[track] = 0;
		trigger(v, e);
	}
	if (v->vol.state == EG_STOP) {              /* silent: nothing computed */
		for (i = 0; i < 64; i++)
			out[i] = 0;
		return;
	}
	/* control rate: once a block, as the LXR's calc...Async */
	pe = decay_calc(&v->pitch);                 /* Q16 */
	ve = vol_calc(&v->vol);
	ratio = step >> 4;                          /* the note, Q12 */
	if (e <= E_PERC) {
		amt = c * c * 8;                        /* LXR: (c/127)^2 x 32: Q12, up to x32 */
		osc_pitch(&v->osc, mul_q12(ratio, 4096 + ((amt * (pe >> 4)) >> 12)));
		osc_pitch(&v->mod, ratio);
		fm = (w * (pe >> 8)) >> 7;              /* Sweep x the pitch EG: the FM index, Q8 of a turn */
	} else if (e <= E_CLAP) {
		amt = w * w * 2;                        /* Sweep: the snap of the tone, up to x8 */
		osc_pitch(&v->osc, mul_q12(ratio, 4096 + ((amt * (pe >> 4)) >> 12)));
		osc_pitch(&v->mod, 4096);               /* the noise's rate does not follow the note */
		mix = e == E_CLAP ? 32767 : c * 258;    /* Color: tone -> noise */
		f = svf_f(e == E_CLAP ? 700 + s * 30 : 900 + s * 60);
		q = e == E_CLAP ? 9000 : 16000;
		fm = 0;
	} else {
		osc_pitch(&v->osc, ratio);
		osc_pitch(&v->mod, ratio);
		osc_pitch(&v->mod2, 4096);
		g1 = (127 - s) * 258;                   /* Shape: metal (sine) -> noise */
		g2 = s * 258;
		f = svf_f(2000 + w * 78);               /* Sweep: the high-pass, 2..12 kHz */
		q = 20000;
		fm = 0;
	}
	gain = ve >> 1;                             /* Q15 */
	drive = e <= E_PERC ? 64 + w : 32;
	for (i = 0; i < NB; i++) {
		s32 x, n;
		if (e <= E_PERC) {
			s32 m = osc_tick(&v->mod, 0, &v->seed);
			x = osc_tick(&v->osc, (u32)(m * fm) << 9, &v->seed);
		} else if (e <= E_CLAP) {
			s32 t = osc_tick(&v->osc, 0, &v->seed);
			n = svf(&v->f, osc_tick(&v->mod, 0, &v->seed), f, q, 1);
			x = ((t * (32767 - mix)) >> 15) + ((clamp16(n) * mix) >> 15);
		} else {
			s32 m = (osc_tick(&v->mod, 0, &v->seed) * g1 + osc_tick(&v->mod2, 0, &v->seed) * g2) >> 15;
			x = osc_tick(&v->osc, (u32)m << 17, &v->seed) >> 1;
			x = svf(&v->f, x, f, q, 2);
		}
		/* the click: a 2 ms burst of noise and a falling blip, its level by the engine */
		if (v->click < 96) {
			s32 a = (96 - (s32)v->click) * 341;
			s32 nz = (s32)(s16)(rnd(&v->seed) >> 16);
			x += (((nz >> 2) + ((v->click & 2) ? 4000 : -4000)) * a) >> 15;
			v->click++;
		}
		x = clamp16((x * gain) >> 15);
		x = clamp16(dist(x, drive));            /* the curve's rounding can pass full scale by a hair */
		out[2 * i] = out[2 * i + 1] = x << 15;
	}
}
