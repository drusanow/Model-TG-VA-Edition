/*
 * ACHORD: machine 14 of the Model-TG Plaits Edition - a thick analogue-style chord machine with keys and progressions.
 *
 * Copyright (C) 2026 the Model-TG Plaits Edition contributors
 * SPDX-License-Identifier: MIT
 *
 * Built with src/lxr/lxr.c into src/lxr/lxr_gen.s (tools/gen_lxr.py), which includes this file at its end; it uses
 * nothing of lxr.c's but its types and the OVL section macro. Its own tables: tools/gen_achord.py.
 *
 * THE SOUND (paraphonic, as the VA machine made a chord): four voices, each two oscillators - a saw and a pulse on
 * one phase (the pulse: a second saw pw on, subtracted), band-limited (polyBLEP) - OSC 2 detuned against OSC 1
 * and set an interval away (OSC2 INT); a square sub an octave under the lowest voice; DRIVE (a soft clip pushed
 * harder) and GRIT (fewer bits, held samples); a bucket-brigade-style chorus. No filter of its own: the track's Filter
 * and Resonance (Model-TG's, as every machine's), which the LFOs can move. Each voice drifts a little, and each sits a
 * few cents off the others.
 *   Color   DETN  OSC 2 against OSC 1: 0 in tune, up to 40 cents apart (each half of it, either way)
 *   Shape   WAVE  saw, saw + square, square, then the pulse narrowing and moving (PWM, at PWM RATE)
 *   Sweep   OSC2  OSC 2's level: 0 OSC 1 alone, 64 both alike, 127 OSC 2 alone
 *   Contour CHRD  the chord: its type (FREE: MAJ, MIN, 7 ...) or, in a key, its size (TRIAD, 7TH, 9TH ...)
 * Pitch and the keyboard give the note; the track's amp stage (Attack, Decay) shapes the level, as every machine's.
 *
 * THE CHORDS. The track's settings (SETTINGS + PRESET, ac_menu_*; a block of AC_SET bytes in each pattern's kit,
 * saved with the pattern): KEY and SCALE (FREE: the note is the chord's root and Contour its type, as the stock
 * Chord; or a scale, and every chord is built of its notes); NOTES (SNAP: the note snapped into the scale picks the
 * chord on it; DEGREES: each semitone up is the next chord of the key, so the pads play I, ii, iii ...); PROG (off,
 * sixteen progressions, or USER, the eight steps below it) and ADVANCE (a chord a trig, or a bar, two or four, with
 * the pattern); VOICING (AUTO: each chord's notes moved as little as they can from the last, as a player would;
 * CLOSE, OPEN, DROP 2, WIDE); BASS (the root an octave or two below); STRUM; SUB; CHORUS; DRIFT; OSC2 INT;
 * PWM RATE; DRIVE; GRIT.
 *
 * Contract: ac_params(track, trackData, settings) once a block, then ac_fill(track, step, machine), which writes 64
 * values at sampler_buf+32 (96 kHz, each 48 kHz sample twice, a sample << 15) and reads and clears dr_trig[track].
 */
typedef signed char s8;
#include "../achord/achord_tables.h"

#define AC_SET 24                               /* a track's settings: each stored + 1, 0 = the default */
enum { S_KEY, S_SCALE, S_NOTES, S_PROG, S_ADV, S_U0, S_U7 = S_U0 + 7, S_VOICE, S_BASS, S_STRUM, S_SUB, S_CHORUS,
       S_DRIFT, S_INT, S_PWM, S_DRIVE, S_GRIT, N_S };
/* each setting's largest value and its default */
static const u8 s_max[N_S] __attribute__((section(".lxr_ro"), aligned(4))) = {
	11, 7, 1, 17, 3, 7, 7, 7, 7, 7, 7, 7, 7, 4, 2, 10, 10, 3, 10, 5, 10, 10, 10};
static const u8 s_def[N_S] __attribute__((section(".lxr_ro"), aligned(4))) = {
	0, 0, 0, 0, 0, 0, 4, 5, 3, 7, 7, 7, 7, 0, 0, 0, 4, 1, 3, 0, 3, 3, 0};

static inline s32 setv(const u8 *set, s32 i)
{
	s32 v;
	if (!set || !set[i])
		return s_def[i];
	v = set[i] - 1;
	return v > s_max[i] ? s_def[i] : v;
}

/* ---- the harmony ---------------------------------------------------------------------------------------------- */
static const u8 scales[7][7] __attribute__((section(".lxr_ro"), aligned(4))) = {
	{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 2, 4, 5, 7, 9, 10},
	{0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 4, 6, 7, 9, 11}};   /* MAJOR .. LYDIAN */
/* FREE: sixteen chord types, their intervals in the order they give way (root, 3rd, 7th, the colour, 5th); 99 ends */
static const s8 free_types[16][5] __attribute__((section(".lxr_ro"), aligned(4))) = {
	{0, 4, 7, 99}, {0, 3, 7, 99}, {0, 4, 10, 7, 99}, {0, 4, 11, 7, 99}, {0, 3, 10, 7, 99}, {0, 3, 10, 6, 99},
	{0, 3, 6, 99}, {0, 3, 9, 6, 99}, {0, 4, 8, 99}, {0, 2, 7, 99}, {0, 5, 7, 99}, {0, 4, 9, 7, 99},
	{0, 3, 9, 7, 99}, {0, 4, 14, 7, 99}, {0, 4, 10, 14, 7}, {0, 3, 10, 14, 7}};
static const char *const free_names[16] __attribute__((section(".lxr_ro"), aligned(4))) = {
	"MAJ", "MIN", "7", "MAJ7", "MIN7", "M7B5", "DIM", "DIM7", "AUG", "SUS2", "SUS4", "6", "MIN6", "ADD9", "9", "MIN9"};
/* in a key: eight sizes, as scale steps from the chord's degree (7: the octave), in the same order */
static const s8 key_types[8][5] __attribute__((section(".lxr_ro"), aligned(4))) = {
	{0, 2, 4, 99}, {0, 2, 6, 4, 99}, {0, 2, 6, 8, 4}, {0, 2, 8, 4, 99}, {0, 1, 4, 99}, {0, 3, 4, 99},
	{0, 2, 5, 4, 99}, {0, 4, 7, 99}};
static const char *const key_names[8] __attribute__((section(".lxr_ro"), aligned(4))) = {
	"TRIAD", "7TH", "9TH", "ADD9", "SUS2", "SUS4", "6TH", "POWER"};
/* the progressions: degrees (0 = I) and their mode when SCALE is FREE (0 major, 1 minor) */
static const u8 progs[16][5] __attribute__((section(".lxr_ro"), aligned(4))) = {
	{0, 4, 5, 3, 0}, {0, 5, 3, 4, 0}, {5, 3, 0, 4, 0}, {0, 3, 4, 3, 0}, {1, 4, 0, 0, 0}, {0, 5, 1, 4, 0},
	{3, 4, 2, 5, 0}, {0, 2, 3, 4, 0}, {0, 3, 5, 4, 0}, {0, 4, 3, 4, 0}, {0, 5, 2, 6, 1}, {0, 3, 4, 0, 1},
	{0, 6, 5, 6, 1}, {0, 3, 6, 2, 1}, {0, 5, 3, 4, 1}, {0, 3, 5, 4, 1}};
static const char *const prog_names[18] __attribute__((section(".lxr_ro"), aligned(4))) = {
	"OFF", "I-V-vi-IV", "I-vi-IV-V", "vi-IV-I-V", "I-IV-V-IV", "ii-V-I-I", "I-vi-ii-V", "IV-V-iii-vi",
	"I-iii-IV-V", "I-IV-vi-V", "I-V-IV-V", "i-VI-III-VII", "i-iv-v-i", "i-VII-VI-VII", "i-iv-VII-III",
	"i-VI-iv-v", "i-iv-VI-v", "USER"};
static const char *const note_names[12] __attribute__((section(".lxr_ro"), aligned(4))) = {
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static s32 adv_wrap(s32 p, s32 len);

/* ---- the voice ---------------------------------------------------------------------------------------------- */
#define AC_DL 256                               /* the chorus's line: 5.3 ms, kept twice (reads never wrap) */
typedef struct {
	u32 ph[8], inc[8], rinc[8];             /* the oscillators' phases, increments, 2^30 / (inc >> 8): OSC 1 of
						 * each voice, then OSC 2 */
	s32 note[4], tgt[4], wait[4];           /* their notes (MIDI), the notes they go to, blocks until they do */
	s32 nv;                                 /* voices sounding */
	u32 sph;                                /* the sub-oscillator's phase */
	s32 gh, gn;                             /* GRIT: the sample held, samples until the next */
	s32 drift[4];                           /* each voice's drift, 1/16 cent */
	u32 seed, lfo, pwm;                     /* the drift's noise; the chorus's and the PWM's triangles */
	s32 dw;                                 /* the chorus's write place */
	s32 dial[4];
	const u8 *set;
	s32 pos, idx;                           /* the progression's next step (a chord a trig), and the one playing */
	s32 key;                                /* what the chord was made from (-1: remake it) */
	s32 root, pcs;                          /* the chord, for its name: its root (MIDI) and pitch classes */
	s32 started;
	s32 eco;                                /* rendering at 32 kHz (the CPU guard) */
	s16 dl[2 * AC_DL];
} AcV;

static AcV ac_v[6] OVL;
static u32 ac_smax OVL;                         /* the pattern's last step seen (its length - 1) */

#define AC_STEP (*(volatile u32 *)0x40a78840)   /* the sequencer's next step (0 after the last) */
#define AC_PLAY (*(volatile u32 *)0x40a7883c)   /* 1 while it plays */

static inline u32 ac_rnd(u32 *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

/* (a x b) >> 16 for a < 2^32, b < 2^17, without 64-bit arithmetic */
static inline u32 mul16(u32 a, u32 b)
{
	return (a >> 16) * b + (((a & 0xffff) * b) >> 16);
}

static u32 note_inc(s32 n)
{
	if (n < 0)
		n = 0;
	if (n > 131)
		n = 131;
	return ac_inc12[n % 12] >> (10 - n / 12);
}

/* a step (Q16, 1.0 = note 60) -> the nearest note's offset k (-48..48) */
static s32 step_note(u32 step)
{
	s32 lo = 0, hi = 97;                    /* ac_semi[i]: the lower edge of k = i - 48 */
	if (step < ac_semi[0])
		return -48;
	while (hi - lo > 1) {
		s32 m = (lo + hi) >> 1;
		if (step >= ac_semi[m])
			lo = m;
		else
			hi = m;
	}
	return lo > 96 ? 48 : lo - 48;
}

static inline s32 floordiv(s32 a, s32 b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* a chord's notes, from its root note and settings: up to five intervals (in giving-way order) into iv, its root
 * into *root; returns how many */
static s32 ac_chord(AcV *v, s32 n, s32 trig, s32 *iv, s32 *root)
{
	const u8 *set = v->set;
	s32 scale = setv(set, S_SCALE), key = setv(set, S_KEY), prog = setv(set, S_PROG);
	s32 contour = v->dial[3] >> 8, i, cnt = 0;
	if (prog || scale) {                    /* in a key */
		const u8 *sc;
		s32 deg, oct, typ = contour * 8 / 128;
		s32 base = 60 + key - (key >= 6 ? 12 : 0);    /* the key's tonic nearest middle C */
		if (prog) {
			s32 len = 4, d0;
			const u8 *steps;
			u8 user[8];
			if (prog == 17) {               /* USER: its steps up to the first END */
				for (len = 0; len < 8; len++) {
					s32 u = setv(set, S_U0 + len);
					if (u > 6)
						break;
					user[len] = (u8)u;
				}
				if (!len) {
					user[0] = 0;
					len = 1;
				}
				steps = user;
			} else
				steps = progs[prog - 1];
			if (trig) {                     /* the step: with the pattern, or a trig at a time */
				s32 adv = setv(set, S_ADV);
				if (AC_PLAY == 1) {
					u32 s = AC_STEP, cur;
					if (s > ac_smax)
						ac_smax = s;
					cur = s ? s - 1 : ac_smax;
					if (adv)                /* 1, 2, 4 bars: 16, 32, 64 steps a chord */
						v->idx = (s32)(cur >> (3 + adv));
					else                    /* a chord a trig, from the top on the pattern's first step */
						v->idx = cur == 0 ? 0 : v->pos;
				} else
					v->idx = v->pos;        /* (stopped: a chord a trig) */
				v->idx = adv_wrap(v->idx, len);
				v->pos = adv_wrap(v->idx + 1, len);
			}
			v->idx = adv_wrap(v->idx, len);
			d0 = steps[v->idx];
			sc = scales[scale ? scale - 1 : (prog == 17 ? 0 : progs[prog - 1][4])];
			deg = d0;
			oct = 0;
			base += n - 60;                 /* the note transposes it */
		} else {
			sc = scales[scale - 1];
			if (setv(set, S_NOTES)) {       /* DEGREES: a semitone a chord */
				s32 idx = n - 60;
				deg = idx - 7 * floordiv(idx, 7);
				oct = floordiv(idx, 7);
			} else {                        /* SNAP: the note, down into the scale */
				s32 rel = n - (60 + key), pc = rel - 12 * floordiv(rel, 12);
				oct = floordiv(rel, 12);
				base = 60 + key;
				for (deg = 6; deg > 0 && sc[deg] > pc; deg--)
					;
			}
		}
		*root = base + 12 * oct + sc[deg];
		for (i = 0; i < 5 && key_types[typ][i] != 99; i++) {
			s32 d = deg + key_types[typ][i];
			iv[cnt++] = sc[d % 7] + 12 * (d / 7) - sc[deg];
		}
	} else {                                /* FREE: the note is the root, Contour the type */
		s32 typ = contour * 16 / 128;
		*root = n;
		for (i = 0; i < 5 && free_types[typ][i] != 99; i++)
			iv[cnt++] = free_types[typ][i];
	}
	return cnt;
}

/* (a position past a progression's end, after its settings changed) */
static s32 adv_wrap(s32 p, s32 len)
{
	return len ? (p < 0 ? 0 : p % len) : 0;
}

/* the voices' notes for a chord: its bass, then its upper notes voiced (VOICING) */
static s32 adv_wrap(s32 p, s32 len);
static s32 ac_voice(AcV *v, s32 root, const s32 *iv, s32 cnt, s32 *out)
{
	s32 voicing = setv(v->set, S_VOICE), bass = setv(v->set, S_BASS);
	s32 up[4], nu = 0, i, j, nv = 0;
	for (i = 0; i < cnt && nu < (bass ? 3 : 4); i++) {   /* the upper notes: with a bass, the root gives way first */
		if (bass && i == 0 && cnt > 3)
			continue;
		up[nu++] = iv[i];
	}
	if (bass)
		out[nv++] = root - 12 * bass;
	{
		s32 cl[4], m = nu;                  /* close position, root up */
		for (i = 0; i < m; i++)
			cl[i] = root + (up[i] % 12);
		for (i = 1; i < m; i++)             /* (sort, ascending) */
			for (j = i; j > 0 && cl[j] < cl[j - 1]; j--) {
				s32 t = cl[j];
				cl[j] = cl[j - 1];
				cl[j - 1] = t;
			}
		if (voicing == 0 && v->started) {   /* AUTO: the inversion and octave nearest the last chord */
			s32 best = 0x7fffffff, bv[4], k, o, prev[4], pn = 0;
			for (i = 0; i < v->nv && pn < 4; i++)
				if (!(bass && i == 0))
					prev[pn++] = v->tgt[i];
			for (k = 0; k < m; k++)
				for (o = -12; o <= 12; o += 12) {
					s32 w[4], c = 0;
					for (i = 0; i < m; i++)
						w[i] = cl[(i + k) % m] + (i + k >= m ? 12 : 0) + o;
					if (w[0] < root - 9 || w[0] > root + 9)
						continue;
					for (i = 0; i < m; i++) {
						s32 p = pn ? prev[i < pn ? i : pn - 1] : root, d = w[i] - p;
						c += d < 0 ? -d : d;
					}
					if (c < best) {
						best = c;
						for (i = 0; i < m; i++)
							bv[i] = w[i];
					}
				}
			if (best != 0x7fffffff)
				for (i = 0; i < m; i++)
					cl[i] = bv[i];
		} else if (voicing == 2 && m >= 3)  /* OPEN: the second note an octave up */
			cl[1] += 12;
		else if (voicing == 3 && m >= 3)    /* DROP 2: the second from the top an octave down */
			cl[m - 2] -= 12;
		else if (voicing == 4) {            /* WIDE: as the intervals stand, the third an octave up */
			for (i = 0; i < m; i++)
				cl[i] = root + up[i] + (i == 1 ? 12 : 0);
		}
		for (i = 1; i < m; i++)             /* (low to high, for the strum) */
			for (j = i; j > 0 && cl[j] < cl[j - 1]; j--) {
				s32 t = cl[j];
				cl[j] = cl[j - 1];
				cl[j - 1] = t;
			}
		for (i = 0; i < m; i++)
			out[nv++] = cl[i];
	}
	for (i = 0; i < nv; i++)
		out[i] = out[i] < 12 ? 12 : (out[i] > 120 ? 120 : out[i]);
	return nv;
}

/* ---- ac_params: the dials and the settings, every block ---------------------------------------------------- */
void ac_params(s32 track, const s16 *td, const u8 *set)
{
	AcV *v = &ac_v[track];
	s32 i;
	for (i = 0; i < 4; i++) {
		s32 x = td[11 + i];
		v->dial[i] = x < 0 ? 0 : (x > 0x7f00 ? 0x7f00 : x);
	}
	v->set = set;
}

/* ---- the sound ---------------------------------------------------------------------------------------------- */
/* a band-limited saw, Q14: the naive ramp less the polyBLEP residual on either side of its jump */
static inline s32 saw(u32 ph, u32 inc, u32 r)
{
	s32 s = (s32)(ph >> 17) - 16384;
	if (ph + inc < inc + inc) {             /* within a step of the jump (one compare: inc < 2^31) */
		if (ph < inc) {                     /* just after it */
			s32 x = (s32)(((ph >> 8) * r) >> 16);
			s -= 2 * x - ((x * x) >> 14) - 16384;
		} else {                            /* just before it */
			s32 x = (s32)((((u32)-ph >> 8) * r) >> 16);
			s -= ((x * x) >> 14) - 2 * x + 16384;
		}
	}
	return s;
}

/* the polyBLEP residual of a jump of 2 at phase 0 (full scale +-16384), for a phase within a step of it */
static inline s32 blep(u32 ph, u32 inc, u32 r)
{
	if (ph < inc) {                             /* just after it */
		s32 x = (s32)(((ph >> 8) * r) >> 16);
		return 2 * x - ((x * x) >> 14) - 16384;
	} else {                                    /* just before it */
		s32 x = (s32)((((u32)-ph >> 8) * r) >> 16);
		return ((x * x) >> 14) - 2 * x + 16384;
	}
}

/* a band-limited pulse, Q14: high for the first pw of the cycle; up at 0, down at pw */
static inline s32 sqr(u32 ph, u32 pw, u32 inc, u32 r)
{
	s32 s = ph < pw ? 16384 : -16384;
	if (ph + inc < inc + inc)
		s += blep(ph, inc, r);
	if (ph - pw + inc < inc + inc)
		s -= blep(ph - pw, inc, r);
	return s;
}

static inline s32 soft(s32 x)                   /* a soft knee, Q14: a quarter of what passes 1, flat past 2.5 */
{
	if (x > 16384)
		return x > 40960 ? 22528 : 16384 + ((x - 16384) >> 2);
	if (x < -16384)
		return x < -40960 ? -22528 : -16384 + ((x + 16384) >> 2);
	return x;
}

#define AC_K 26029                              /* the limiter's knee: -2 dB */
static inline s32 ac_limit(s32 y)               /* 16-bit, softly limited towards full scale */
{
	s32 u, neg = y < 0;
	u = (neg ? -y : y) - AC_K;
	if (u <= 0)
		return y;
	u = AC_K + ((32767 - AC_K) * u) / (u + (32767 - AC_K));
	return neg ? -u : u;
}

void ac_fill(s32 track, u32 step, s32 machine)
{
	AcV *v = &ac_v[track];
	s32 *out = sampler_buf + 8;
	s32 i, j, k, n, key, eco, trig = dr_trig[track];
	u32 fine;
	(void)machine;
	if (!step)
		step = 0x10000;
	dr_trig[track] = 0;
	k = step_note(step);
	n = 60 + k;
	fine = (step << 8) / (ac_ratio[k + 48] >> 8);   /* the step's cents past its note, Q16 */
	/* ---- the chord: on a trig, or when what it is made from changes ---- */
	key = k + 64 + ((v->dial[3] >> 8) << 7);
	if (v->set) {                               /* (the settings' bytes, a word at a time) */
		const u32 *w = (const u32 *)v->set;
		for (i = 0; i < AC_SET / 4; i++)
			key = (key << 3 | (u32)key >> 29) ^ (s32)w[i];
	}
	if (trig || key != v->key || !v->started) {
		s32 iv[5], cnt, root, notes[4], nv, strum = setv(v->set, S_STRUM) * 3;
		cnt = ac_chord(v, n, trig, iv, &root);
		nv = ac_voice(v, root, iv, cnt, notes);
		v->root = root;
		v->pcs = 0;
		for (i = 0; i < cnt; i++)
			v->pcs |= 1 << ((iv[i] % 12 + 12) % 12);
		for (i = 0; i < nv; i++) {
			if (!v->started || i >= v->nv)
				v->note[i] = notes[i];  /* (a voice coming in starts there) */
			v->tgt[i] = notes[i];
			v->wait[i] = trig ? i * strum : 0;
		}
		if (!v->started) {
			v->seed = 0x9e3779b9u * (u32)(track + 1);
			for (i = 0; i < 8; i++)
				v->ph[i] = ac_rnd(&v->seed);
		}
		v->nv = nv;
		v->key = key;
		v->started = 1;
	}
	/* ---- the rate: 48 kHz, or 32 kHz when the CPU guard asks (guard.inc: gd_eco). eco_n (src/lxr/eco_rs.c, as
	 * DX's) gives the samples to render, 32 or 21-22; eco_out makes them the block's 32 frames (linearly at 32 kHz, a
	 * copy two samples late at 48 kHz), so a change of rate carries on from the same place in the sound. At 32 kHz the
	 * increments are x 1.5 and the chorus's times are two
	 * thirds as many samples; the drift and the triangles step once a block as always ---- */
	eco = (gd_eco >> track) & 1;
	/* (back to 48 kHz on the next trig: a saw's edges would show the resampling's change of spacing mid-note;
	 * down to 32 kHz is clean) */
	if (v->eco && !eco && !trig)
		eco = 1;
	v->eco = eco;
	n = eco_n(track, eco);
	/* ---- once a block: the strum, the drift, the increments (OSC 2: the interval, the detune) ---- */
	{
		s32 drift = setv(v->set, S_DRIFT), det = (v->dial[0] >> 8) * 320 / 127;   /* the detune, 1/16 cent */
		static const s8 spread[4] = {-3, 2, -1, 4};
		static const s8 ints[6] = {0, 7, 12, -12, 19, 24};
		s32 iv2 = ints[setv(v->set, S_INT)];
		for (i = 0; i < v->nv; i++) {
			s32 c;
			if (v->wait[i] > 0)
				v->wait[i]--;
			else
				v->note[i] = v->tgt[i];
			v->drift[i] += (s32)(ac_rnd(&v->seed) >> 28) - 8 + (v->drift[i] > 0 ? -1 : 1);
			if (v->drift[i] > drift * 24)
				v->drift[i] = drift * 24;
			if (v->drift[i] < -drift * 24)
				v->drift[i] = -drift * 24;
			c = spread[i] * drift * 16 / 5 + v->drift[i];  /* 1/16 cent */
			{
				s32 nt2 = v->note[i] + iv2;
				nt2 = nt2 < 0 ? 0 : (nt2 > 127 ? 127 : nt2);
				v->inc[i] = mul16(mul16(note_inc(v->note[i]), fine), (u32)(65536 + (c - det) * 37 / 16));
				v->inc[4 + i] = mul16(mul16(note_inc(nt2), fine), (u32)(65536 + (c + det - v->drift[i] / 2) * 37 / 16));
			}
			if (eco) {
				v->inc[i] += v->inc[i] >> 1;
				v->inc[4 + i] += v->inc[4 + i] >> 1;
			}
			v->rinc[i] = (1u << 30) / ((v->inc[i] >> 8) | 1);
			v->rinc[4 + i] = (1u << 30) / ((v->inc[4 + i] >> 8) | 1);
		}
	}
	{
		s32 shape = v->dial[1] >> 8, mix = v->dial[2] >> 8;
		s32 sub = setv(v->set, S_SUB), chorus = setv(v->set, S_CHORUS), drive = setv(v->set, S_DRIVE);
		s32 grit = setv(v->set, S_GRIT), prate = setv(v->set, S_PWM);
		s32 fo[32], og, gs, dg, A, B, pwm = 0, g1, g2, h1, h2, nosc;
		u32 pw, sinc, srinc;
		/* the wave: saw + B/2 x (saw - saw half a cycle on): saw at 0, + square at 64, square at 96, then the
		 * pulse narrowing and moving with a triangle (PWM RATE: 0.15 to 3.7 Hz) */
		if (shape <= 64) {
			A = 4096;
			B = shape * 64;
		} else {
			A = shape >= 96 ? 0 : (96 - shape) * 128;
			B = 4096;
			if (shape > 96)
				pwm = (shape - 96) * 4;     /* 0..124: how far the width moves */
		}
		v->pwm += 215040 + (u32)prate * 286720;  /* (0.7 Hz at 3) */
		{
			s32 tri = (s32)(v->pwm >> 16) - 32768;
			tri = (tri < 0 ? -tri : tri) - 16384;   /* -16384..16384 */
			pw = 0x80000000u + (u32)(tri * pwm * 837);   /* up to +-40% of a cycle */
		}
		/* the levels (Q12, four voices into one, Q14 waves): OSC 1 by 127 - Sweep, OSC 2 by Sweep, both whole at 64;
		 * A x saw + B x pulse = (A + B/2) x saw - B/2 x the saw pw on (the pulse, less its DC) */
		{
			s32 l1 = mix <= 64 ? 4096 : (127 - mix) * 65, l2 = mix >= 64 ? 4096 : mix * 64;
			g1 = ((A + (B >> 1)) * l1) >> 14;
			g2 = (B * l1) >> 15;
			h1 = ((A + (B >> 1)) * l2) >> 14;
			h2 = (B * l2) >> 15;
		}
		nosc = mix ? 8 : 4;                     /* (OSC 2 silent: not rendered) */
		if (mix >= 127)
			nosc = -8;                          /* (OSC 1 silent: OSC 2 alone) */
		gs = sub * 820;                         /* the sub: at 10, half full scale */
		dg = 2048 + drive * 700;                /* DRIVE: x 0.5 (clean) to x 2.2 into the soft clip (Q12) */
		og = 9830;                              /* x 2.4 to 16 bits (the limiter takes the peaks) */
		if (chorus)
			og = og * 3 >> 2;                   /* (the chorus adds its line to the dry sound) */
		sinc = (v->nv ? v->inc[0] : 0) >> 1;
		srinc = (1u << 30) / ((sinc >> 8) | 1);
		{
			/* the oscillators, one at a time into four sums: OSC 1's saws and its saws pw on, OSC 2's */
			s32 s1[32], s2[32], t1[32], t2[32];
			s32 o0 = nosc < 0 ? 4 : 0, o1 = nosc < 0 ? 8 : nosc;
			for (i = 0; i < n; i++)
				s1[i] = t1[i] = 0;
			if (B)
				for (i = 0; i < n; i++)
					s2[i] = t2[i] = 0;
			for (k = o0; k < o1; k++) {
				u32 p, inc, r;
				s32 *a1, *a2;
				j = k & 3;
				if (j >= v->nv)
					continue;
				p = v->ph[k];
				inc = v->inc[k];
				r = v->rinc[k];
				a1 = k < 4 ? s1 : t1;
				a2 = k < 4 ? s2 : t2;
				if (!B)                         /* saws */
					for (i = 0; i < n; i++) {
						p += inc;
						a1[i] += saw(p, inc, r);
					}
				else                            /* and the saw pw on: their difference is the pulse */
					for (i = 0; i < n; i++) {
						p += inc;
						a1[i] += saw(p, inc, r);
						a2[i] += saw(p - pw, inc, r);
					}
				v->ph[k] = p;
			}
			/* the mix, the sub (band-limited), DRIVE (a soft clip), GRIT (fewer bits, a sample held) */
			{
				u32 sp = v->sph;
				s32 gh = v->gh, gn = v->gn, hold = 1 + (grit >> 1), mask = ~((1 << (grit + 2)) - 1);
				for (i = 0; i < n; i++) {
					s32 x = (s1[i] * g1 - s2[i] * g2 + t1[i] * h1 - t2[i] * h2) >> 12;
					sp += sinc;
					if (gs)
						x += (sqr(sp, 0x80000000u, sinc, srinc) * gs) >> 14;
					x = soft((x * dg) >> 12);
					if (grit) {
						if (--gn <= 0) {
							gh = x & mask;
							gn = hold;
						}
						x = gh;
					}
					fo[i] = chorus ? x : (x * og) >> 12;
				}
				v->sph = sp;
				v->gh = gh;
				v->gn = gn;
			}
		}
		/* the chorus: the line read at a time a triangle moves (I 0.51 Hz, II 0.86 Hz, I+II 9.8 Hz) */
		if (chorus) {
			static const u32 rate[3] = {731000, 1230000, 13960000};   /* a block's step */
			static const s16 depth[3] = {77, 91, 17};                 /* samples, either way */
			s32 tri, d, fr, w = v->dw, rd;
			s16 *dl = v->dl;
			v->lfo += rate[chorus - 1];
			tri = (s32)(v->lfo >> 16) - 32768;
			tri = (tri < 0 ? -tri : tri) - 16384;
			d = (154 << 8) + ((tri * depth[chorus - 1]) >> 6);          /* Q8 samples back */
			if (eco)
				d = d * 2 / 3;                  /* (as many milliseconds at 32 kHz) */
			fr = d & 255;
			rd = AC_DL - (d >> 8);              /* (the read's distance past the write, in the doubled line) */
			for (i = 0; i < n; i++) {          /* (the line holds half: no clipping) */
				s16 *q = dl + w + rd;
				s32 a = q[0], b = q[-1], x = fo[i], y;
				dl[w] = dl[w + AC_DL] = (s16)(x >> 1);
				y = x + 2 * (a + (((b - a) * fr) >> 8));
				fo[i] = (y * og) >> 12;
				w = (w + 1) & (AC_DL - 1);
			}
			v->dw = w;
		}
		/* the samples -> the block's 32 frames, 16 bits, limited, each twice: in one pass at either rate (eco_rs.c's
		 * resampling, its state shared with DX's, as a track plays one machine at a time); the block or two that
		 * realign at 48 kHz go through eco_out itself */
		{
			EcoRs *r = &eco_rs_[track];
			s32 *o = out;
			if (r->step == 65536 && r->t == 65536 && n == 32) {     /* 48 kHz: the samples, two late */
				s32 h2 = r->h[2], h3 = r->h[3];
				o[0] = o[1] = ac_limit(h2) << 15;
				o[2] = o[3] = ac_limit(h3) << 15;
				for (i = 0; i < 30; i++) {
					s32 y = ac_limit(fo[i]) << 15;
					o[4 + 2 * i] = o[5 + 2 * i] = y;
				}
				r->h[0] = fo[28];
				r->h[1] = fo[29];
				r->h[2] = fo[30];
				r->h[3] = fo[31];
			} else if (r->step < 65536) {   /* 32 kHz: linearly between the samples */
				s32 xb[36], *x = xb + 3;
				u32 t = r->t, st = r->step;
				xb[0] = r->h[0];
				xb[1] = r->h[1];
				xb[2] = r->h[2];
				xb[3] = r->h[3];
				for (i = 0; i < n; i++)
					x[1 + i] = fo[i];
				for (i = 0; i < 32; i++, t += st) {
					const s32 *q = x - 2 + (t >> 16);
					s32 x0 = q[0], y;
					y = ac_limit(x0 + (((q[1] - x0) * (s32)((t & 0xffff) >> 8)) >> 8)) << 15;
					o[2 * i] = o[2 * i + 1] = y;
				}
				r->t = t - ((u32)n << 16);
				r->h[0] = x[n - 3];
				r->h[1] = x[n - 2];
				r->h[2] = x[n - 1];
				r->h[3] = x[n];
			} else {
				eco_out(track, fo, n);
				for (i = 0; i < 32; i++) {
					s32 y = ac_limit(fo[i]) << 15;
					o[2 * i] = o[2 * i + 1] = y;
				}
			}
		}
	}
}

/* ac_eco(track) -> 1 while the track renders at 32 kHz (the tests) */
s32 ac_eco(s32 track)
{
	return ac_v[track].eco;
}

/* ac_notes(track, out) -> how many voices; their notes (MIDI, where they are going) into out (the tests) */
s32 ac_notes(s32 track, s32 *out)
{
	const AcV *v = &ac_v[track];
	s32 i;
	for (i = 0; i < v->nv; i++)
		out[i] = v->tgt[i];
	return v->nv;
}

/* ---- names ---------------------------------------------------------------------------------------------------- */
/* Contour's value, as the page shows it: the chord type (FREE) or its size (in a key) */
const char *ac_cname(s32 contour, const u8 *set)
{
	if (contour < 0)
		contour = 0;
	contour >>= 8;
	if (setv(set, S_SCALE) || setv(set, S_PROG))
		return key_names[(contour > 127 ? 127 : contour) * 8 / 128];
	return free_names[(contour > 127 ? 127 : contour) * 16 / 128];
}

/* the chord a track plays, named (C, Am7, F#sus4 ...) into buf */
static void ac_name(const AcV *v, char *buf)
{
	static const struct { s16 pcs; char s[6]; } q[] = {
		{0x091, ""}, {0x089, "m"}, {0x491, "7"}, {0x891, "maj7"}, {0x489, "m7"}, {0x449, "m7b5"}, {0x049, "dim"},
		{0x249, "dim7"}, {0x111, "aug"}, {0x085, "sus2"}, {0x0a1, "sus4"}, {0x291, "6"}, {0x289, "m6"},
		{0x095, "add9"}, {0x495, "9"}, {0x48d, "m9"}, {0x081, "5"}, {0x485, "7sus2"}, {0x4a1, "7sus4"},
		{0x895, "maj9"}, {0x08d, "madd9"}, {0x0c1, "sus#4"}, {0x083, "susb2"}, {0x889, "mM7"}, {0x445, "m7b5"},
		{0x44b, "m7b9"}, {0x493, "7b9"}, {0x249, "dim7"}};
	const char *r = note_names[((v->root % 12) + 12) % 12], *s = "?";
	s32 i;
	if (!v->started) {
		buf[0] = '-';
		buf[1] = '-';
		buf[2] = 0;
		return;
	}
	for (i = 0; i < (s32)(sizeof q / sizeof q[0]); i++)
		if (q[i].pcs == v->pcs) {
			s = q[i].s;
			break;
		}
	while (*r)
		*buf++ = *r++;
	while (*s)
		*buf++ = *s++;
	*buf = 0;
}

/* ---- the settings page (SETTINGS + PRESET: mm_open's kind MM_ACH, lxr_glue.inc) ----------------------------- */
typedef struct {
	u8 s, max;
	const char *name;
	const char *const *vals;                /* the values' names, or 0: a number */
} AcRow;
static const char *const n_scale[8] __attribute__((section(".lxr_ro"), aligned(4))) = {
	"FREE", "MAJOR", "MINOR", "DORIAN", "MIXOLYD", "HARM MIN", "PHRYGIAN", "LYDIAN"};
static const char *const n_notes[2] __attribute__((section(".lxr_ro"), aligned(4))) = {"SNAP", "DEGREES"};
static const char *const n_adv[4] __attribute__((section(".lxr_ro"), aligned(4))) = {"TRIG", "1 BAR", "2 BARS", "4 BARS"};
static const char *const n_deg[8] __attribute__((section(".lxr_ro"), aligned(4))) = {"I", "II", "III", "IV", "V", "VI", "VII", "END"};
static const char *const n_voice[5] __attribute__((section(".lxr_ro"), aligned(4))) = {"AUTO", "CLOSE", "OPEN", "DROP 2", "WIDE"};
static const char *const n_bass[3] __attribute__((section(".lxr_ro"), aligned(4))) = {"OFF", "-1 OCT", "-2 OCT"};
static const char *const n_chorus[4] __attribute__((section(".lxr_ro"), aligned(4))) = {"OFF", "I", "II", "I+II"};
static const char *const n_int[6] __attribute__((section(".lxr_ro"), aligned(4))) = {"UNISON", "+5TH", "+1 OCT", "-1 OCT", "+OCT+5TH", "+2 OCT"};
static const AcRow ac_rows[] __attribute__((section(".lxr_ro"), aligned(4))) = {
	{S_KEY, 11, "KEY", note_names}, {S_SCALE, 7, "SCALE", n_scale}, {S_NOTES, 1, "NOTES", n_notes},
	{S_PROG, 17, "PROG", prog_names}, {S_ADV, 3, "ADVANCE", n_adv},
	{S_U0, 7, "USER 1", n_deg}, {S_U0 + 1, 7, "USER 2", n_deg}, {S_U0 + 2, 7, "USER 3", n_deg},
	{S_U0 + 3, 7, "USER 4", n_deg}, {S_U0 + 4, 7, "USER 5", n_deg}, {S_U0 + 5, 7, "USER 6", n_deg},
	{S_U0 + 6, 7, "USER 7", n_deg}, {S_U0 + 7, 7, "USER 8", n_deg},
	{S_VOICE, 4, "VOICING", n_voice}, {S_BASS, 2, "BASS", n_bass}, {S_STRUM, 10, "STRUM", 0},
	{S_SUB, 10, "SUB", 0}, {S_CHORUS, 3, "CHORUS", n_chorus}, {S_DRIFT, 10, "DRIFT", 0},
	{S_INT, 5, "OSC2 INT", n_int}, {S_PWM, 10, "PWM RATE", 0}, {S_DRIVE, 10, "DRIVE", 0}, {S_GRIT, 10, "GRIT", 0}};
#define AC_ROWS ((s32)(sizeof ac_rows / sizeof ac_rows[0]))

static s32 am_row OVL, am_top OVL, am_edit OVL;

void ac_menu_open(void)
{
	am_row = am_top = am_edit = 0;
}

s32 ac_menu_accel(s32 knob)
{
	return knob == 1 && !am_edit ? 1 : 2;
}

/* ac_menu_turn(set, knob, clicks) -> 1: redraw. DATA moves through the rows (or, pressed, changes the value);
 * any other knob changes the selected row's value */
s32 ac_menu_turn(u8 *set, s32 knob, s32 clicks)
{
	s32 r, v;
	if (!set || !clicks)
		return 0;
	if (knob == 1 && !am_edit) {
		r = am_row + clicks;
		r = r < 0 ? 0 : (r > AC_ROWS ? AC_ROWS : r);
		if (r == am_row)
			return 0;
		am_row = r;
		if (am_row < am_top)
			am_top = am_row;
		if (am_row >= am_top + 5)
			am_top = am_row - 4;
		return 1;
	}
	if (am_row >= AC_ROWS)
		return 0;
	{
		const AcRow *w = &ac_rows[am_row];
		v = setv(set, w->s) + clicks;
		v = v < 0 ? 0 : (v > w->max ? w->max : v);
		if (v == setv(set, w->s))
			return 0;
		set[w->s] = (u8)(v + 1);
		return 1;
	}
}

/* ac_menu_press(set) -> 1: redraw. On the last row every setting back to its default; else DATA changes the value */
s32 ac_menu_press(u8 *set)
{
	s32 i;
	if (!set)
		return 0;
	if (am_row >= AC_ROWS) {
		for (i = 0; i < AC_SET; i++)
			set[i] = 0;
		am_edit = 0;
		return 1;
	}
	am_edit = !am_edit;
	return 1;
}

typedef void (*ac_rect_fn)(void *ctx, s32 x0, s32 y0, s32 x1, s32 y1, s32 colour);
typedef void (*ac_text_fn)(void *ctx, u32 font, s32 x, s32 y, s32 flags, const char *fmt, const char *s);
#define AC_RECT ((ac_rect_fn)0x40070dea)
#define AC_TEXT ((ac_text_fn)0x40071a04)

static void am_text(void *ctx, s32 x, s32 top, s32 flags, const char *s)
{
	AC_TEXT(ctx, 0x40ea14cc, x, 55 - top, flags, (const char *)0x40124b58, s);
}

static void am_box(void *ctx, s32 x0, s32 top, s32 x1, s32 bot, s32 colour)
{
	AC_RECT(ctx, x0, 63 - bot, x1, 63 - top, colour);
}

/* ac_menu_draw(ctx, set, track): the whole screen - ACHORD and the chord playing, then five rows */
void ac_menu_draw(void *ctx, const u8 *set, s32 track)
{
	char name[12], num[4];
	s32 i;
	AC_RECT(ctx, 0, 0, 127, 63, 0);
	am_text(ctx, 2, 0, 0x10, "ACHORD");
	ac_name(&ac_v[track < 0 || track > 5 ? 0 : track], name);
	am_text(ctx, 125, 0, 0x14, name);
	am_box(ctx, 0, 10, 127, 10, 1);
	if (am_top > am_row)
		am_top = am_row;
	if (am_row >= am_top + 5)
		am_top = am_row - 4;
	for (i = 0; i < 5; i++) {
		s32 r = am_top + i, top = 12 + 10 * i;
		if (r > AC_ROWS)
			break;
		if (r == AC_ROWS)
			am_text(ctx, 2, top, 0x10, "PUT BACK DEFAULTS");
		else {
			const AcRow *w = &ac_rows[r];
			s32 val = setv(set, w->s);
			const char *vs;
			if (w->vals)
				vs = w->vals[val];
			else {
				num[0] = (char)(val >= 10 ? '1' : '0' + val);
				num[1] = val >= 10 ? '0' : 0;
				num[2] = 0;
				vs = num;
			}
			am_text(ctx, 2, top, 0x10, w->name);
			am_text(ctx, 122, top, 0x14, vs);
		}
		if (r == am_row)
			am_box(ctx, am_edit ? 56 : 0, top - 1, 123, top + 8, -1);
	}
	{
		s32 all = AC_ROWS + 1, y0 = 12 + 50 * am_top / all;
		s32 y1 = 12 + 50 * (am_top + 5 < all ? am_top + 5 : all) / all - 1;
		am_box(ctx, 126, y0, 127, y1, 1);
	}
}
