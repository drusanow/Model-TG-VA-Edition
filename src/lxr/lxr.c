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
 * HiHat.c, CymbalVoice.c, Oscillator.c, ResonantFilter.c, Decay.c, SlopeEg2.c, snapEg.c, distortion.c and mixer.c's
 * decimator (https://github.com/erica-synths/lxr-desktop-app). It is not made, approved or supported by Erica Synths.
 * See src/lxr/LICENSE and src/lxr/NOTICE.md.
 *
 * The four LXR voices, as the LXR computes them: the drum voice (an oscillator mixed with, or phase-modulated by, a
 * second one; the FM amount following the pitch envelope), the snare (a tone plus sample-and-hold noise through its
 * filter; its repeat mode is the LXR's clap), the cymbal and the hi-hat (a carrier phase-modulated by two summed
 * oscillators). Each has the LXR's own pitch EG (DecayEg), amp EG (SlopeEg2: attack, decay, slope, repeats), snap EG,
 * phase-offset trigger, zero-delay-feedback state-variable filter (LP, HP, BP, unity BP, notch, peak, the naive
 * 2-pole, off) with its input drive, distortion, volume and sample-rate decimation. A voice's sound is the LXR's
 * knob values for it (P_*, 0..127, as an LXR kit stores them); every curve that turns a knob into a coefficient is
 * the LXR's own, evaluated by tools/gen_lxr.py into lxr_tables.h. The wavetables are the LXR's (lxr_wave.h); the sine
 * is the stock OS's in the SRAM, interpolated. The presets are the LXR's init kit (lxr_presets.h).
 * Not ported: the sampled transients (their source recordings carry their own terms - a kit asking for one plays
 * without it, as an LXR whose sample is missing), user samples as oscillator waves (silent likewise), the LFOs and
 * velocity routing (the Cycles has its own), pan.
 *
 * Contract (as dx_fill / mi_fill): lxr_params(track, trackData) once a block, before lxr_fill(track, step, machine),
 * which writes 64 values at sampler_buf+32 (96 kHz, each 48 kHz sample twice, a sample << 15) and reads and clears
 * dr_trig[track]. step: the note, Q16, 1.0 = note 60 (the LXR's default note). The amp stage (Model-TG: Attack,
 * Filter, Resonance, Amp Decay) follows. No floating point, no 64-bit arithmetic, no library call: built with gcc -S
 * into lxr_gen.s. Also built on a host (-DLX_HOST, any FS / NB) to be compared with the LXR's own code (tests).
 */
typedef int s32;
typedef unsigned int u32;
typedef short s16;
typedef unsigned short u16;
typedef unsigned char u8;

#define OVL __attribute__((section(".lxr_rw")))   /* gen_lxr.py moves every section to .ovl */
#ifndef FS
#define FS 48000                                /* our rate */
#endif
#ifndef NB
#define NB 32                                   /* frames a block */
#endif
/* The voice runs as on an LXR: at its rate, 44.14 kHz - its oscillators alias as they do there (much of a metallic
 * hat's sound is aliasing), its filter and decimator see the same rate - and in its 64-sample blocks, the envelopes
 * and every control value stepping once a block (calc...Async), the blocks starting at the trigger as an LXR's
 * triggers fall between blocks. Each of our blocks renders the voice's samples its NB frames at FS span, then
 * interpolates the frames from them (4-point Hermite, 2 samples late: within 1 dB of the LXR's spectrum to 16 kHz,
 * where linear interpolation lost 2 to 5 dB of a hat's top). */
#define LXR_FS 44140
#define LB 64                                             /* the LXR's block */
#define STEP_Q16 ((u32)(65536.0 * LXR_FS / FS + 0.5))     /* the voice's samples a frame of ours, Q16 */
#define NI (NB + 2)                                       /* the voice's samples a block, at most */
#define KF ((u32)(1099511627776.0 / LXR_FS + 0.5))        /* Hz Q8 -> a 32-bit increment: x 2^40 / 44140, >> 16 */
#define INC440 ((u32)(440.0 * 4294967296.0 / LXR_FS + 0.5))   /* 440 Hz's increment */

enum { K_DRUM, K_SNARE, K_CYM, K_HAT };
enum { W_SINE, W_TRI, W_SAW, W_REC, W_NOISE, W_PWM };
/* a voice's parameters: the LXR's knobs (tools/lxr_import.py names where each comes from) */
enum { P_WAVE, P_WAVE2, P_WAVE3, P_COARSE, P_FINE, P_MODF1, P_MODF2, P_GAIN1, P_GAIN2, P_MIXMODE,
       P_PDEC, P_PAMT, P_PSLOPE, P_ATT, P_DEC, P_DECO, P_VSLOPE, P_REPEAT, P_NOISEF, P_MIX,
       P_FTYPE, P_FFREQ, P_FRESO, P_FDRIVE, P_DIST, P_VOL, P_DECIM, P_TWAVE, P_TVOL, P_TFREQ, P_PWM, N_P };
enum { F_LP = 1, F_HP, F_BP, F_UBP, F_NOTCH, F_PEAK, F_NAIVE, F_OFF };
enum { EG_STOP, EG_A, EG_D, EG_REP };

#include "lxr_tables.h"
#include "lxr_wave.h"
#include "lxr_presets.h"

#ifdef LX_HOST
extern unsigned char dr_trig[8];
extern s32 sampler_buf[];
extern const s16 *lx_host_sin;                  /* the stock table's high halves, from the test */
#define LX_SIN lx_host_sin
#else
extern unsigned char dr_trig[8];
extern s32 sampler_buf[];
/* The sine: the stock OS's own table in the internal SRAM (0x8000eee4, filled at boot and only read by the stock
 * voices: 257 longs, round(2^31 sin(2 pi i / 256)), Q31; build.py checks it). Its high halves are a Q15 sine; read
 * there, interpolated, it never misses the data cache and takes no cache line from the other tracks. */
#define LX_SIN ((const s16 *)0x8000eee4)
#endif

typedef struct {
	u32 phase, inc;
	s32 wave, band, out;
} Osc;

typedef struct {
	s32 s1, s2, a, b;                       /* the filter: ZDF states / the naive 2-pole's, Q12 */
} Svf;

typedef struct {
	u8 p[N_P];                              /* the knobs in use: the preset's, moved by the macros */
	s32 dial[4];
	s32 preset;                             /* + 1; 0: none yet */
	u32 seed;
	Osc osc, mod, mod2, noise;
	s32 pval;                               /* the pitch EG, Q24 */
	s32 vstate, vval, vrep;                 /* the amp EG, Q24 */
	s32 snap;                               /* the snap EG, Q24 */
	s32 gain, last;                         /* the amp gain this block and the last (drum voice), Q15 */
	s32 open;                               /* the hat: triggered open */
	Svf f;
	u32 dcnt;                               /* the decimator */
	s32 dhold;
	u32 t;                                  /* to our rate: the next frame's place after hist[3], Q16 */
	s16 hist[4];                            /* the last four samples of the block before */
	s32 bpos;                               /* the place in the LXR's block, 0..63 */
	s32 vol, cv, fa, pe, inst;              /* this LXR block's control values */
	s32 cooked;                             /* the dials and preset p[] was made from; -1: none */
	s32 dknob;                              /* the distortion knob dcurve was made for; -1: none */
	s16 dcurve[65];
} Voice;

static Voice v_[6] OVL;

/* ---- arithmetic --------------------------------------------------------------------------------------------- */
static inline s32 sat16(s32 x)
{
	return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

/* (a x b) >> s, 0 < s <= 32, without 64-bit arithmetic (16-bit pieces); saturates at 2^32 - 1 */
static u32 mulshr(u32 a, u32 b, s32 s)
{
	u32 al = a & 0xffff, ah = a >> 16, bl = b & 0xffff, bh = b >> 16;
	u32 p0 = al * bl, p1 = al * bh, p2 = ah * bl, p3 = ah * bh;
	u32 mid = (p0 >> 16) + (p1 & 0xffff) + (p2 & 0xffff);
	u32 hi = p3 + (p1 >> 16) + (p2 >> 16) + (mid >> 16);
	u32 lo = (mid << 16) | (p0 & 0xffff);
	if (s == 32)
		return hi;
	if (hi >> s)
		return 0xffffffffu;
	return (hi << (32 - s)) | (lo >> s);
}

static inline u32 rnd(u32 *s)                   /* the LXR's hardware RNG: here an LCG */
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

/* The LXR's slope curve, (1+k) v / (1+k|v|) with k = 2a / (1-a), written with a: (1+a) v / (1 - a + 2a|v|).
 * v, a: Q15 (|a| <= 1); -> Q15. */
static s32 slope(s32 v, s32 a)
{
	u32 num, den;
	s32 neg = v < 0;
	if (neg)
		v = -v;
	num = (u32)(32768 + a) * (u32)v;
	den = (u32)(32768 - a + ((a * v) >> 14));
	if ((s32)den <= 0)
		v = num ? 32768 : 0;
	else
		v = (s32)(num / den);
	return neg ? -v : v;
}

/* ---- oscillators: Oscillator.c ------------------------------------------------------------------------------ */
static inline s32 sin_at(u32 ph)                /* the LXR's "sine", 256 steps, interpolated: Q15 */
{
	u32 i;
	ph -= 0x40000000u;                      /* its sine_table is -cos: a sine a quarter turn late */
	i = (ph >> 24) << 1;
	s32 frac = (ph >> 8) & 0xffff;
	s32 a = LX_SIN[i], b = LX_SIN[i + 2];
	return a + (((b - a) * (frac >> 1)) >> 15);
}

static inline s32 tab_at(const s16 *t, u32 ph)  /* a 1024-sample table, interpolated (22-bit fraction) */
{
	u32 i = ph >> 22;
	s32 a = t[i], b = t[(i + 1) & 1023];
	return a + (((b - a) * (s32)((ph >> 7) & 0x7fff)) >> 15);
}

/* osc_calcWavetableFreq's table: the octave of the note the frequency rounds down to, from MIDI note 0; every
 * frequency under 440 Hz uses the fifth (the LXR's (12 log2(f / 440) + 70) / 12 over integers) */
static s32 band_of(u32 inc)
{
	u32 b = INC440;
	s32 k = 5;
	if (inc < b)
		return 4;
	while (k < 10 && inc >= b << 1 && b < 0x80000000u) {
		b <<= 1;
		k++;
	}
	return k;
}

/* The LXR's phase modulation, (uint32_t)(mod x fmMod) << shift, from a float: on its ARM core the conversion
 * saturates, so a negative product is 0 - only the modulator's positive half moves the phase. Kept. */
static inline u32 pmod(s32 m, s32 fm)
{
	s32 x = (m * fm) >> 15;
	return x > 0 ? (u32)x : 0;
}

/* one oscillator into buf, x gain (Q15; 32768 = 1), phase-modulated by mod x fm (Q15) shifted by the LXR's
 * amount for the wave (sine: << 17, tables: << 19); mod = 0: none */
static void osc_block(Osc *o, s16 *buf, s32 n, s32 gain, const s16 *mod, s32 fm, s32 pwm, u32 *seed)
{
	s32 i;
	u32 ph = o->phase, inc = o->inc;
	switch (o->wave) {
	case W_SINE:
		for (i = 0; i < n; i++, ph += inc) {
			u32 p = ph;
			if (mod)
				p += pmod(mod[i], fm) << 17;
			buf[i] = (s16)((sin_at(p) * gain) >> 15);
		}
		break;
	case W_TRI: case W_SAW: case W_REC: {
		const s16 *t = lx_wav[o->wave - W_TRI][o->band];
		for (i = 0; i < n; i++, ph += inc) {
			u32 p = ph;
			if (mod)
				p += pmod(mod[i], fm) << 19;
			buf[i] = (s16)((tab_at(t, p) * gain) >> 15);
		}
		break;
	}
	case W_NOISE:                           /* calcNoiseBlock: a new random value at each wrap */
		for (i = 0; i < n; i++) {
			u32 last = ph;
			ph += inc;
			if (last > ph)
				o->out = (s32)(s16)rnd(seed);
			buf[i] = (s16)((o->out * gain) >> 15);
		}
		break;
	case W_PWM: {                           /* calcPwmBlock: a saw less a saw pwm x half a turn on */
		const s16 *t = lx_wav[W_SAW - W_TRI][o->band];
		u32 off = (u32)pwm << 16;
		for (i = 0; i < n; i++, ph += inc) {
			u32 p = ph;
			if (mod)
				p += pmod(mod[i], fm) << 19;
			buf[i] = (s16)((sat16(tab_at(t, p) - tab_at(t, p + off)) * gain) >> 15);
		}
		break;
	}
	default:                                /* a user sample: none here */
		for (i = 0; i < n; i++)
			buf[i] = 0;
		ph += inc * n;
		break;
	}
	o->phase = ph;
}

/* a frequency (the LXR's note table x its detune x our note x a modulation, Q12) -> the increment and band */
static void osc_tune(Osc *o, s32 coarse, s32 fine, u32 step, u32 mod_q12)
{
	u32 inc = mulshr(lx_freq[coarse], KF, 16);
	inc = mulshr(inc, lx_det[fine], 16);
	inc = mulshr(inc, step, 16);
	o->inc = mulshr(inc, mod_q12, 12);
	o->band = band_of(o->inc);
}

/* the LXR's trigger phase: a sine starts a quarter turn in, a table wave just under, the rest at 0; offset mode
 * (transient wave 1) moves it back by the transient's volume */
static u32 start_phase(s32 wave, s32 offset_q15)
{
	if (wave == W_SINE)
		return 1024 + (u32)mulshr((0x3ffu << 20) - 1024, (u32)offset_q15, 15);
	if (wave > W_SINE && wave <= W_REC)
		return mulshr(0xffu << 22, (u32)offset_q15, 15);
	return 0;
}

/* ---- the filter: ResonantFilter.c, SVF_calcBlockZDF (no non-linear integrators, input drive) ---------------- */
static inline s32 softclip(s32 x)               /* softClipTwo, Q12 in and out, over -16..16 (64 steps a unit) */
{
	s32 i, fr;
	x += 16 << 12;
	if ((u32)x >= 32u << 12)                /* past the table (|x| >= 16; the Pade form is about 2.0 there) */
		return x < 0 ? -lx_soft[2048] : lx_soft[2048];
	i = x >> 6;
	fr = x & 63;
	return lx_soft[i] + (((lx_soft[i + 1] - lx_soft[i]) * fr) >> 6);
}

static inline s32 ftanh(s32 y)                  /* fastTanh, Q12 in, Q15 out, over -2..2 (128 steps a unit) */
{
	s32 i, fr;
	y += 2 << 12;
	if ((u32)y >= 4u << 12)
		return y < 0 ? -32767 : 32767;
	i = y >> 5;
	fr = y & 31;
	return lx_tanh[i] + (((lx_tanh[i + 1] - lx_tanh[i]) * fr) >> 5);
}

static inline s32 clampq(s32 x, s32 m)
{
	return x > m ? m : (x < -m ? -m : x);
}

static void svf_block(Svf *s, s32 type, s32 freq, s32 reso, s32 drive, s16 *buf, s32 n)
{
	s32 i;
	s32 g = lx_fg[freq];                    /* fastTan(pi f), Q12 */
	s32 R = lx_ff[freq] >= 29485 ? 4096 : lx_fq[reso];  /* f >= 0.4499: no resonance */
	s32 drv = lx_fdrv[drive];               /* Q12 */
	if (type == F_OFF)
		return;
	if (type == F_NAIVE) {                  /* the alternative 2-pole low-pass */
		s32 f2 = lx_flp2[freq];             /* f x 2.21, Q12 */
		s32 q1 = 4096 - lx_fq[reso];
		s32 q = ((q1 * 5734) >> 12) + (q1 << 12) / (4096 - f2);   /* (1-q) 1.4 + (1-q) / (1 - f2) */
		for (i = 0; i < n; i++) {
			s32 x = softclip((buf[i] * drv) >> 15);
			s->a += (f2 * ((x - s->a) + ((q * (s->a - s->b)) >> 12))) >> 12;
			s->a = clampq(s->a, 4096);
			s->b += (f2 * (s->a - s->b)) >> 12;
			s->b = clampq(s->b, 4096);
			buf[i] = (s16)sat16((s->b * 32767) >> 12);
		}
		return;
	}
	{
		s32 g0 = (1 << 24) / (4096 + ((2 * g * R) >> 12));     /* 1 / (1 + 2 f R), Q12 */
		s32 gg = (g * g) >> 12;
		s32 f1 = (s32)(((u32)gg * (u32)g0) >> 12);             /* f^2 g0 */
		s32 den = (1 << 24) / (f1 + 4096);                     /* 1 / (f1 + 1) */
		s32 gg0 = (g * g0) >> 12, g2 = 2 * g, r2 = 2 * R;
		s32 s1 = s->s1, s2 = s->s2;
		for (i = 0; i < n; i++) {
			s32 x = softclip((buf[i] * drv) >> 15);
			s32 y1 = ((((f1 * x) >> 12) + s2 + ((gg0 * s1) >> 12)) * den) >> 12;
			s32 xx = clampq(x - y1, 65535);
			s32 sc = softclip(s1);
			s32 y0 = ((sc + ((g * xx) >> 12)) * g0) >> 12;
			s32 ugb = (r2 * y0) >> 12;                         /* 2 R y0 */
			s32 out;
			s1 = clampq(sc + ((g2 * (xx - ugb)) >> 12), 32767);
			s2 = clampq(s2 + ((g2 * y0) >> 12), 32767);
			if (type == F_LP)
				out = ftanh(y1);
			else if (type == F_BP)
				out = y0 * 8;
			else if (type == F_HP)
				out = (x - ugb - y1) * 8;
			else if (type == F_UBP)
				out = ugb * 8;
			else if (type == F_NOTCH)
				out = (x - ugb) * 8;
			else                                /* F_PEAK: y1 - h */
				out = (y1 - (x - ugb - y1)) * 8;
			buf[i] = (s16)sat16(out);
		}
		s->s1 = s1;
		s->s2 = s2;
	}
}

/* ---- the distortion: (1+k) x / (1+k|x|), k = 2a / (1-a), a = the knob - 1/128 ------------------------------ */
/* its curve, 64 steps over 0..1, rebuilt when the knob moves: a sample is then an interpolation, not a division */
static void dist_curve(s16 *t, s32 d)
{
	s32 i, a = lx_dist[d];
	for (i = 0; i <= 64; i++)
		t[i] = (s16)sat16(slope(i * 512, a));
}

static inline s32 dist(const s16 *t, s32 x)
{
	s32 neg = x < 0, i, fr, y;
	if (neg)
		x = -x;
	i = x >> 9;
	fr = x & 511;
	y = i >= 64 ? t[64] : t[i] + (((t[i + 1] - t[i]) * fr) >> 9);
	return neg ? -y : y;
}

/* ---- envelopes ---------------------------------------------------------------------------------------------- */
static s32 pitch_calc(Voice *v)                 /* DecayEg_calc: linear down, through the slope, Q15 */
{
	s32 d = (s32)lx_pdec[v->p[P_PDEC]];
	v->pval = v->pval > 0 ? v->pval - d : 0;    /* (it may end a hair under 0 for a block, as the LXR's) */
	return slope(v->pval >> 9, lx_psl[v->p[P_PSLOPE]]);
}

static void vol_trigger(Voice *v)               /* slopeEg2_trigger: the attack from where it is, or the repeats */
{
	if (!v->p[P_REPEAT])
		v->vstate = EG_A;
	else {
		v->vstate = EG_REP;
		v->vval = 1 << 24;
	}
	v->vrep = v->p[P_REPEAT];
}

static s32 vol_calc(Voice *v)                   /* slopeEg2_calc, Q15 */
{
	s32 att = (s32)lx_att[v->p[P_ATT]];
	s32 dec = (s32)lx_dec[(v->open ? v->p[P_DECO] : v->p[P_DEC])];
	s32 a = lx_vsl[v->p[P_VSLOPE]];
	switch (v->vstate) {
	case EG_REP:                            /* the attack time as a second decay, looped */
		if (v->vval > 0)
			v->vval -= att;
		else if (--v->vrep > 0)
			v->vval = 1 << 24;
		else {
			v->vstate = EG_D;
			v->vval = 1 << 24;
		}
		return slope(v->vval >> 9, a);
	case EG_A:
		v->vval += att;
		if (v->vval >= 1 << 24) {
			v->vval = 1 << 24;
			v->vstate = EG_D;
		}
		return slope(v->vval >> 9, -a);     /* invSlope */
	case EG_D:
		if (v->vval > 0) {
			v->vval -= dec;
			return slope(v->vval >> 9, a);  /* (a hair under 0 for a block, as the LXR's) */
		}
		v->vval = 0;
		v->vstate = EG_STOP;                /* (the LXR stays in D, returning 0: the same) */
		return 0;
	}
	return 0;
}

static s32 snap_calc(Voice *v)                  /* SnapEg_calc: value^2 x 24, falling by 0.2 x the transient's pitch; Q12 */
{
	s32 r = 0;
	if (v->snap > 0) {
		s32 x = v->snap >> 12;              /* Q12 */
		r = ((x * x) >> 12) * 24;
		v->snap -= (s32)lx_snap[v->p[P_TFREQ]];
	}
	return r;
}

/* ---- the parameters: the preset, moved by the macros -------------------------------------------------------- */
/* Contour -> the preset: eight equal steps */
static s32 preset_of(s32 contour)
{
	s32 e = (contour >> 8) * N_PRESETS / 128;
	return e >= N_PRESETS ? N_PRESETS - 1 : e;
}

static inline u8 knob(s32 x)
{
	return (u8)(x < 0 ? 0 : (x > 127 ? 127 : x));
}

/* Color / Shape / Sweep: each moves one of the voice's knobs from the preset's value (64: the preset as it is) */
static void cook(Voice *v, s32 pr)
{
	const u8 *src = lx_presets[pr];
	s32 i, c = (v->dial[0] >> 8) - 64, s = (v->dial[1] >> 8) - 64, w = (v->dial[2] >> 8) - 64;
	for (i = 0; i < N_P; i++)
		v->p[i] = src[i];
#ifdef LX_HOST
	{                                       /* the tests' overrides of single knobs */
		extern s32 lx_host_over[N_P];
		for (i = 0; i < N_P; i++)
			if (lx_host_over[i] >= 0)
				v->p[i] = (u8)lx_host_over[i];
	}
#endif
	switch (lx_kind[pr]) {
	case K_DRUM:                            /* pitch amount, pitch decay, FM amount */
		v->p[P_PAMT] = knob(src[P_PAMT] + 2 * c);
		v->p[P_PDEC] = knob(src[P_PDEC] + 2 * s);
		v->p[P_GAIN1] = knob(src[P_GAIN1] + 2 * w);
		break;
	case K_SNARE:
		if (src[P_REPEAT]) {                /* the clap: spacing, tone, how many */
			v->p[P_ATT] = knob(src[P_ATT] + 2 * c);
			v->p[P_FFREQ] = knob(src[P_FFREQ] + 2 * s);
			v->p[P_REPEAT] = knob(src[P_REPEAT] + w / 8);
			if (!v->p[P_REPEAT])
				v->p[P_REPEAT] = 1;
		} else {                            /* noise mix, noise rate, pitch amount (the snap) */
			v->p[P_MIX] = knob(src[P_MIX] + 2 * c);
			v->p[P_NOISEF] = knob(src[P_NOISEF] + 2 * s);
			v->p[P_PAMT] = knob(src[P_PAMT] + 2 * w);
		}
		break;
	case K_CYM:                             /* tuning (semitones), the filter's ring, the filter */
		v->p[P_COARSE] = knob(src[P_COARSE] + c / 2);
		v->p[P_FRESO] = knob(src[P_FRESO] + 2 * s);
		v->p[P_FFREQ] = knob(src[P_FFREQ] + 2 * w);
		break;
	default:                                /* the hats: tuning (semitones), the modulators, the filter */
		v->p[P_COARSE] = knob(src[P_COARSE] + c / 2);
		v->p[P_GAIN1] = knob(src[P_GAIN1] + 2 * s);
		v->p[P_GAIN2] = knob(src[P_GAIN2] + 2 * s);
		v->p[P_FFREQ] = knob(src[P_FFREQ] + 2 * w);
		break;
	}
}

/* ---- lxr_params: the four dials of a track, every block ----------------------------------------------------- */
void lxr_params(s32 track, const s16 *td)
{
	Voice *v = &v_[track];
	s32 i;
	for (i = 0; i < 4; i++) {
		s32 x = td[11 + i];
		v->dial[i] = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	}
}

static inline s32 q15(s32 knob127)              /* a knob as the LXR's value.flt (x / 127), Q15 */
{
	return (knob127 * 32768 + 63) / 127;
}

static void trigger(Voice *v, s32 kind)
{
	s32 off = v->p[P_TWAVE] == 1 ? 32768 - q15(v->p[P_TVOL]) : 32768;
	v->osc.phase = start_phase(v->osc.wave = v->p[P_WAVE], off);
	if (kind == K_DRUM) {
		v->mod.phase = start_phase(v->mod.wave = v->p[P_WAVE2], 32768);
		v->pval = 1 << 24;
		v->f.s1 = v->f.s2 = v->f.a = v->f.b = 0;            /* SVF_reset */
	} else if (kind == K_SNARE) {
		v->pval = 1 << 24;
	} else {
		v->mod.phase = v->mod2.phase = 0;
	}
	vol_trigger(v);
	v->snap = 1 << 24;
}

/* ---- once an LXR block (calc...Async): the envelopes, the tuning, the gains -------------------------------- */
static void control(Voice *v, s32 kind, u32 step)
{
	s32 pm = 4096, ve;
	v->inst = lx_att[v->p[P_ATT]] >= 1 << 24 && (v->vstate == EG_A || v->vstate == EG_REP);
	ve = vol_calc(v);
	v->pe = 0;
	if (kind == K_DRUM || kind == K_SNARE) {
		v->pe = pitch_calc(v);
		pm += (v->pe * (lx_pamt[v->p[P_PAMT]] >> 3)) >> 12;     /* (Q15 x Q9: no overflow at x32) */
	}
	if (v->p[P_TWAVE] == 0)
		pm += (snap_calc(v) * q15(v->p[P_TVOL])) >> 15;
	v->osc.wave = v->p[P_WAVE];
	osc_tune(&v->osc, v->p[P_COARSE], v->p[P_FINE], step, (u32)pm);
	v->vol = q15(v->p[P_VOL]);
	v->cv = (v->vol * ve) >> 15;
	if (kind == K_DRUM) {
		v->fa = q15(v->p[P_GAIN1]);
		v->mod.wave = v->p[P_WAVE2];
		osc_tune(&v->mod, v->p[P_MODF1], 64, step, 4096);
		/* the amp's gain: ramped across the block from the last, unless an instant attack is rising */
		if (v->inst)                        /* (its state before this block's step) */
			v->last = v->gain = ve;
		else {
			v->last = v->gain;
			v->gain = ve;
		}
	} else if (kind == K_SNARE) {
		v->noise.wave = W_NOISE;
		v->noise.inc = mulshr((u32)v->p[P_NOISEF] * 22000u * 256u / 127u, KF, 16);   /* mix x 22 kHz */
		v->last = ve;
	} else {
		v->mod.wave = v->p[P_WAVE2];
		v->mod2.wave = v->p[P_WAVE3];
		osc_tune(&v->mod, v->p[P_MODF1], 64, step, 4096);
		osc_tune(&v->mod2, v->p[P_MODF2], 64, step, 4096);
		v->last = ve;
	}
}

/* n samples of the voice (calc...SyncBlock), from place b in the LXR's block */
static void render(Voice *v, s32 kind, s16 *buf, s32 n, s32 b)
{
	s16 mb[LB], mb2[LB];
	s32 i, pwm = q15(v->p[P_PWM]);
	if (kind == K_DRUM) {
		osc_block(&v->mod, mb, n, v->fa, 0, 0, pwm, &v->seed);
		if (v->p[P_MIXMODE]) {              /* mixed: the carrier x (1 - amount) plus the modulator */
			osc_block(&v->osc, buf, n, 32768 - v->fa, 0, 0, pwm, &v->seed);
			for (i = 0; i < n; i++)
				buf[i] = (s16)sat16(buf[i] + mb[i]);
		} else                              /* FM: by amount x the pitch EG */
			osc_block(&v->osc, buf, n, 32768, mb, (v->fa * v->pe) >> 15, pwm, &v->seed);
		svf_block(&v->f, v->p[P_FTYPE] + 1, v->p[P_FFREQ], v->p[P_FRESO], v->p[P_FDRIVE], buf, n);
		{                                   /* the gain, ramped over the LXR's block (addGainInterpolated),
		                                     * Q22; the distortion; the volume */
			s32 dg = ((v->gain - v->last) * 128) / (LB - 1);
			s32 gq = (v->last * 128) + dg * b;
			for (i = 0; i < n; i++, gq += dg) {
				s32 y = (buf[i] * (gq >> 7)) >> 15;
				buf[i] = (s16)((dist(v->dcurve, (s16)y) * v->vol) >> 15);
			}
		}
	} else if (kind == K_SNARE) {
		s32 mix = q15(v->p[P_MIX]);
		osc_block(&v->noise, mb, n, 29491, 0, 0, 0, &v->seed);                      /* x 0.9 */
		svf_block(&v->f, v->p[P_FTYPE] + 1, v->p[P_FFREQ], v->p[P_FRESO], v->p[P_FDRIVE], mb, n);
		osc_block(&v->osc, buf, n, 32768 - mix, 0, 0, pwm, &v->seed);
		for (i = 0; i < n; i++)
			buf[i] = (s16)dist(v->dcurve, (s16)sat16((((((mb[i] * mix) >> 15) + buf[i]) * v->cv) >> 15)));
	} else {                                /* the cymbal and the hat */
		osc_block(&v->mod, mb, n, q15(v->p[P_GAIN1]), 0, 0, pwm, &v->seed);
		osc_block(&v->mod2, mb2, n, q15(v->p[P_GAIN2]), 0, 0, pwm, &v->seed);
		for (i = 0; i < n; i++)
			mb[i] = (s16)sat16(mb[i] + mb2[i]);
		osc_block(&v->osc, buf, n, kind == K_HAT ? 16384 : 32768, mb, 32768, pwm, &v->seed);
		svf_block(&v->f, v->p[P_FTYPE] + 1, v->p[P_FFREQ], v->p[P_FRESO], v->p[P_FDRIVE], buf, n);
		for (i = 0; i < n; i++)
			buf[i] = (s16)dist(v->dcurve, (buf[i] * v->cv) >> 15);
	}
	{                                       /* the decimator (mixer_decimateBlock): a sample held until the
	                                         * counter passes 1 */
		u32 rate = lx_decim[v->p[P_DECIM]];
		if (rate >= 65536) {                /* none (the knob at 127): every sample taken */
			v->dhold = buf[n - 1];
			return;
		}
		for (i = 0; i < n; i++) {
			v->dcnt += rate;
			if (v->dcnt >= 65536) {
				v->dcnt -= 65536;
				v->dhold = buf[i];
			}
			buf[i] = (s16)v->dhold;
		}
	}
}

/* ---- lxr_fill: one block ------------------------------------------------------------------------------------ */
void lxr_fill(s32 track, u32 step, s32 machine)
{
	Voice *v = &v_[track];
	s32 *out = sampler_buf + 8;
	s32 pr = preset_of(v->dial[3]), kind = lx_kind[pr];
	s16 xb[NI + 4];
	s16 *x = xb + 3, *buf = xb + 4;         /* x[-3..0]: the block before's last four; x[1..n]: this block's */
	s32 i, n, done;
	(void)machine;
	if (!step)
		step = 0x10000;
	if (v->preset != pr + 1) {              /* a new preset: from rest */
		v->preset = pr + 1;
		v->vstate = EG_STOP;
		v->vval = v->pval = v->snap = 0;
		v->gain = v->last = 0;
		v->dcnt = 0;
		v->dhold = 0;
		v->hist[0] = v->hist[1] = v->hist[2] = v->hist[3] = 0;
		v->t = 65536;
		v->bpos = 0;
		v->seed = 0x2545f491u * (u32)(track + 1);
		v->cooked = v->dknob = -1;
	}
	{
		s32 key = pr | (v->dial[0] >> 8) << 4 | (v->dial[1] >> 8) << 11 | (v->dial[2] >> 8) << 18;
		if (v->cooked != key) {
			cook(v, pr);
			v->cooked = key;
		}
		if (v->dknob != v->p[P_DIST]) {
			v->dknob = v->p[P_DIST];
			dist_curve(v->dcurve, v->dknob);
		}
	}
	if (dr_trig[track]) {
		dr_trig[track] = 0;
		v->open = lx_open[pr];
		trigger(v, kind);
		v->bpos = 0;                        /* the LXR's blocks start at the trigger */
	}
	if (v->vstate == EG_STOP && !v->last)
		v->dhold = 0;                       /* (a held sample does not outlive the voice) */
	if (v->vstate == EG_STOP && !v->last && !(v->hist[0] | v->hist[1] | v->hist[2] | v->hist[3])) {   /* silent */
		for (i = 0; i < 2 * NB; i++)
			out[i] = 0;
		return;
	}
	/* the voice's samples this block: as many as our NB frames span, so that the next block's first frame is after
	 * this block's last sample (t stays in 0 .. 1]; at the LXR's own rate, exactly NB */
	n = (s32)((v->t + NB * STEP_Q16 - 1) >> 16);
	for (done = 0; done < n; ) {            /* in the LXR's blocks */
		s32 seg = LB - v->bpos;
		if (seg > n - done)
			seg = n - done;
		if (!v->bpos)
			control(v, kind, step);
		render(v, kind, buf + done, seg, v->bpos);
		v->bpos = (v->bpos + seg) & (LB - 1);
		done += seg;
	}
	/* ---- to our rate: NB frames, 4-point Hermite between the voice's samples, two samples late ---- */
	xb[0] = v->hist[0];                     /* (a frame less than a sample after the last: x[-3..0]) */
	xb[1] = v->hist[1];
	xb[2] = v->hist[2];
	xb[3] = v->hist[3];
	{
		u32 t = v->t;
		for (i = 0; i < NB; i++, t += STEP_Q16) {
			s32 k = (s32)(t >> 16) - 2, fr = (s32)(t & 0xffff) >> 4;   /* the place, Q12 */
			s32 xm1, x0, x1, x2, c1, c2, c3, y;
			if (k > n - 2) {
				k = n - 2;
				fr = 4095;
			}
			xm1 = x[k - 1];
			x0 = x[k];
			x1 = x[k + 1];
			x2 = x[k + 2];
			c1 = (x1 - xm1) >> 1;
			c2 = xm1 - ((5 * x0) >> 1) + 2 * x1 - (x2 >> 1);
			c3 = ((x2 - xm1) >> 1) + ((3 * (x0 - x1)) >> 1);
			y = x0 + ((((((((c3 * fr) >> 12) + c2) * fr) >> 12) + c1) * fr) >> 12);
			out[2 * i] = out[2 * i + 1] = sat16(y) * 32768;
		}
		v->t = t - ((u32)n << 16);
	}
	v->hist[0] = x[n - 3];
	v->hist[1] = x[n - 2];
	v->hist[2] = x[n - 1];
	v->hist[3] = x[n];
}
