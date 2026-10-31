/* Imported from the Erica Synths LXR-02 firmware (lxr-desktop-app, GPL-3.0-only, Copyright (C) 2012-2026
 * Erica Synths) by tools/lxr_import.py - do not edit. Part of LXR, GPL-3.0-only (src/lxr/LICENSE). */
/* the presets: the init kit's voices ("from hrtlkits_batch3"), knob values 0..127, in P_* order */
#define N_PRESETS 8
static const u8 lx_presets[N_PRESETS][N_P] __attribute__((section(".lxr_ro"))) = {
  /* KICK */ {0, 0, 0, 31, 63, 0, 70, 0, 64, 0, 26, 44, 22, 0, 21, 0, 46, 0, 0, 0, 0, 127, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* TOM  */ {0, 0, 0, 41, 63, 0, 70, 0, 64, 0, 93, 14, 10, 0, 13, 0, 11, 0, 0, 0, 0, 127, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* PERC */ {0, 0, 0, 58, 63, 0, 70, 0, 64, 0, 7, 127, 14, 0, 3, 0, 4, 0, 0, 0, 0, 127, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* SNAR */ {1, 0, 0, 45, 64, 70, 70, 64, 64, 0, 14, 81, 15, 0, 10, 0, 10, 0, 127, 61, 0, 127, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* CLAP */ {1, 0, 0, 45, 64, 70, 70, 64, 64, 0, 14, 0, 15, 11, 25, 0, 30, 4, 127, 127, 2, 70, 40, 0, 63, 127, 127, 0, 0, 0, 0},
  /* CHAT */ {0, 0, 0, 50, 63, 56, 127, 127, 127, 0, 0, 0, 0, 0, 5, 17, 6, 0, 0, 0, 1, 104, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* OHAT */ {0, 0, 0, 50, 63, 56, 127, 127, 127, 0, 0, 0, 0, 0, 5, 17, 6, 0, 0, 0, 1, 104, 0, 0, 63, 127, 127, 0, 0, 0, 0},
  /* CYMB */ {4, 0, 0, 127, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 107, 0, 0, 0, 0, 0, 1, 68, 0, 0, 63, 127, 127, 0, 0, 0, 0},
};
static const u8 lx_kind[N_PRESETS] __attribute__((section(".lxr_ro"))) = {K_DRUM, K_DRUM, K_DRUM, K_SNARE, K_SNARE, K_HAT, K_HAT, K_CYM};
static const u8 lx_open[N_PRESETS] __attribute__((section(".lxr_ro"))) = {0, 0, 0, 0, 0, 0, 1, 0};
