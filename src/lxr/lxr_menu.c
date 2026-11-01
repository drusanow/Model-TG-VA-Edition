/*
 * LXR's voice menu: every setting of the LXR voice a track plays, as an LXR's front panel has them, on one page.
 * Part of LXR (machine 13 of the Model-TG Plaits Edition). Included by lxr.c.
 *
 * Copyright (C) 2026 the Model-TG Plaits Edition contributors
 * SPDX-License-Identifier: GPL-3.0-only (src/lxr/LICENSE, src/lxr/NOTICE.md)
 *
 * SETTINGS + PRESET on an LXR track opens it (src/model_tg.s: mm_open, kind MM_LXR; src/lxr_glue.inc). A track's
 * edits are a block of LX_ED bytes in its pattern's kit (lxr_glue.inc: lx_edp): [0] the preset they were made on,
 * + 1 (0: none), then its N_P knobs, 0..127, in P_* order. They go with the kit: saved with the pattern, copied with
 * it, cleared with the track's sound (lfo2.inc, lfocc.inc). The voice plays them when Contour is on that preset; on
 * another it plays that preset as it is, and the first edit there makes it the track's edited preset. Color, Shape
 * and Sweep still move their knobs from the edited values (64: as edited).
 *
 * The page: the voice's name and EDITED or PRESET on top, then five rows of settings, the selected one lit. DATA
 * moves through them; any other knob changes the selected one (faster turns go further); DATA's press makes DATA
 * change it too, until pressed again. The last row puts the preset back. Drawn with the stock screen calls (the
 * rectangle 0x40070dea, the text 0x40071a04, in the small font), as the System and retrig pages are.
 */
#define LX_ED 32                                 /* a track's block: the preset + 1, then N_P knobs (4-aligned) */

/* a track's knobs before the macros: its edits, if made on this preset; else the preset's */
static const u8 *lx_src(const u8 *ed, s32 pr)
{
	return ed && ed[0] == pr + 1 ? ed + 1 : lx_presets[pr];
}

/* the edits' sum, to see them change (a kit pasted, a pattern loaded, a knob turned): any one word's change shows */
static u32 lx_edsum(const u8 *ed)
{
	const u32 *w = (const u32 *)ed;
	u32 s = 0;
	s32 i;
	if (!ed)
		return 0;
	for (i = 0; i < LX_ED / 4; i++)
		s = (s << 5 | s >> 27) ^ w[i];
	return s;
}

enum { M_NUM, M_FINE, M_WAVE, M_FILT, M_MODE, M_TRANS };
typedef struct {
	u8 p, fmt, max;
	const char *name;
} Row;

#define R(p, f, m, n) { p, f, m, n }
#define NUM(p, n) R(p, M_NUM, 127, n)
static const Row rows_drum[] __attribute__((section(".lxr_ro"))) = {
	R(P_WAVE, M_WAVE, 5, "OSC WAVE"), NUM(P_COARSE, "COARSE"), R(P_FINE, M_FINE, 127, "FINE"),
	R(P_WAVE2, M_WAVE, 5, "MOD WAVE"), NUM(P_MODF1, "MOD FREQ"), NUM(P_GAIN1, "FM AMOUNT"),
	R(P_MIXMODE, M_MODE, 1, "MOD MODE"),
	NUM(P_PDEC, "PITCH DEC"), NUM(P_PAMT, "PITCH AMT"), NUM(P_PSLOPE, "PITCH SLOPE"),
	NUM(P_ATT, "ATTACK"), NUM(P_DEC, "DECAY"), NUM(P_VSLOPE, "VOL SLOPE"),
	R(P_FTYPE, M_FILT, 7, "FILTER"), NUM(P_FFREQ, "CUTOFF"), NUM(P_FRESO, "RESONANCE"), NUM(P_FDRIVE, "FILT DRIVE"),
	NUM(P_DIST, "DISTORTION"), NUM(P_DECIM, "SAMPLE RATE"),
	R(P_TWAVE, M_TRANS, 13, "TRANSIENT"), NUM(P_TVOL, "TRANS VOL"), NUM(P_TFREQ, "TRANS PITCH"),
	NUM(P_PWM, "PWM"), NUM(P_VOL, "VOLUME"),
};
static const Row rows_snare[] __attribute__((section(".lxr_ro"))) = {
	R(P_WAVE, M_WAVE, 5, "OSC WAVE"), NUM(P_COARSE, "COARSE"), R(P_FINE, M_FINE, 127, "FINE"),
	NUM(P_NOISEF, "NOISE FREQ"), NUM(P_MIX, "NOISE MIX"),
	NUM(P_PDEC, "PITCH DEC"), NUM(P_PAMT, "PITCH AMT"), NUM(P_PSLOPE, "PITCH SLOPE"),
	NUM(P_ATT, "ATTACK"), NUM(P_DEC, "DECAY"), NUM(P_VSLOPE, "VOL SLOPE"), NUM(P_REPEAT, "REPEAT"),
	R(P_FTYPE, M_FILT, 7, "FILTER"), NUM(P_FFREQ, "CUTOFF"), NUM(P_FRESO, "RESONANCE"), NUM(P_FDRIVE, "FILT DRIVE"),
	NUM(P_DIST, "DISTORTION"), NUM(P_DECIM, "SAMPLE RATE"),
	R(P_TWAVE, M_TRANS, 13, "TRANSIENT"), NUM(P_TVOL, "TRANS VOL"), NUM(P_TFREQ, "TRANS PITCH"),
	NUM(P_PWM, "PWM"), NUM(P_VOL, "VOLUME"),
};
static const Row rows_cym[] __attribute__((section(".lxr_ro"))) = {
	R(P_WAVE, M_WAVE, 5, "OSC WAVE"), NUM(P_COARSE, "COARSE"), R(P_FINE, M_FINE, 127, "FINE"),
	R(P_WAVE2, M_WAVE, 5, "MOD1 WAVE"), NUM(P_MODF1, "MOD1 FREQ"), NUM(P_GAIN1, "MOD1 GAIN"),
	R(P_WAVE3, M_WAVE, 5, "MOD2 WAVE"), NUM(P_MODF2, "MOD2 FREQ"), NUM(P_GAIN2, "MOD2 GAIN"),
	NUM(P_ATT, "ATTACK"), NUM(P_DEC, "DECAY"), NUM(P_VSLOPE, "VOL SLOPE"), NUM(P_REPEAT, "REPEAT"),
	R(P_FTYPE, M_FILT, 7, "FILTER"), NUM(P_FFREQ, "CUTOFF"), NUM(P_FRESO, "RESONANCE"), NUM(P_FDRIVE, "FILT DRIVE"),
	NUM(P_DIST, "DISTORTION"), NUM(P_DECIM, "SAMPLE RATE"),
	R(P_TWAVE, M_TRANS, 13, "TRANSIENT"), NUM(P_TVOL, "TRANS VOL"), NUM(P_TFREQ, "TRANS PITCH"),
	NUM(P_PWM, "PWM"), NUM(P_VOL, "VOLUME"),
};
static const Row rows_hat[] __attribute__((section(".lxr_ro"))) = {
	R(P_WAVE, M_WAVE, 5, "OSC WAVE"), NUM(P_COARSE, "COARSE"), R(P_FINE, M_FINE, 127, "FINE"),
	R(P_WAVE2, M_WAVE, 5, "MOD1 WAVE"), NUM(P_MODF1, "MOD1 FREQ"), NUM(P_GAIN1, "MOD1 GAIN"),
	R(P_WAVE3, M_WAVE, 5, "MOD2 WAVE"), NUM(P_MODF2, "MOD2 FREQ"), NUM(P_GAIN2, "MOD2 GAIN"),
	NUM(P_ATT, "ATTACK"), NUM(P_DEC, "DECAY"), NUM(P_DECO, "OPEN DECAY"), NUM(P_VSLOPE, "VOL SLOPE"),
	R(P_FTYPE, M_FILT, 7, "FILTER"), NUM(P_FFREQ, "CUTOFF"), NUM(P_FRESO, "RESONANCE"), NUM(P_FDRIVE, "FILT DRIVE"),
	NUM(P_DIST, "DISTORTION"), NUM(P_DECIM, "SAMPLE RATE"),
	R(P_TWAVE, M_TRANS, 13, "TRANSIENT"), NUM(P_TVOL, "TRANS VOL"), NUM(P_TFREQ, "TRANS PITCH"),
	NUM(P_PWM, "PWM"), NUM(P_VOL, "VOLUME"),
};
#undef R
#undef NUM

static const char *const s_wave[] __attribute__((section(".lxr_ro"))) = {"SINE", "TRI", "SAW", "SQUARE", "NOISE", "PWM"};
static const char *const s_filt[] __attribute__((section(".lxr_ro"))) = {"LP", "HP", "BP", "UNITY BP", "NOTCH", "PEAK",
                                                                          "LP2", "OFF"};
static const char *const s_mode[] __attribute__((section(".lxr_ro"))) = {"FM", "MIX"};
static const char *const s_trans[] __attribute__((section(".lxr_ro"))) = {     /* lxr_trans.h's order: waves 2..13 */
	"SNAP", "OFFSET", "EXPCLICK", "CLICK 2", "ATIK", "AC KICK", "RIMSHOT", "DRIP", "PEDAL CH", "808 CLAP",
	"MATTEL K", "MD 16-17", "MD 16-36", "MD 16-81"};
static const char *const s_voice[N_PRESETS] __attribute__((section(".lxr_ro"))) = {
	"LXR KICK", "LXR TOM", "LXR PERC", "LXR SNARE", "LXR CLAP", "LXR CLOSED HAT", "LXR OPEN HAT", "LXR CYMBAL"};

/* the page's place: the row selected, the first row shown, DATA changing the value (its press) */
static s32 m_row OVL, m_top OVL, m_edit OVL;
#define M_SHOW 5                                 /* rows on screen under the title */

static const Row *m_rows(s32 pr, s32 *n)
{
	switch (lx_kind[pr]) {
	case K_DRUM: *n = sizeof rows_drum / sizeof rows_drum[0]; return rows_drum;
	case K_SNARE: *n = sizeof rows_snare / sizeof rows_snare[0]; return rows_snare;
	case K_CYM: *n = sizeof rows_cym / sizeof rows_cym[0]; return rows_cym;
	default: *n = sizeof rows_hat / sizeof rows_hat[0]; return rows_hat;
	}
}

static s32 m_preset(s32 contour)
{
	return preset_of(contour < 0 ? 0 : contour);
}

/* lxr_menu_open: from the top */
void lxr_menu_open(void)
{
	m_row = m_top = m_edit = 0;
}

/* how much a click of this knob is worth: DATA moving through the rows, one; changing a value, faster turns go
 * further (0x4006f73a's acceleration) */
s32 lxr_menu_accel(s32 knob)
{
	return knob == 1 && !m_edit ? 1 : 4;
}

/* the selected row's value by clicks: the preset's knobs first copied in, if the edits are not this preset's */
static s32 m_change(u8 *ed, s32 pr, const Row *r, s32 clicks)
{
	s32 i, v;
	if (ed[0] != pr + 1) {
		const u8 *src = lx_presets[pr];
		ed[0] = 0;                          /* (the voice plays the preset meanwhile) */
		for (i = 0; i < N_P; i++)
			ed[1 + i] = src[i];
		ed[0] = (u8)(pr + 1);
	}
	v = ed[1 + r->p] + clicks;
	v = v < 0 ? 0 : (v > r->max ? r->max : v);
	if (v == ed[1 + r->p])
		return 0;
	ed[1 + r->p] = (u8)v;
	return 1;
}

/* lxr_menu_turn(ed, contour, knob, clicks) -> 1: redraw. knob: the encoder event's index (1: DATA) */
s32 lxr_menu_turn(u8 *ed, s32 contour, s32 knob, s32 clicks)
{
	s32 pr = m_preset(contour), n, r;
	const Row *rows = m_rows(pr, &n);
	if (!ed || !clicks)
		return 0;
	if (knob == 1 && !m_edit) {                 /* DATA: the next row, or the one before (the last: put back) */
		r = m_row + clicks;
		r = r < 0 ? 0 : (r > n ? n : r);
		if (r == m_row)
			return 0;
		m_row = r;
		if (m_row < m_top)
			m_top = m_row;
		if (m_row >= m_top + M_SHOW)
			m_top = m_row - M_SHOW + 1;
		return 1;
	}
	if (m_row >= n)
		return 0;
	return m_change(ed, pr, &rows[m_row], clicks);
}

/* lxr_menu_press(ed, contour) -> 1: redraw. DATA's press: on the last row the preset back; else DATA's turns change
 * the value, or move through the rows again */
s32 lxr_menu_press(u8 *ed, s32 contour)
{
	s32 pr = m_preset(contour), n, i;
	(void)m_rows(pr, &n);
	if (!ed)
		return 0;
	if (m_row >= n) {
		ed[0] = 0;                          /* (first: the voice plays the preset from now) */
		for (i = 1; i < LX_ED; i++)
			ed[i] = 0;
		m_edit = 0;
		return 1;
	}
	m_edit = !m_edit;
	return 1;
}

/* ---- the drawing -------------------------------------------------------------------------------------------- */
typedef void (*rect_fn)(void *ctx, s32 x0, s32 y0, s32 x1, s32 y1, s32 colour);
typedef void (*text_fn)(void *ctx, u32 font, s32 x, s32 y, s32 flags, const char *fmt, const char *s);
#define RECT ((rect_fn)0x40070dea)           /* y counts from the bottom; ends included; colour 0, 1, or -1: invert */
#define TEXT ((text_fn)0x40071a04)
#define FONT 0x40ea14cc                      /* the small font, 9 px a line */
#define FMT_S ((const char *)0x40124b58)     /* "%s" */
#define T_LEFT 0x10                          /* left of x, cleared behind */
#define T_RIGHT 0x14                         /* right-aligned against x, cleared behind */
#define X_VAL 122                            /* the values' right edge (the scroll bar past it) */

static void m_text(void *ctx, s32 x, s32 top, s32 flags, const char *s)
{
	TEXT(ctx, FONT, x, 55 - top, flags, FMT_S, s);  /* a 9 px line whose top is row `top`: at 55 - top */
}

static void m_box(void *ctx, s32 x0, s32 top, s32 x1, s32 bot, s32 colour)    /* rows top..bot from the top */
{
	RECT(ctx, x0, 63 - bot, x1, 63 - top, colour);
}

/* a row's value as it reads */
static const char *m_value(const Row *r, s32 v, char *buf)
{
	switch (r->fmt) {
	case M_WAVE: return s_wave[v > 5 ? 5 : v];
	case M_FILT: return s_filt[v & 7];
	case M_MODE: return s_mode[v ? 1 : 0];
	case M_TRANS: return s_trans[v > 13 ? 0 : v];
	default: break;
	}
	{
		char *p = buf;
		if (r->fmt == M_FINE) {             /* fine: centred on 64 */
			v -= 64;
			if (v > 0)
				*p++ = '+';
			else if (v < 0) {
				*p++ = '-';
				v = -v;
			}
		}
		if (v >= 100)
			*p++ = (char)('0' + v / 100);
		if (v >= 10)
			*p++ = (char)('0' + v / 10 % 10);
		*p++ = (char)('0' + v % 10);
		*p = 0;
	}
	return buf;
}

/* lxr_menu_draw(ctx, ed, contour): the whole screen */
void lxr_menu_draw(void *ctx, const u8 *ed, s32 contour)
{
	s32 pr = m_preset(contour), n, i;
	const Row *rows = m_rows(pr, &n);
	const u8 *k = lx_src(ed, pr);
	char buf[8];
	if (m_row > n) {                             /* (a voice of fewer rows than the one before) */
		m_row = n;
		m_edit = 0;
	}
	if (m_top > m_row)
		m_top = m_row;
	if (m_row >= m_top + M_SHOW)
		m_top = m_row - M_SHOW + 1;
	RECT(ctx, 0, 0, 127, 63, 0);
	m_text(ctx, 2, 0, T_LEFT, s_voice[pr]);
	m_text(ctx, 125, 0, T_RIGHT, ed && ed[0] == pr + 1 ? "EDITED" : "PRESET");
	m_box(ctx, 0, 10, 127, 10, 1);
	for (i = 0; i < M_SHOW; i++) {
		s32 r = m_top + i, top = 12 + 10 * i;
		if (r > n)
			break;
		if (r == n)
			m_text(ctx, 2, top, T_LEFT, "PUT BACK THE PRESET");
		else {
			m_text(ctx, 2, top, T_LEFT, rows[r].name);
			m_text(ctx, X_VAL, top, T_RIGHT, m_value(&rows[r], k[rows[r].p], buf));
		}
		if (r == m_row)                     /* the cursor: the row lit, or only the value while DATA changes it */
			m_box(ctx, m_edit ? 72 : 0, top - 1, X_VAL + 1, top + 8, -1);
	}
	{                                            /* the scroll bar: where the rows shown are among them all */
		s32 h = 50, all = n + 1;
		s32 y0 = 12 + h * m_top / all, y1 = 12 + h * (m_top + M_SHOW < all ? m_top + M_SHOW : all) / all - 1;
		m_box(ctx, 126, y0, 127, y1, 1);
	}
}
