/*
 * The CPU guard's 32 kHz for DX (src/dx.inc, src/guard.inc): DX renders a block of its samples - 32 at 48 kHz, 21 or 22
 * at 32 kHz - and this makes them into the block's 32 frames at 48 kHz: linearly between them (the cheapest: DX's work
 * is mostly per operator and block, so the interpolation is much of what 32 kHz saves), two samples late, as LXR's
 * resampling. (The 4-point Hermite below serves the block or two that realign at 48 kHz.) At 48 kHz the frames are the samples, two late (a copy), so a change of rate carries on
 * from the same place in the sound; back at 48 kHz the frames run a hair faster (at most 1.6%) for a block or two,
 * until they fall on the samples again.
 *
 * Copyright (C) 2026 the Model-TG Plaits Edition contributors
 * SPDX-License-Identifier: MIT (built into src/lxr/lxr_gen.s with lxr.c, which includes it)
 */
typedef struct {
	u32 t, step;                            /* the next frame's place after h[3], Q16; samples a frame */
	s32 h[4];                               /* the block before's last four samples */
} EcoRs;

static EcoRs eco_rs_[6] OVL;

#define ECO_STEP32 ((u32)(65536.0 * 32000 / 48000 + 0.5))

/* eco_n(track, eco) -> the samples DX renders this block (at most 32) */
s32 eco_n(s32 track, s32 eco)
{
	EcoRs *r = &eco_rs_[track];
	if (!r->t)
		r->t = 65536;
	if (eco)
		r->step = ECO_STEP32;
	else {
		u32 d = (65536 - r->t) >> 5;
		r->step = 65536 + d;
		if (!d)
			r->t = 65536;
	}
	return (s32)((r->t + 32 * r->step - 1) >> 16);
}

/* eco_out(track, buf, n): buf's n samples -> its 32 frames */
void eco_out(s32 track, s32 *buf, s32 n)
{
	EcoRs *r = &eco_rs_[track];
	s32 xb[32 + 4], *x = xb + 3, i;
	if (r->step == 65536 && r->t == 65536 && n == 32) {     /* 48 kHz: the frames are the samples, two late - in place */
		s32 h2 = r->h[2], h3 = r->h[3];
		r->h[0] = buf[28];
		r->h[1] = buf[29];
		r->h[2] = buf[30];
		r->h[3] = buf[31];
		for (i = 31; i >= 2; i--)
			buf[i] = buf[i - 2];
		buf[1] = h3;
		buf[0] = h2;
		return;
	}
	xb[0] = r->h[0];
	xb[1] = r->h[1];
	xb[2] = r->h[2];
	xb[3] = r->h[3];
	for (i = 0; i < n; i++)
		x[1 + i] = buf[i];
	if (r->step == 65536 && r->t == 65536) {
		for (i = 0; i < 32; i++)
			buf[i] = x[i - 1];
	} else if (r->step < 65536) {           /* 32 kHz: linear (the guard's lower rate: cheaper than the 4 points) */
		u32 t = r->t;
		for (i = 0; i < 32; i++, t += r->step) {
			s32 k = (s32)(t >> 16) - 2, fr = (s32)(t & 0xffff) >> 8;
			s32 x0 = x[k];
			buf[i] = x0 + (((x[k + 1] - x0) * fr) >> 8);
		}
		r->t = t - ((u32)n << 16);
	} else {
		u32 t = r->t;
		for (i = 0; i < 32; i++, t += r->step) {
			s32 k = (s32)(t >> 16) - 2, fr = (s32)(t & 0xffff) >> 8;   /* the place, Q8 (the sums are 18-bit) */
			s32 xm1, x0, x1, x2, c1, c2, c3;
			if (k > n - 2) {
				k = n - 2;
				fr = 255;
			}
			xm1 = x[k - 1];
			x0 = x[k];
			x1 = x[k + 1];
			x2 = x[k + 2];
			c1 = (x1 - xm1) >> 1;
			c2 = xm1 - ((5 * x0) >> 1) + 2 * x1 - (x2 >> 1);
			c3 = ((x2 - xm1) >> 1) + ((3 * (x0 - x1)) >> 1);
			buf[i] = x0 + ((((((((c3 * fr) >> 8) + c2) * fr) >> 8) + c1) * fr) >> 8);
		}
		r->t = t - ((u32)n << 16);
	}
	r->h[0] = x[n - 3];
	r->h[1] = x[n - 2];
	r->h[2] = x[n - 1];
	r->h[3] = x[n];
}
