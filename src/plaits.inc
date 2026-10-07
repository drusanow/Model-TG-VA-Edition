| ============================================================================
| =================  PLAITS: machine 11, eight macro engines  =================
|
| See docs/PLAITS.md. A machine after Mutable Instruments Plaits (Emilie
| Gillet, MIT licence): one voice, four knobs and an engine select. Built
| exactly as the drums are (drums.inc): sampler_pre sends it to va_pre, which
| reads its four dials - here as the raw parameter words, 0..32512, for
| smooth modulation - and hands the stock dispatch a Sampler voice;
| sampler_render then calls mi_fill, into the same 64-sample, 96 kHz window,
| followed by the same tail (decimator, Amp Decay, Attack, Filter, Res).
|
|   Color   HARM  Plaits' HARMONICS     Shape  TIMB  TIMBRE
|   Sweep   MORP  MORPH                 Contour ENG  the engine, eight
|                                              steps of 16 on the dial
| Engines (each a fixed-point remake of Plaits' algorithm, not its code):
|   0 WSHP  waveshaping: a slope oscillator through Plaits' five shapers and
|           its wavefolder               HARM shaper  TIMB fold  MORP slope
|   1 FM    2-op phase modulation        HARM ratio   TIMB index MORP fdbk
|   2 GRAN  two grainlets (formants)     HARM ratio   TIMB form. MORP shape
|   3 PD    phase distortion (Plaits 1.2) HARM ratio  TIMB amount MORP asym
|   4 CHIP  hard-synced square, one arpeggio step per trig (Plaits 1.2)
|                                        HARM chord   TIMB arp   MORP sync
|   5 NOIS  clocked noise through a resonant LP-BP-HP filter
|                                        HARM type    TIMB clock MORP reso
|   6 PART  random impulses through three band-passes (dust)
|                                        HARM spread  TIMB dens. MORP reso
|   7 STRG  a Karplus-Strong string      HARM bridge/dispersion
|                                        TIMB brightness  MORP damping
| The pitch (Pitch knob and the trig's note) is Plaits' FREQUENCY; the stock
| amp envelope and Filter play its low-pass gate.
| ============================================================================

| MI_MACH (11) is set in model_tg.s
MI_LOC  = 64                      | mi_fill's locals
MI_CNT  = 56                      | locals: the sample counter
MI_TRK  = 60                      | ...and the track
MI_ST   = 128                     | state bytes per track

| ---- the four dials -> mi_p (per track: four longs, Q15, 0..32767) ---------
| From va_pre: d2 = track, a2 = trackData. Clobbers d0/a0.
mi_params:
    movel   %d2,%d0
    lsll    #4,%d0
    lea.l   mi_p,%a0
    adda.l  %d0,%a0
    mvsw    %a2@(22),%d0
    bpls    1f
    moveq   #0,%d0
1:  movel   %d0,%a0@
    mvsw    %a2@(24),%d0
    bpls    2f
    moveq   #0,%d0
2:  movel   %d0,%a0@(4)
    mvsw    %a2@(26),%d0
    bpls    3f
    moveq   #0,%d0
3:  movel   %d0,%a0@(8)
    mvsw    %a2@(28),%d0
    bpls    4f
    moveq   #0,%d0
4:  movel   %d0,%a0@(12)
    rts

| ---- MI_SIN: a Q32 phase -> d0 = its sine, +-32767 (256 entries, linear) ---
| Clobbers d1, a0.
    .macro MI_SIN ph
    movel   \ph,%d0
    clrw    %d0
    swap    %d0                   | the top 16 bits
    moveq   #0,%d1
    moveb   %d0,%d1               | the fraction, Q8
    lsrl    #8,%d0
    lea.l   mi_sin,%a0
    movel   %a0@(0,%d0:l:4),%d0   | value << 16 | step
    mulsw   %d0,%d1
    asrl    #8,%d1
    swap    %d0
    extl    %d0
    addl    %d1,%d0
    .endm

| ---- MI_RND: the LCG in \r, a new value. Clobbers \t ------------------------
    .macro MI_RND r, t
    movel   #1664525,\t
    mulul   \t,\r
    addil   #1013904223,\r
    .endm

| ---- helpers (bsr; they never touch the caller's locals) --------------------

| d0 = an increment, d1 = a ratio Q12 (<= 8.0) -> d0 = their product, held
| to 0x7fffffff. Keeps the rest.
mi_mulr:
    movel   %d2,%sp@-
    movel   %d0,%d2
    clrw    %d0
    swap    %d0
    mulul   %d1,%d0               | (inc >> 16) x r, < 2^31
    cmpil   #0x07ffffff,%d0
    blss    1f
    movel   #0x7fffffff,%d0
    bras    2f
1:  lsll    #4,%d0
    andil   #0xffff,%d2
    mulul   %d1,%d2
    lsrl    #8,%d2
    lsrl    #4,%d2
    addl    %d2,%d0
    bpls    2f
    movel   #0x7fffffff,%d0
2:  movel   %sp@+,%d2
    rts

| d0 = a note, Q8 (0 .. 143) -> d0 = its increment, Q32 at 96 kHz.
| Clobbers d1, a0.
mi_n2inc:
    movel   %d2,%sp@-
    tstl    %d0
    bpls    1f
    moveq   #0,%d0
1:  cmpil   #143*256,%d0
    blss    2f
    movel   #143*256,%d0
2:  moveq   #0,%d1
    moveb   %d0,%d1               | the fraction
    lsrl    #8,%d0                | the note
    movel   %d0,%d2
    mulu.w  #2731,%d2
    lsrl    #8,%d2
    lsrl    #7,%d2                | its octave (note / 12 for 0..143)
    movel   %d2,%a0
    mulu.w  #12,%d2
    subl    %d2,%d0               | ...and semitone
    movel   %a0,%d2
    lea.l   mi_semi,%a0
    lea.l   %a0@(0,%d0:l:4),%a0
    movel   %a0@(4),%d0
    subl    %a0@,%d0
    mulul   %d1,%d0
    lsrl    #8,%d0
    addl    %a0@,%d0
    lsll    %d2,%d0
    movel   %sp@+,%d2
    rts

| d0 = semitones, Q8, signed (-120 .. +119) -> d0 = 2^(st/12), Q14.
| Clobbers d1, a0.
mi_exp2:
    movel   %d2,%sp@-
    addil   #120*256,%d0
    bpls    1f
    moveq   #0,%d0
1:  cmpil   #239*256,%d0
    blss    2f
    movel   #239*256,%d0
2:  moveq   #0,%d1
    moveb   %d0,%d1
    lsrl    #8,%d0
    movel   %d0,%d2
    mulu.w  #2731,%d2
    lsrl    #8,%d2
    lsrl    #7,%d2                | the octave, 0..19
    movel   %d2,%a0
    mulu.w  #12,%d2
    subl    %d2,%d0
    movel   %a0,%d2
    lea.l   mi_e2,%a0
    lea.l   %a0@(0,%d0:l:2),%a0
    mvzw    %a0@(2),%d0
    movel   %d1,%sp@-
    mvzw    %a0@,%d1
    subl    %d1,%d0
    mulsl   %sp@+,%d0
    asrl    #8,%d0
    mvzw    %a0@,%d1
    addl    %d1,%d0               | 2^(semi/12), Q14
    subil   #10,%d2               | octave - 10
    bmis    3f
    lsll    %d2,%d0
    bras    4f
3:  negl    %d2
    lsrl    %d2,%d0
4:  movel   %sp@+,%d2
    rts

| d0 (u32) -> d0 = floor(sqrt(d0)). Clobbers d1.
mi_isqrt:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    moveq   #0,%d1
    movel   #0x40000000,%d2
1:  cmpl    %d0,%d2
    blss    2f
    lsrl    #2,%d2
    bras    1b
2:  tstl    %d2
    beqs    5f
    movel   %d1,%d3
    addl    %d2,%d3
    cmpl    %d3,%d0
    bcss    3f
    subl    %d3,%d0
    lsrl    #1,%d1
    addl    %d2,%d1
    bras    4f
3:  lsrl    #1,%d1
4:  lsrl    #2,%d2
    bras    2b
5:  movel   %d1,%d0
    movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| d0 = an increment -> d0 = g = tan(pi f / fs), Q14, f held to 0.2 fs.
| Clobbers d1.
mi_tan:
    movel   %d2,%sp@-
    cmpil   #0x33333333,%d0
    blss    1f
    movel   #0x33333333,%d0
1:  clrw    %d0
    swap    %d0
    movel   #205887,%d1           | pi, Q16
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0                   | x = pi f / fs, Q16
    movel   %d0,%d2
    mulul   %d2,%d2
    clrw    %d2
    swap    %d2                   | x^2
    movel   %d2,%d1
    mulul   %d0,%d1
    clrw    %d1
    swap    %d1                   | x^3
    mulul   %d1,%d2
    clrw    %d2
    swap    %d2                   | x^5
    mulu.w  #8738,%d2             | x 2/15
    clrw    %d2
    swap    %d2
    mulu.w  #21845,%d1            | x 1/3
    clrw    %d1
    swap    %d1
    addl    %d1,%d0
    addl    %d2,%d0
    lsrl    #2,%d0                | Q14
    movel   %sp@+,%d2
    rts

| d0 = an increment -> d0 = the one-pole coefficient a = x / (1 + x),
| x = 2 pi f / fs, Q15 (the inc is held to 2^30, fs / 4). Clobbers d1.
mi_onepole:
    cmpil   #0x40000000,%d0
    blss    1f
    movel   #0x40000000,%d0
1:  moveq   #17,%d1
    lsrl    %d1,%d0
    movel   #411775,%d1           | 2 pi, Q16
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0                   | x, Q15
    movel   %d0,%d1
    addil   #32768,%d1            | 1 + x
    swap    %d0
    clrw    %d0
    lsrl    #1,%d0                | x << 15
    divul   %d1,%d0
    rts

| a4@ (HARM) -> d0 = Plaits' quantized FM ratio, Q12. Clobbers d1, a0.
mi_fmratio:
    movel   %d2,%sp@-
    movel   %a4@,%d0
    moveq   #0,%d1
    moveb   %d0,%d1
    lsrl    #8,%d0                | 0..127
    lea.l   mi_fmq,%a0
    lea.l   %a0@(0,%d0:l:2),%a0
    mvzw    %a0@,%d2
    mvzw    %a0@(2),%d0
    subl    %d2,%d0
    mulsl   %d1,%d0
    asrl    #8,%d0
    addl    %d2,%d0
    movel   %sp@+,%d2
    rts

| ---- mi_fill(track, stepQ16, machine): one block into sampler_buf+32 -------
| Frame: MI_LOC bytes of locals, the ten saved registers at sp@(64), the
| arguments at sp@(108), sp@(112), sp@(116). Each engine is entered with
| a3 = the window, a4 = the dials (HARM, TIMB, MORP, ENG), a5 = this track's
| state, d3 = the note's increment, d7 = a new note this block.
mi_fill:
    lea.l   %sp@(-104),%sp
    moveml  %d2-%d7/%a2-%a5,%sp@(64)
    movel   %sp@(108),%d6         | track
    movel   %d6,%sp@(MI_TRK)
    movel   %sp@(112),%d1         | step, Q16 (1.0 = note 60)
    bnes    mf_st
    movel   #0x10000,%d1
mf_st:
    movel   %d1,%d3               | inc_note: step x 357.2031 / 2, as va_fill
    movel   #357,%d0
    mulul   %d0,%d3
    moveq   #52,%d0
    mulul   %d0,%d1
    lsrl    #8,%d1
    addl    %d1,%d3
    lsrl    #1,%d3
    movel   %d6,%d0
    lsll    #4,%d0
    lea.l   mi_p,%a4
    adda.l  %d0,%a4
    movel   %d6,%d0
    lsll    #7,%d0
    lea.l   mi_st,%a5
    adda.l  %d0,%a5
    movel   %a4@(12),%d0          | the engine: ENG / 4096
    moveq   #12,%d1
    lsrl    %d1,%d0
    lea.l   mi_eng,%a0
    cmpl    %a0@(0,%d6:l:4),%d0
    beqs    mf_same
    movel   %d0,%a0@(0,%d6:l:4)   | a new engine starts from rest
    movea.l %a5,%a1
    moveq   #31,%d1
mf_clr:
    clrl    %a1@+
    subql   #1,%d1
    bpls    mf_clr
mf_same:
    lea.l   dr_trig,%a0
    moveq   #0,%d7
    moveb   %a0@(0,%d6:l),%d7     | d7 = a new note this block
    clrb    %a0@(0,%d6:l)
    moveq   #63,%d1
    movel   %d1,%sp@(MI_CNT)
    lea.l   sampler_buf+32,%a3
    lea.l   mi_etab,%a0
    movel   %a0@(0,%d0:l:4),%a0
    jmp     %a0@

mi_out:
    moveml  %sp@(64),%d2-%d7/%a2-%a5
    lea.l   %sp@(104),%sp
    rts

    .align 2
mi_etab:
    .long   mi_ws_e, mi_fm_e, mi_gr_e, mi_pd_e, mi_ch_e, mi_ns_e, mi_pt_e, mi_sg_e

| ============================================================================
| 0 WSHP - Plaits' waveshaping engine: a slope oscillator (a triangle at
| MORP 0, nearly a ramp at 127), through Plaits' waveshapers (HARM picks and
| blends inverse-tan, inverse-sin, linear, bump, double bump) and its
| wavefolder (TIMB, gain 0.03 .. 0.49).
| locals: 0 1/pw, 4 1/(1-pw) (both 2^32 / Q16), 8 the blend, 12 the gain
| state: 0 the phase
mi_ws_e:
    movel   %a4@(8),%d0           | pw = 0.5 + 0.45 MORP, Q16
    mulu.w  #29491,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #32768,%d0
    movel   %d0,%d4
    moveq   #-1,%d1
    divul   %d4,%d1
    movel   %d1,%sp@(0)
    movel   #65536,%d0
    subl    %d4,%d0
    moveq   #-1,%d1
    divul   %d0,%d1
    movel   %d1,%sp@(4)
    movel   %a4@,%d0              | HARM x 4: the shaper and the blend
    lsll    #2,%d0
    movel   %d0,%d1
    andil   #0x7fff,%d1
    movel   %d1,%sp@(8)
    moveq   #15,%d1
    lsrl    %d1,%d0
    mulu.w  #514,%d0
    lea.l   mi_ws,%a1
    adda.l  %d0,%a1
    movel   %a4@(4),%d0           | the folder's gain: 0.03 + 0.46 TIMB, Q15
    mulu.w  #15073,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #983,%d0
    movel   %d0,%sp@(12)
    movel   %a5@,%d2
    lea.l   mi_fold,%a2
mws_l:
    addl    %d3,%d2
    movel   %d2,%d0
    clrw    %d0
    swap    %d0                   | the phase, Q16
    cmpl    %d4,%d0
    bccs    mws_dn
    mulul   %sp@(0),%d0           | rising: 0 .. 1
    clrw    %d0
    swap    %d0
    subil   #32768,%d0
    bras    mws_s
mws_dn:
    subl    %d4,%d0               | falling
    mulul   %sp@(4),%d0
    clrw    %d0
    swap    %d0
    negl    %d0
    addil   #32767,%d0
mws_s:
    muls.w  #127,%d0              | the shaper's position: 127 x + 128, Q8
    asrl    #7,%d0
    addil   #32768,%d0
    moveq   #0,%d1
    moveb   %d0,%d1
    lsrl    #8,%d0
    lea.l   %a1@(0,%d0:l:2),%a0
    mvsw    %a0@,%d0
    mvsw    %a0@(2),%d5
    subl    %d0,%d5
    mulsl   %d1,%d5
    asrl    #8,%d5
    addl    %d0,%d5               | this shaper
    mvsw    %a0@(514),%d0
    mvsw    %a0@(516),%d6
    subl    %d0,%d6
    mulsl   %d1,%d6
    asrl    #8,%d6
    addl    %d0,%d6               | the next
    subl    %d5,%d6
    mulsl   %sp@(8),%d6
    asrl    #8,%d6
    asrl    #7,%d6
    addl    %d5,%d6               | blended
    mulsl   %sp@(12),%d6          | the folder: at (x gain + 0.5) x 512
    asrl    #8,%d6
    asrl    #7,%d6
    addil   #16384,%d6
    moveq   #63,%d1
    andl    %d6,%d1
    asrl    #6,%d6
    lea.l   %a2@(0,%d6:l:2),%a0
    mvsw    %a0@,%d0
    mvsw    %a0@(2),%d5
    subl    %d0,%d5
    mulsl   %d1,%d5
    asrl    #6,%d5
    addl    %d5,%d0
    DR_OUT
    subql   #1,%sp@(MI_CNT)
    bplw    mws_l
    movel   %d2,%a5@
    braw    mi_out

| ============================================================================
| 1 FM - Plaits' FM engine: a sine carrier phase-modulated by a sine at
| Plaits' quantized ratios (HARM), index up to 2 cycles (TIMB squared),
| MORP above the middle feeds the modulator back on itself, below it
| modulates the modulator's frequency by the carrier.
| locals: 0 self feedback (0.25 fb^2), 4 frequency feedback (0.5 fb^2)
| state: 0 carrier phase, 4 modulator phase, 8 the carrier, low-passed
mi_fm_e:
    bsr     mi_fmratio
    movel   %d0,%d1
    movel   %d3,%d0
    bsr     mi_mulr
    movel   %d0,%d5               | the modulator's increment
    movel   %a4@(4),%d7
    mulu.w  %d7,%d7
    lsrl    #8,%d7
    lsrl    #7,%d7                | the index: TIMB^2 (2 cycles at 1.0)
    movel   %a4@(8),%d0           | feedback: 2 MORP - 1
    addl    %d0,%d0
    subil   #32768,%d0
    movel   %d0,%d1
    muls.w  %d1,%d1
    asrl    #8,%d1
    asrl    #7,%d1                | fb^2, Q15
    tstl    %d0
    bles    mfm_n
    asrl    #2,%d1
    movel   %d1,%sp@(0)
    clrl    %sp@(4)
    bras    mfm_g
mfm_n:
    asrl    #1,%d1
    movel   %d1,%sp@(4)
    clrl    %sp@(0)
mfm_g:
    movel   %a5@,%d2
    movel   %a5@(4),%d4
    movel   %a5@(8),%d6
mfm_l:
    movel   %d6,%d0               | the modulator: inc x (1 + prev x 0.5 fb^2)
    mulsl   %sp@(4),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    movel   %d5,%d1
    clrw    %d1
    swap    %d1
    mulsl   %d0,%d1
    asrl    #7,%d1
    asrl    #7,%d1
    addl    %d5,%d1
    addl    %d1,%d4
    movel   %d6,%d0               | ...at its phase + prev x 0.25 fb^2
    mulsl   %sp@(0),%d0
    lsll    #2,%d0
    addl    %d4,%d0
    MI_SIN  %d0
    mulsl   %d7,%d0               | x the index: a phase offset, cycles Q32
    lsll    #2,%d0
    addl    %d3,%d2
    addl    %d2,%d0
    MI_SIN  %d0                   | the carrier
    movel   %d0,%d1
    subl    %d6,%d1
    asrl    #3,%d1
    addl    %d1,%d6
    DR_OUT
    subql   #1,%sp@(MI_CNT)
    bplw    mfm_l
    movel   %d2,%a5@
    movel   %d4,%a5@(4)
    movel   %d6,%a5@(8)
    braw    mi_out

| ============================================================================
| 2 GRAN - Plaits' grain engine (its main output): two grainlets - a window
| at the note's pitch (shaped by MORP) filled with a sine at a formant -
| the first formant set by TIMB (note 24 .. 108, whatever the pitch), the
| second at HARM's ratio to it (-24 .. +24 semitones), HARM below the
| middle letting the carrier bleed through; then a DC blocker.
| locals: 0 / 4 the formants' increments, 8 2 x bleed, 12 1 / (1 + bleed),
| 16 the window's regime (0..2), 20 / 24 its constants, 28 the breakpoint,
| 32 the DC blocker's coefficient
| state: 0 carrier phase, 4 / 8 formant phases, 12 DC blocker
mi_gr_e:
    movel   %a4@(4),%d0           | formant 1: note 24 + 84 TIMB
    mulu.w  #21504,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #24*256,%d0
    bsr     mi_n2inc
    cmpil   #0x40000000,%d0
    blss    1f
    movel   #0x40000000,%d0
1:  movel   %d0,%sp@(0)
    movel   %d0,%d4
    movel   %a4@,%d0              | formant 2: x 2^((48 HARM - 24) / 12)
    mulu.w  #12288,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    subil   #24*256,%d0
    bsr     mi_exp2
    lsrl    #2,%d0                | Q12
    movel   %d0,%d1
    movel   %d4,%d0
    bsr     mi_mulr
    cmpil   #0x40000000,%d0
    blss    2f
    movel   #0x40000000,%d0
2:  movel   %d0,%sp@(4)
    movel   %a4@,%d0              | bleed: below the middle, b (2 - b)
    moveq   #0,%d1
    cmpil   #16384,%d0
    bccs    3f
    movel   #32768,%d1
    subl    %d0,%d1
    subl    %d0,%d1               | b = 1 - 2 HARM
    movel   #65536,%d0
    subl    %d1,%d0
    mulul   %d0,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
3:  movel   %d1,%d0
    addl    %d0,%d0
    movel   %d0,%sp@(8)
    addil   #32768,%d1
    movel   #0x40000000,%d0
    divul   %d1,%d0
    cmpil   #32767,%d0
    blss    4f
    movel   #32767,%d0
4:  movel   %d0,%sp@(12)
    | the window: MORP x 3 -> regime and t
    movel   %a4@(8),%d0
    mulu.w  #3,%d0
    movel   %d0,%d1
    andil   #0x7fff,%d1           | t
    moveq   #15,%d2
    lsrl    %d2,%d0
    movel   %d0,%sp@(16)
    movel   #32768,%d2            | u = 1 - t, u^3
    subl    %d1,%d2
    movel   %d2,%d4
    mulul   %d2,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    mulul   %d2,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    tstl    %d0
    bnes    mgr_r1
    mulu.w  #15,%d4               | regime 0: phase x (1 + 15 u^3), Q12
    lsrl    #3,%d4
    addil   #4096,%d4
    movel   %d4,%sp@(20)
    bras    mgr_dc
mgr_r1:
    subql   #1,%d0
    bnes    mgr_r2
    movel   #32702,%d0            | regime 1: a breakpoint 0.001 + 0.499 u^3
    mulul   %d0,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    addil   #66,%d4
    movel   %d4,%sp@(28)
    movel   #0x80000000,%d0       | 0.5 / bp and 0.5 / (1 - bp), Q16
    divul   %d4,%d0
    movel   %d0,%sp@(20)
    movel   #65536,%d1
    subl    %d4,%d1
    movel   #0x80000000,%d0
    divul   %d1,%d0
    movel   %d0,%sp@(24)
    bras    mgr_dc
mgr_r2:
    movel   %d1,%d4               | regime 2: 0.25 + phase x (0.5 + 14.5 t^3)
    mulul   %d1,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    mulul   %d1,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    movel   #59392,%d0
    mulul   %d0,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    addil   #2048,%d4
    movel   %d4,%sp@(20)
mgr_dc:
    movel   %d3,%d0               | the DC blocker: 0.3 f0
    clrw    %d0
    swap    %d0
    movel   #123534,%d1
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0
    movel   %d0,%sp@(32)
    movel   %a5@,%d2
    movel   %a5@(4),%d4
    movel   %a5@(8),%d5
    movel   %a5@(12),%d6
mgr_l:
    addl    %d3,%d2
    bccs    mgr_nr
    moveq   #0,%d4                | the window restarts: so do the formants
    moveq   #0,%d5
    bras    mgr_w
mgr_nr:
    addl    %sp@(0),%d4
    addl    %sp@(4),%d5
mgr_w:
    movel   %d2,%d0
    clrw    %d0
    swap    %d0                   | the carrier's phase, Q16
    movel   %sp@(16),%d1
    beqs    mgr_w0
    subql   #1,%d1
    beqs    mgr_w1
    mulul   %sp@(20),%d0          | 2: 0.25 + p k, to 0.75
    lsrl    #8,%d0
    lsrl    #4,%d0
    addil   #16384,%d0
    cmpil   #49152,%d0
    blss    mgr_ws
    movel   #49152,%d0
    bras    mgr_ws
mgr_w0:
    mulul   %sp@(20),%d0          | 0: 0.75 + min(p k, 1)
    lsrl    #8,%d0
    lsrl    #4,%d0
    cmpil   #65536,%d0
    blss    1f
    movel   #65536,%d0
1:  addil   #49152,%d0
    bras    mgr_ws
mgr_w1:
    cmpl    %sp@(28),%d0          | 1: two slopes about the breakpoint
    bccs    2f
    mulul   %sp@(20),%d0
    clrw    %d0
    swap    %d0
    bras    3f
2:  subl    %sp@(28),%d0
    mulul   %sp@(24),%d0
    clrw    %d0
    swap    %d0
    addil   #32768,%d0
3:  addil   #49152,%d0
mgr_ws:
    swap    %d0                   | Q16 -> a Q32 phase (wraps)
    clrw    %d0
    MI_SIN  %d0
    addil   #32767,%d0
    asrl    #2,%d0                | the window: (sin + 1) / 4
    movel   %d0,%d7
    MI_SIN  %d4
    movea.l %d0,%a2
    MI_SIN  %d5
    addl    %a2,%d0
    addl    %sp@(8),%d0
    asrl    #1,%d0
    mulsl   %d7,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    mulsl   %sp@(12),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    subl    %d6,%d0               | the DC blocker
    movel   %d0,%d1
    mulsl   %sp@(32),%d1
    asrl    #8,%d1
    asrl    #8,%d1
    addl    %d1,%d6
    DR_OUT
    subql   #1,%sp@(MI_CNT)
    bplw    mgr_l
    movel   %d2,%a5@
    movel   %d4,%a5@(4)
    movel   %d5,%a5@(8)
    movel   %d6,%a5@(12)
    braw    mi_out

| ============================================================================
| 3 PD - Plaits 1.2's phase distortion engine: a cosine whose phase is bent
| by a triangle at HARM's quantized ratio, hard-synced to the note and
| windowed so the reset is smooth; TIMB the amount (to 8 cycles), MORP the
| triangle's and the phasor's asymmetry.
| locals: 0 1/pw, 4 1/(1-pw), 8 the asymmetry, Q15
| state: 0 master phase, 4 slave phase
mi_pd_e:
    bsr     mi_fmratio
    movel   %d0,%d1
    movel   %d3,%d0
    bsr     mi_mulr
    cmpil   #0x40000000,%d0
    blss    1f
    movel   #0x40000000,%d0
1:  movel   %d0,%d5               | the modulator: at most fs / 4
    movel   %a4@(8),%d0           | pw = 0.5 + 0.49 MORP, Q16
    mulu.w  #32113,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #32768,%d0
    movel   %d5,%d1
    clrw    %d1
    swap    %d1                   | mf, Q16
    cmpil   #16384,%d1
    bcss    2f
    movel   #32768,%d0
    bras    3f
2:  addl    %d1,%d1               | held to 2 mf .. 1 - 2 mf
    cmpl    %d1,%d0
    bccs    4f
    movel   %d1,%d0
4:  negl    %d1
    addil   #65536,%d1
    cmpl    %d1,%d0
    blss    3f
    movel   %d1,%d0
3:  movel   %d0,%d6
    moveq   #-1,%d1
    divul   %d6,%d1
    movel   %d1,%sp@(0)
    movel   #65536,%d0
    subl    %d6,%d0
    moveq   #-1,%d1
    divul   %d0,%d1
    movel   %d1,%sp@(4)
    movel   %d6,%d0               | the asymmetry: |pw - 0.5| x 2, Q15
    subil   #32768,%d0
    bpls    5f
    negl    %d0
5:  movel   %d0,%sp@(8)
    movel   %a4@(4),%d7           | the amount: 8 TIMB^2 (1 - 3.8 mf), Q16
    mulu.w  %d7,%d7
    lsrl    #8,%d7
    lsrl    #7,%d7
    movel   %d5,%d0
    clrw    %d0
    swap    %d0
    movel   #249037,%d1
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0
    movel   #65536,%d1
    subl    %d0,%d1
    bpls    6f
    moveq   #0,%d1
6:  mulul   %d1,%d7
    lsrl    #8,%d7
    lsrl    #4,%d7
    movel   %a5@,%d2
    movel   %a5@(4),%d4
mpd_l:
    addl    %d3,%d2
    bccs    1f
    moveq   #0,%d4                | the master wrapped: the slave restarts
    bras    2f
1:  addl    %d5,%d4
2:  movel   %d4,%d0               | the slave's triangle, Q16
    clrw    %d0
    swap    %d0
    cmpl    %d6,%d0
    bccs    3f
    mulul   %sp@(0),%d0
    clrw    %d0
    swap    %d0
    bras    4f
3:  subl    %d6,%d0
    mulul   %sp@(4),%d0
    clrw    %d0
    swap    %d0
    negl    %d0
    addil   #65535,%d0
4:  movea.l %d0,%a2
    movel   %d2,%d0               | the window: w = 4 (1 - m) m; w (2 - w)
    clrw    %d0
    swap    %d0
    movea.l %d0,%a1               | m, Q16
    movel   %d0,%d1
    negl    %d1
    addil   #65536,%d1
    mulul   %d0,%d1
    lsrl    #7,%d1
    lsrl    #7,%d1                | w, Q16
    movel   %d1,%d0
    lsrl    #1,%d0
    negl    %d1
    addil   #131072,%d1
    mulul   %d1,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0                | the window, Q16
    movel   %a2,%d1
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0
    mulul   %d7,%d0               | x the amount: cycles, Q32 (wraps)
    movea.l %d0,%a2
    movel   %a1,%d0               | the phasor: m + (m^4 - m) x asymmetry
    movel   %d0,%d1
    mulul   %d1,%d1
    clrw    %d1
    swap    %d1
    mulul   %d1,%d1
    clrw    %d1
    swap    %d1
    subl    %d0,%d1
    mulsl   %sp@(8),%d1
    asrl    #8,%d1
    asrl    #7,%d1
    addl    %d1,%d0
    swap    %d0
    clrw    %d0
    addl    %a2,%d0
    addil   #0x40000000,%d0       | + 0.25: a cosine
    MI_SIN  %d0
    DR_OUT
    subql   #1,%sp@(MI_CNT)
    bplw    mpd_l
    movel   %d2,%a5@
    movel   %d4,%a5@(4)
    braw    mi_out

| ============================================================================
| 4 CHIP - Plaits 1.2's chiptune engine, as Plaits plays it when triggered:
| each trig takes the next note of an arpeggio over Plaits' eleven chords
| (HARM), in one of its twelve patterns (TIMB: up, down, up-down, random
| over 1, 2 or 4 octaves), on a square hard-synced to the note - MORP the
| synced square's ratio (0.51 .. 1, then up to ~4.9).
| state: 0 master phase, 4 slave phase, 8 note, 12 octave, 16 direction
| (0 up, 1 down), 20 this note's chord ratio (Q12; 0 = none yet)
mi_ch_e:
    tstl    %d7
    beqw    mch_set
    movel   %a4@,%d0              | the chord
    mulu.w  #11,%d0
    moveq   #15,%d1
    lsrl    %d1,%d0
    movel   %d0,%sp@(0)
    lea.l   mi_chn,%a0
    moveq   #0,%d4
    moveb   %a0@(0,%d0:l),%d4     | d4 = its notes
    movel   %a4@(4),%d0           | the pattern, 0..11
    mulu.w  #12,%d0
    lsrl    %d1,%d0
    movel   %d0,%d5
    mulu.w  #21846,%d5
    clrw    %d5
    swap    %d5                   | d5 = mode: pattern / 3
    movel   %d5,%d1
    mulu.w  #3,%d1
    subl    %d1,%d0
    moveq   #1,%d6
    lsll    %d0,%d6               | d6 = range: 1 << pattern % 3
    cmpil   #1,%d4
    bnes    mck_1
    cmpil   #1,%d6
    bnes    mck_1
    clrl    %a5@(8)
    clrl    %a5@(12)
    braw    mck_done
mck_1:
    cmpil   #3,%d5
    bnes    mck_seq
mck_rnd:                          | random: a different note or octave
    movel   mi_seed,%d0
    MI_RND  %d0,%d1
    movel   %d0,mi_seed
    movel   %d0,%d1
    lsrl    #4,%d1
    movel   %d6,%d2
    subql   #1,%d2
    andl    %d2,%d1               | octave
    moveq   #20,%d2
    lsrl    %d2,%d0
    mulu.w  %d4,%d0
    lsrl    #8,%d0
    lsrl    #4,%d0                | note
    cmpl    %a5@(12),%d1
    bnes    1f
    cmpl    %a5@(8),%d0
    beqs    mck_rnd
1:  movel   %d1,%a5@(12)
    movel   %d0,%a5@(8)
    braw    mck_done
mck_seq:
    tstl    %d5                   | up / down set the direction
    bnes    1f
    clrl    %a5@(16)
1:  cmpil   #1,%d5
    bnes    2f
    moveq   #1,%d0
    movel   %d0,%a5@(16)
2:  movel   %a5@(8),%d0           | d0 = note, d1 = octave, d2 = +-1
    movel   %a5@(12),%d1
    moveq   #1,%d2
    tstl    %a5@(16)
    beqs    3f
    moveq   #-1,%d2
3:  addl    %d2,%d0
mck_lp:
    tstl    %d0                   | past either end of the chord: next octave
    bmis    4f
    cmpl    %d4,%d0
    blts    5f
4:  addl    %d2,%d1
    moveq   #0,%d0
    tstl    %d2
    bpls    5f
    movel   %d4,%d0
    subql   #1,%d0
5:  tstl    %d1                   | past either end of the range
    bmis    6f
    cmpl    %d6,%d1
    blts    mck_st
6:  moveq   #0,%d1
    tstl    %d2
    bpls    7f
    movel   %d6,%d1
    subql   #1,%d1
7:  cmpil   #2,%d5
    bnes    mck_st
    negl    %d2                   | up-down turns around
    moveq   #0,%d0
    tstl    %d2
    bmis    8f
    clrl    %a5@(16)
    moveq   #1,%d0
    moveq   #0,%d1
    bras    mck_lp
8:  moveq   #1,%d0
    movel   %d0,%a5@(16)
    movel   %d4,%d0
    subql   #2,%d0
    movel   %d6,%d1
    subql   #1,%d1
    bras    mck_lp
mck_st:
    movel   %d0,%a5@(8)
    movel   %d1,%a5@(12)
mck_done:
    movel   %sp@(0),%d0           | the note's ratio
    lsll    #2,%d0
    addl    %a5@(8),%d0
    lea.l   mi_chord,%a0
    mvzw    %a0@(0,%d0:l:2),%d0
    movel   %d0,%a5@(20)
mch_set:
    movel   %a5@(20),%d1
    bnes    1f
    movel   #4096,%d1             | no trig yet: the root
    clrl    %a5@(12)
1:  movel   %d3,%d0
    bsr     mi_mulr
    movel   %a5@(12),%d1          | x 2^octave, held to fs / 4
    movel   #0x40000000,%d2
    lsrl    %d1,%d2
    cmpl    %d2,%d0
    blss    2f
    movel   %d2,%d0
2:  lsll    %d1,%d0
    movel   %d0,%d5               | the master
    movel   %a4@(8),%d0           | the slave's ratio, Q12
    mulu.w  #32604,%d0            | x 0.995
    lsrl    #8,%d0
    lsrl    #7,%d0
    cmpil   #16384,%d0
    bccs    3f
    mulu.w  #4014,%d0             | 0.51 + 0.98 s
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #2089,%d0
    bras    4f
3:  subil   #16384,%d0            | 1 + 16 (s - 0.5)^2
    mulu.w  %d0,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addl    %d0,%d0
    addil   #4096,%d0
4:  movel   %d0,%d1
    movel   %d5,%d0
    bsr     mi_mulr
    movel   %d0,%d6               | the slave
    movel   %a5@,%d2
    movel   %a5@(4),%d4
    movel   #24000,%d7
mch_l:
    addl    %d5,%d2
    bccs    1f
    moveq   #0,%d4                | hard sync
    bras    2f
1:  addl    %d6,%d4
2:  movel   %d7,%d0
    tstl    %d4
    bmis    3f
    negl    %d0
3:  lsll    #8,%d0
    lsll    #7,%d0
    movel   %d0,%a3@+
    subql   #1,%sp@(MI_CNT)
    bplw    mch_l
    movel   %d2,%a5@
    movel   %d4,%a5@(4)
    braw    mi_out

| ============================================================================
| The three engines below run at 48 kHz - the rate the stock decimator brings
| every render down to - and write each sample twice: half the work, and
| their filters' coefficients twice as large, so the integer states keep
| their precision at low cutoffs.

| MI_CLAMP r, lim: \r held to +-\lim
    .macro MI_CLAMP r, lim
    cmpil   #\lim,\r
    bles    .Lca\@
    movel   #\lim,\r
.Lca\@:
    cmpil   #-\lim,\r
    bges    .Lcb\@
    movel   #-\lim,\r
.Lcb\@:
    .endm

| MI_OUT2: d0 (Q15) -> held, << 15, written twice
    .macro MI_OUT2
    MI_CLAMP %d0, 32767
    lsll    #8,%d0
    lsll    #7,%d0
    movel   %d0,%a3@+
    movel   %d0,%a3@+
    .endm

| MI_SVF pre, post: one sample of a state-variable filter (Plaits' / stmlib's
| topology) with its coefficients and states at a2: 0 g (Q14), 4 g h (Q14),
| 8 r + g (Q12), 12 s1, 16 s2 (Q16, held to +-100000). d0 = x (Q16) ->
| d1 = band-pass, d0 = low-pass, a1 = x - (r + g) s1 - s2 (hp = this x h).
| \pre / \post split g h's shift (14) for headroom. Clobbers a0.
    .macro MI_SVF pre, post
    movel   %a2@(12),%d1
    mulsl   %a2@(8),%d1
    asrl    #8,%d1
    asrl    #4,%d1                | (r + g) s1
    addl    %a2@(16),%d1
    subl    %d1,%d0
    movea.l %d0,%a1
    asrl    #\pre,%d0
    mulsl   %a2@(4),%d0
    addil   #1<<(\post-1),%d0
    asrl    #8,%d0
    asrl    #(\post-8),%d0        | g hp, rounded
    movel   %d0,%d1
    addl    %a2@(12),%d1          | bp = g hp + s1
    addl    %d1,%d0
    MI_CLAMP %d0, 100000
    movel   %d0,%a2@(12)          | s1 = g hp + bp
    movel   %d1,%d0
    asrl    #1,%d0
    mulsl   %a2@,%d0
    addil   #4096,%d0
    asrl    #8,%d0
    asrl    #5,%d0                | g bp, rounded
    movea.l %d0,%a0
    addl    %a2@(16),%d0          | lp = g bp + s2
    adda.l  %d0,%a0
    movel   %d0,%sp@-
    movel   %a0,%d0
    MI_CLAMP %d0, 100000
    movel   %d0,%a2@(16)          | s2 = g bp + lp
    movel   %sp@+,%d0
    .endm

| d0 = p (Q16, < 1), d1 = w (Q16, <= 1.5) -> d0 = atan(p sin w / (1 - p cos w)),
| Q14 - a one-pole's phase at w (with w small: sin w = w, cos w = 1 - w^2/2).
| atan(u) = u / (1 + 0.28 u^2) for u <= 1, pi/2 - 1 / (u + 0.28 / u) above.
| Clobbers d1.
mi_atanp:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    movel   %d0,%d2               | p
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0                   | p w
    movel   %d1,%d3               | 1 - p + p w^2 / 2
    mulul   %d3,%d3
    clrw    %d3
    swap    %d3
    lsrl    #2,%d3
    movel   %d2,%d1
    lsrl    #2,%d1
    mulul   %d1,%d3
    moveq   #13,%d1
    lsrl    %d1,%d3
    movel   #65536,%d1
    subl    %d2,%d1
    addl    %d1,%d3
    bnes    1f
    moveq   #1,%d3
1:  moveq   #14,%d1               | u, Q14
    lsll    %d1,%d0
    divul   %d3,%d0
    cmpil   #16384,%d0
    bhis    2f
    movel   %d0,%d3
    mulul   %d3,%d3
    lsrl    %d1,%d3
    mulu.w  #4588,%d3
    lsrl    %d1,%d3
    addil   #16384,%d3
    lsll    %d1,%d0
    divul   %d3,%d0
    bras    3f
2:  movel   #4588,%d3
    lsll    %d1,%d3
    divul   %d0,%d3
    addl    %d0,%d3
    movel   #0x10000000,%d0
    divul   %d3,%d0
    negl    %d0
    addil   #25736,%d0
3:  movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| d0 = g, d1 = r (both Q14), a1 = a filter's block -> its g, g h and r + g
| written (MI_SVF's layout); d0 = h, Q14. Clobbers d1.
mi_svfset:
    movel   %d2,%sp@-
    movel   %d0,%a1@
    movel   %d1,%d2
    addl    %d0,%d2
    lsrl    #2,%d2
    movel   %d2,%a1@(8)           | r + g, Q12
    mulul   %d0,%d1
    moveq   #14,%d2
    lsrl    %d2,%d1               | r g
    movel   %d0,%d2
    mulul   %d2,%d2
    lsrl    #8,%d2
    lsrl    #6,%d2                | g^2
    addl    %d2,%d1
    addil   #16384,%d1
    movel   #0x10000000,%d2
    divul   %d1,%d2               | h = 1 / (1 + r g + g^2)
    movel   %d0,%d1
    mulul   %d2,%d1
    lsrl    #8,%d1
    lsrl    #6,%d1
    movel   %d1,%a1@(4)           | g h
    movel   %d2,%d0
    movel   %sp@+,%d2
    rts

| ============================================================================
| 5 NOIS - Plaits' noise engine (its main output): white noise sampled and
| held at a clock (TIMB, note 0 .. 128; raw noise blends in at the top),
| through a state-variable filter at the note's pitch whose type HARM
| sweeps low-pass - band-pass - high-pass, resonance MORP (Q 0.5 .. 512),
| with Plaits' gain compensation. 48 kHz.
| locals: 0 h, 12 the input gain (Q12), 16 the clock, 20 the raw noise's
| share, 24/28/32 the LP/BP/HP weights (Q14), 36.. the filter (MI_SVF)
| state: 0 s1, 4 s2, 8 the held sample, 12 the clock's phase
mi_ns_e:
    moveq   #31,%d0
    movel   %d0,%sp@(MI_CNT)
    movel   %a4@(8),%d0           | r = 1 / q = 2 x 2^(-10 MORP)
    mulu.w  #30720,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    negl    %d0
    bsr     mi_exp2
    addl    %d0,%d0               | Q14
    movel   %d0,%d5
    movel   %d3,%d0               | g at the note, at 48 kHz
    addl    %d0,%d0
    bsr     mi_tan
    movel   %d5,%d1
    lea.l   %sp@(36),%a1
    bsr     mi_svfset
    movel   %d0,%sp@(0)           | h
    | the gain: 1 / sqrt((0.5 + q) x 40 f0), f0 normalized to 48 kHz
    movel   #262144,%d0           | (0.5 + q) Q4 = 2^18 / r + 8
    divul   %d5,%d0
    addql   #8,%d0
    movel   %d3,%d1               | 80 x inc / 2^32, Q12
    clrw    %d1
    swap    %d1
    mulu.w  #80,%d1
    lsrl    #4,%d1
    bnes    1f
    moveq   #1,%d1
1:  movel   #0xffffffff,%d2
    divul   %d1,%d2
    cmpl    %d2,%d0
    blss    2f
    movel   %d2,%d0
2:  mulul   %d1,%d0               | Y, Q16
    bsr     mi_isqrt              | Q8
    tstl    %d0
    bnes    3f
    moveq   #1,%d0
3:  movel   #0x100000,%d1         | 1 / sqrt(Y), Q12, at most 4
    divul   %d0,%d1
    cmpil   #16384,%d1
    blss    4f
    movel   #16384,%d1
4:  movel   %d1,%sp@(12)
    movel   %a4@(4),%d0           | the clock: note 128 TIMB (Q8 = TIMB)
    bsr     mi_n2inc
    movel   %d0,%d1
    addl    %d1,%d1               | at 48 kHz
    movel   %d1,%sp@(16)
    clrw    %d0                   | raw noise: 4 (f48 - 0.25), 0..1
    swap    %d0
    subil   #8192,%d0
    bpls    5f
    moveq   #0,%d0
5:  lsll    #2,%d0
    cmpil   #32767,%d0
    blss    6f
    movel   #32767,%d0
6:  movel   %d0,%sp@(20)
    movel   %a4@,%d0              | the weights
    cmpil   #16384,%d0
    bccs    7f
    movel   #16384,%d1
    subl    %d0,%d1
    movel   %d1,%sp@(24)
    movel   %d0,%sp@(28)
    clrl    %sp@(32)
    bras    8f
7:  clrl    %sp@(24)
    movel   #32768,%d1
    subl    %d0,%d1
    movel   %d1,%sp@(28)
    subil   #16384,%d0
    movel   %d0,%sp@(32)
8:  movel   %a5@,%sp@(48)
    movel   %a5@(4),%sp@(52)
    movel   %a5@(8),%d5
    movel   %a5@(12),%d6
    movel   mi_seed,%d7
    lea.l   %sp@(36),%a2
mns_l:
    MI_RND  %d7,%d1
    movel   %d7,%d0
    swap    %d0
    extl    %d0                   | white, Q15
    addl    %sp@(16),%d6
    bccs    1f
    movel   %d0,%d5               | the clock: a new sample
1:  subl    %d5,%d0
    mulsl   %sp@(20),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    addl    %d5,%d0
    mulsl   %sp@(12),%d0          | x the gain -> Q16
    asrl    #8,%d0
    asrl    #3,%d0
    MI_CLAMP %d0, 131071
    MI_SVF  2, 12
    asrl    #2,%d0                | lp wl + bp wb + hp wh
    mulsl   %sp@(24),%d0
    asrl    #2,%d1
    mulsl   %sp@(28),%d1
    addl    %d1,%d0
    movel   %a1,%d1               | hp = (x - (r + g) s1 - s2) h
    asrl    #3,%d1
    mulsl   %sp@(0),%d1
    asrl    #8,%d1
    asrl    #3,%d1
    asrl    #2,%d1
    mulsl   %sp@(32),%d1
    addl    %d1,%d0
    asrl    #8,%d0                | Q15
    asrl    #5,%d0
    MI_OUT2
    subql   #1,%sp@(MI_CNT)
    bplw    mns_l
    movel   %d7,mi_seed
    movel   %sp@(48),%a5@
    movel   %sp@(52),%a5@(4)
    movel   %d5,%a5@(8)
    movel   %d6,%a5@(12)
    braw    mi_out

| ============================================================================
| 6 PART - Plaits' particle engine: random impulses (their density TIMB:
| note 60 .. 132 squared) each ringing one of three band-pass filters, each
| filter retuned at random about the note when it is struck (spread HARM,
| up to +-48 semitones), resonance MORP above the middle (Q to ~1000; below
| it Q 1.5), with Plaits' gain compensation; then a low-pass at the note
| (one pole, where Plaits' has two). 48 kHz.
| locals: 0 r (Q14), 4 the event threshold (Q32), 8 the input gain (Q12),
| 12 the spread (semitones Q8), 16 a block mask of retuned filters, 20 this
| sample's impulse (Q16), 24 the low-pass's coefficient (Q15)
| state: three filter blocks (MI_SVF's, 20 bytes) at 0/20/40; 60 the
| low-pass
mi_pt_e:
    moveq   #31,%d0
    movel   %d0,%sp@(MI_CNT)
    movel   %a4@(8),%d0           | q
    cmpil   #16384,%d0
    bccs    1f
    movel   #10923,%d0            | r for q 1.5
    bras    2f
1:  subil   #16384,%d0            | q = 0.5 + qs^2, qs = 2^(120 (M - .5) / 12)
    mulu.w  #30720,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    bsr     mi_exp2
    lsrl    #6,%d0                | Q8
    mulul   %d0,%d0
    lsrl    #8,%d0
    addil   #128,%d0
    movel   %d0,%d1
    movel   #4194304,%d0          | r = 1 / q, Q14
    divul   %d1,%d0
2:  movel   %d0,%sp@(0)
    movel   %a4@(4),%d0           | ds = f(note 60 + 72 TIMB^2)
    mulu.w  %d0,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    mulu.w  #18432,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #60*256,%d0
    bsr     mi_n2inc
    movel   %d0,%d4               | as an increment at 96 kHz
    moveq   #15,%d1
    lsrl    %d1,%d0               | ds, Q16 of 48 kHz
    mulul   %d0,%d0               | events a 48 kHz sample: ds^2, Q32
    movel   %d0,%sp@(4)
    | the gain, Plaits': 0.5 / sqrt(q f0 sqrt(ds^2 / 6)), f0 and ds at 48k
    movel   %sp@(0),%d0           | sqrt(r), Q14
    moveq   #14,%d1
    lsll    %d1,%d0
    bsr     mi_isqrt
    movel   %d0,%d5
    movel   %d3,%d0               | 1 / sqrt(f0), Q8
    addl    %d0,%d0
    bsr     mi_isqrt
    tstl    %d0
    bnes    3f
    moveq   #1,%d0
3:  movel   #0x1000000,%d1
    divul   %d0,%d1
    mulul   %d1,%d5
    moveq   #14,%d1
    lsrl    %d1,%d5               | sqrt(r) / sqrt(f0), Q8
    movel   %d4,%d0               | 1 / sqrt(ds), Q8
    addl    %d0,%d0
    bsr     mi_isqrt
    tstl    %d0
    bnes    4f
    moveq   #1,%d0
4:  movel   #0x1000000,%d1
    divul   %d0,%d1
    mulul   %d1,%d5
    lsrl    #8,%d5                | Q8
    movel   %d5,%d0
    cmpil   #5234,%d0             | (the gain held to 16)
    blss    5f
    movel   #5234,%d0
5:  mulu.w  #3205,%d0             | x 0.5 x 6^(1/4), -> Q12
    lsrl    #8,%d0
    movel   %d0,%sp@(8)
    movel   %a4@,%d0              | the spread: 48 HARM^2 semitones, Q8
    mulu.w  %d0,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    mulu.w  #12288,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    movel   %d0,%sp@(12)
    clrl    %sp@(16)
    movel   %d3,%d0               | the low-pass at the note (one pole)
    addl    %d0,%d0
    bsr     mi_onepole
    movel   %d0,%sp@(24)
    movel   mi_seed,%d7
mpt_l:
    moveq   #0,%d6                | the filter struck (x 20, +1; 0 = none)
    MI_RND  %d7,%d1
    cmpl    %sp@(4),%d7
    bccw    mpt_g
    MI_RND  %d7,%d1               | an event
    movel   %d7,%d6
    clrw    %d6
    swap    %d6
    mulu.w  #3,%d6
    clrw    %d6
    swap    %d6                   | the filter: 0..2
    movel   %d6,%d0
    mulu.w  #20,%d0
    lea.l   %a5@(0,%d0:l),%a2
    moveq   #1,%d0
    lsll    %d6,%d0
    movel   %sp@(16),%d1
    andl    %d0,%d1
    bnew    mpt_v
    orl     %d0,%sp@(16)          | its first this block: retune it
    MI_RND  %d7,%d1
    movel   %d7,%d0
    swap    %d0
    extl    %d0                   | u, -1..1
    mulsl   %sp@(12),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    bsr     mi_exp2
    lsrl    #2,%d0
    cmpil   #32768,%d0
    blss    1f
    movel   #32768,%d0
1:  movel   %d0,%d1
    movel   %d3,%d0
    bsr     mi_mulr
    cmpil   #0x20000000,%d0
    blss    2f
    movel   #0x20000000,%d0
2:  addl    %d0,%d0               | at 48 kHz
    bsr     mi_tan
    movel   %sp@(0),%d1
    movea.l %a2,%a1
    bsr     mi_svfset
mpt_v:
    movel   %d7,%d0               | the amplitude: uniform, x the gain
    andil   #0x7fff,%d0
    mulul   %sp@(8),%d0
    lsrl    #8,%d0
    lsrl    #3,%d0                | Q16
    movel   %d0,%sp@(20)
    mulu.w  #20,%d6
    addql   #1,%d6
mpt_g:
    moveq   #0,%d5                | the three band-passes, summed
    movea.l %a5,%a2
    moveq   #2,%d4
mpt_b:
    moveq   #0,%d0
    movel   %a2,%d1
    subl    %a5,%d1
    addql   #1,%d1
    cmpl    %d6,%d1
    bnes    1f
    movel   %sp@(20),%d0          | the impulse, into its filter
1:  MI_SVF  3, 11
    addl    %d1,%d5
    lea.l   %a2@(20),%a2
    subql   #1,%d4
    bplw    mpt_b
    movel   %d5,%d0               | the low-pass at the note
    MI_CLAMP %d0, 131071
    subl    %a5@(60),%d0
    asrl    #2,%d0
    mulsl   %sp@(24),%d0
    addil   #4096,%d0
    asrl    #8,%d0
    asrl    #5,%d0
    addl    %a5@(60),%d0
    movel   %d0,%a5@(60)
    asrl    #1,%d0                | Q15
    MI_OUT2
    subql   #1,%sp@(MI_CNT)
    bplw    mpt_l
    movel   %d7,mi_seed
    braw    mi_out

| ============================================================================
| 7 STRG - Plaits' string engine, one string: a Karplus-Strong delay line
| one period long (from ~23 Hz), struck on each trig by a period of noise
| low-passed by the brightness (TIMB^2), with a one-pole damping filter
| whose cutoff rises with MORP and the brightness (MORP 0.95 .. 1: no
| decay). HARM below 0.24 curves the bridge (a buzz, sitar-like), above
| 0.26 adds dispersion (stiffness), clean between. 48 kHz. The damping
| filter's phase delay at the note is taken off the line's length, so it
| stays in tune at every setting.
| locals: 0 the read delay (Q8), 4 damping a (Q15), 8 excitation a (Q15),
| 12 the bridge's depth (Q15), 16 the allpass coefficient (Q15), 20 the trig
| state: 0 write position, 4 damping state, 8 / 12 DC blocker x1 / y1 x 256,
| 16 noise left, 20 excitation state, 24 / 28 allpass x1 / y1,
| 32 the bridge's curve
mi_sg_e:
    moveq   #31,%d0
    movel   %d0,%sp@(MI_CNT)
    movel   %d7,%sp@(20)          | the trig, kept (d7 is borrowed below)
    | this track's delay line: Pluck's (DLY_BASE + 8 KiB x track), zeroed
    | before its first use and whenever Pluck has had it since
    movel   %sp@(MI_TRK),%d6
    movel   %d6,%d0
    moveq   #13,%d1
    lsll    %d1,%d0
    addil   #DLY_BASE,%d0
    movea.l %d0,%a2
    lea.l   pk_init,%a0
    tstb    %a0@(0,%d6:l)
    bnes    msg_lz
    lea.l   mi_dz,%a1
    tstb    %a1@(0,%d6:l)
    bnes    msg_lok
msg_lz:
    clrb    %a0@(0,%d6:l)
    lea.l   mi_dz,%a1
    moveq   #1,%d0
    moveb   %d0,%a1@(0,%d6:l)
    movea.l %a2,%a0
    moveq   #0,%d0
    moveq   #0,%d1
    moveq   #0,%d2
    moveq   #0,%d4
    movel   #511,%d5
1:  moveml  %d0-%d2/%d4,%a0@
    lea.l   %a0@(16),%a0
    subql   #1,%d5
    bpls    1b
msg_lok:
    | the period: 2^32 / inc samples at 48 kHz, Q8, from 4 to 2040
    movel   %d3,%d1
    addl    %d1,%d1
    lsrl    #8,%d1
    bnes    1f
    moveq   #1,%d1
1:  moveq   #-1,%d4
    divul   %d1,%d4
    cmpil   #2040*256,%d4
    blss    2f
    movel   #2040*256,%d4
2:  cmpil   #4*256,%d4
    bccs    3f
    movel   #4*256,%d4
3:  | damping: cutoff = f0 x 2^((12 + 60 M^2 + 24 B) / 12), B = TIMB^2
    movel   %a4@(4),%d6
    mulu.w  %d6,%d6
    lsrl    #8,%d6
    lsrl    #7,%d6                | B, Q15
    movel   %a4@(8),%d5           | M
    movel   %d5,%d0
    mulu.w  %d0,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    mulu.w  #15360,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    movel   %d6,%d1
    mulu.w  #6144,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
    addl    %d1,%d0
    addil   #12*256,%d0
    cmpil   #84*256,%d0
    blss    4f
    movel   #84*256,%d0
4:  subil   #31130,%d5            | M >= 0.95: toward no damping
    blss    5f
    mulu.w  #20,%d5               | x, Q15
    cmpil   #32768,%d5
    blss    6f
    movel   #32768,%d5
6:  movel   #128*256,%d1
    subl    %d0,%d1
    mulul   %d5,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
    addl    %d1,%d0
5:  bsr     mi_exp2               | the cutoff's ratio, Q14 (up to ~2^10)
    movel   %d3,%d1               | f0 (at 48 kHz) x ratio, in steps of 8
    addl    %d1,%d1
7:  cmpil   #131072,%d0
    blss    8f
    lsrl    #3,%d0
    lsll    #3,%d1
    cmpil   #0x08000000,%d1
    blss    7b
    movel   #0x40000000,%d1
    moveq   #0,%d0
8:  tstl    %d0
    beqs    9f
    lsrl    #2,%d0
    movel   %d0,%sp@-
    movel   %d1,%d0
    movel   %sp@+,%d1
    bsr     mi_mulr
    bras    10f
9:  movel   %d1,%d0
10: bsr     mi_onepole
    tstl    %d5
    bles    11f
    movel   #32768,%d1            | ...and toward a = 1
    subl    %d0,%d1
    mulul   %d5,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
    addl    %d1,%d0
11: tstl    %d0
    bnes    12f
    moveq   #1,%d0
12: movel   %d0,%sp@(4)
    | the damping low-pass's phase delay at the note, atan(u(1 - a)) / w
    | (w = 2 pi f0 / 48 kHz), taken off the line's length. (The DC
    | blocker's pole is close enough to 1 that its lead is a few cents.)
    movel   %d0,%d5               | a
    movel   %d3,%d1               | w, Q16, held to 1.5
    addl    %d1,%d1
    clrw    %d1
    swap    %d1
    cmpil   #15645,%d1
    blss    13f
    movel   #15645,%d1
13: movel   #411775,%d0
    mulul   %d0,%d1
    clrw    %d1
    swap    %d1
    tstl    %d1
    bnes    14f
    moveq   #1,%d1
14: movea.l %d1,%a1               | w
    movel   #32768,%d0            | p = 1 - a, Q16
    subl    %d5,%d0
    addl    %d0,%d0
    bsr     mi_atanp
    moveq   #10,%d5               | / w -> samples, Q8
    lsll    %d5,%d0
    movel   %a1,%d1
    divul   %d1,%d0
    subl    %d0,%d4
    | the bridge / dispersion
    clrl    %sp@(12)
    clrl    %sp@(16)
    movel   %a4@,%d0
    cmpil   #7864,%d0
    bccs    18f
    movel   #7864,%d1             | curved bridge: n = (0.24 - H) x 4.166
    subl    %d0,%d1
    mulu.w  #4166,%d1
    movel   #1000,%d0
    divul   %d0,%d1
    mulu.w  %d1,%d1               | depth = n^2 x 0.01, Q15
    lsrl    #8,%d1
    lsrl    #7,%d1
    mulu.w  #328,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
    movel   %d1,%sp@(12)
    bras    19f
18: cmpil   #8520,%d0
    blss    19f
    subil   #8520,%d0             | dispersion: n = (H - 0.26) x 1.351
    mulu.w  #1351,%d0
    movel   #1000,%d1
    divul   %d1,%d0
    movel   %d0,%d1               | allpass c = 0.618 n / (0.15 + n)
    mulu.w  #20251,%d1
    addil   #4915,%d0
    divul   %d0,%d1
    movel   %d1,%sp@(16)
    movel   #32768,%d0            | its delay at DC: (1 - c) / (1 + c)
    subl    %d1,%d0
    lsll    #8,%d0
    addil   #32768,%d1
    divul   %d1,%d0
    subl    %d0,%d4
19: cmpil   #2*256,%d4
    bges    20f
    movel   #2*256,%d4
20: movel   %d4,%sp@(0)
    | a trig: a period of noise, low-passed at
    | 4 f0 x 2^((B (2 - B) - 0.5) x 72 / 12)
    tstl    %sp@(20)
    beqs    21f
    movel   %sp@(0),%d0
    lsrl    #8,%d0
    movel   %d0,%a5@(16)
21: movel   #65536,%d0
    subl    %d6,%d0
    mulul   %d6,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    subil   #16384,%d0
    muls.w  #18432,%d0
    asrl    #8,%d0
    asrl    #7,%d0
    bsr     mi_exp2
    lsrl    #2,%d0
    movel   %d0,%d1
    movel   %d3,%d0               | 4 f0 at 48 kHz
    cmpil   #0x08000000,%d0
    blss    22f
    movel   #0x08000000,%d0
22: lsll    #3,%d0
    bsr     mi_mulr
    bsr     mi_onepole
    movel   %d0,%sp@(8)
    | the loop
    movel   %a5@,%d2
    movel   %a5@(4),%d6
    movel   %a5@(8),%d4
    movel   %a5@(12),%d5
    movel   mi_seed,%d7
msg_lp:
    moveq   #0,%d0                | the excitation
    tstl    %a5@(16)
    beqs    1f
    subql   #1,%a5@(16)
    MI_RND  %d7,%d1
    movel   %d7,%d0
    swap    %d0
    extl    %d0
    asrl    #1,%d0                | Q14
1:  subl    %a5@(20),%d0
    mulsl   %sp@(8),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    addl    %a5@(20),%d0
    movel   %d0,%a5@(20)
    movea.l %d0,%a1
    | read the line one period back (less the bridge's curve)
    movel   %sp@(0),%d1
    tstl    %sp@(12)
    beqs    2f
    movel   %a5@(32),%d0
    mulsl   %sp@(12),%d0
    asrl    #8,%d0
    asrl    #7,%d0                | curve x depth (the curve Q14)
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    subl    %d0,%d1
2:  movel   %d2,%d0
    lsll    #8,%d0
    subl    %d1,%d0               | the read position, Q8
    moveq   #0,%d1
    moveb   %d0,%d1
    asrl    #8,%d0
    andil   #2047,%d0
    movel   %a2@(0,%d0:l:4),%a0   | y0
    addql   #1,%d0
    andil   #2047,%d0
    movel   %a2@(0,%d0:l:4),%d0   | y1, a sample newer
    subl    %a0,%d0
    mulsl   %d1,%d0
    asrl    #8,%d0
    addl    %a0,%d0
    addl    %a1,%d0               | + the excitation
    MI_CLAMP %d0, 131071
    movel   %d0,%d1               | the DC blocker: y = x - x1 + (1 - 2^-12) y1,
    subl    %d4,%d1               | y kept x 256 (d5) so it never sticks
    movel   %d0,%d4
    movel   %d5,%d0
    asrl    #8,%d0
    asrl    #4,%d0
    subl    %d0,%d5
    lsll    #8,%d1
    addl    %d1,%d5
    movel   %d5,%d0
    asrl    #8,%d0
    tstl    %sp@(16)              | dispersion: y = c (x - y1) + x1
    beqs    3f
    subl    %a5@(28),%d0
    asrl    #2,%d0
    mulsl   %sp@(16),%d0
    asrl    #8,%d0
    asrl    #5,%d0
    addl    %a5@(24),%d0
    movel   %d5,%d1
    asrl    #8,%d1
    movel   %d1,%a5@(24)
    movel   %d0,%a5@(28)
3:  subl    %d6,%d0               | damping
    asrl    #3,%d0
    mulsl   %sp@(4),%d0
    addil   #2048,%d0
    asrl    #8,%d0
    asrl    #4,%d0
    addl    %d0,%d6
    movel   %d6,%a2@(0,%d2:l:4)
    addql   #1,%d2
    andil   #2047,%d2
    tstl    %sp@(12)              | the bridge's curve: (|s| - 0.025)+ x sign
    beqs    6f
    movel   %d6,%d0
    bpls    4f
    negl    %d0
4:  subil   #410,%d0
    bpls    5f
    moveq   #0,%d0
5:  addl    %d0,%d0
    tstl    %d6
    bpls    7f
    movel   %d0,%d1
    asrl    #1,%d1
    addl    %d1,%d0
    negl    %d0
7:  movel   %d0,%a5@(32)
6:  movel   %d6,%d0
    lsll    #2,%d0                | Q14 -> Q15, x 2
    MI_OUT2
    subql   #1,%sp@(MI_CNT)
    bplw    msg_lp
    movel   %d7,mi_seed
    movel   %d2,%a5@
    movel   %d6,%a5@(4)
    movel   %d4,%a5@(8)
    movel   %d5,%a5@(12)
    braw    mi_out

| ---- the labels: apply_names d1 = 6, for the selected track's engine -------
| Rows as va_swap's. mi_swap's name and short pointers are filled from
| mi_lab0.. for the engine (mi_lab: the selected track's, or 0), so the
| four knobs read as Plaits' for that engine and ENG shows its name.
| Returns a1 = mi_swap. Keeps d1. Clobbers d0, a0.
mi_fillswap:
    movel   mi_lab,%d0
    bpls    1f
    moveq   #0,%d0
1:  lsll    #5,%d0                | 32 bytes an engine
    lea.l   mi_labs,%a0
    adda.l  %d0,%a0
    lea.l   mi_swap,%a1
    movel   %a0@+,%a1@(4)
    movel   %a0@+,%a1@(12)
    movel   %a0@+,%a1@(28+4)
    movel   %a0@+,%a1@(28+12)
    movel   %a0@+,%a1@(56+4)
    movel   %a0@+,%a1@(56+12)
    movel   %a0@+,%a1@(84+4)
    movel   %a0@+,%a1@(84+12)
    rts

| ---- mi_watch: the label set follows the selected track's engine ------------
| From led_hook, once per audio block, at IPL 0 (as gran_watch). When the
| selected track is a PLAITS and its engine is not the one labelled, label
| it now: the four knobs' names change as ENG turns.
mi_watch:
    jsr     0x400cf866
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    cmpil   #MAX_TRK,%d0
    bccs    mw_off
    lea.l   trk_mach,%a0
    moveq   #MI_MACH,%d1
    cmpl    %a0@(0,%d0:l:4),%d1
    bnes    mw_off
    lea.l   mi_p,%a0
    lsll    #4,%d0
    movel   %a0@(12,%d0:l),%d0
    moveq   #12,%d1
    lsrl    %d1,%d0
    cmpl    mi_lab,%d0
    beqs    mw_out
    movel   %d0,mi_lab
    moveq   #6,%d1
    jsr     apply_names
    rts
mw_off:
    moveq   #-1,%d0
    movel   %d0,mi_lab
mw_out:
    rts

| ---- Contour names the engine ---------------------------------------------
| The page prints a parameter's value through the formatter at +0x20 of its
| 100-byte UI record (0x40a71754 + id x 100), called as (record + 0x14,
| value, buf) - see an_done. Contour is id 0x4a (Chord Contour, record
| 0x40a7343c). While PLAITS' labels are up (apply_names set 6) its formatter
| is mi_eng_fmt, which prints the engine's name as the stock list formatters
| (0x40045720) print theirs: sprintf(buf, "%s", name). Any other set puts
| the stock one back. From an_done, d1 = the set; keeps d1, clobbers d0/a0.
MI_ENG_FMT = 0x40a7345c
mi_fmtswap:
    lea.l   MI_ENG_FMT,%a0
    movel   %a0@,%d0
    cmpil   #6,%d1
    bnes    1f
    cmpil   #mi_eng_fmt,%d0
    beqs    2f
    movel   %d0,mi_fmt0           | the stock formatter, kept
    movel   #mi_eng_fmt,%a0@
    rts
1:  cmpil   #mi_eng_fmt,%d0
    bnes    2f
    movel   mi_fmt0,%a0@
2:  rts

| (record + 0x14, value, buf) -> sprintf(buf, "%s", the engine's name)
mi_eng_fmt:
    movel   %sp@(8),%d0
    moveq   #12,%d1
    asrl    %d1,%d0               | the engine: value / 4096, as mi_fill
    bpls    1f
    moveq   #0,%d0
1:  moveq   #7,%d1
    cmpl    %d0,%d1
    bges    2f
    movel   %d1,%d0
2:  lea.l   mi_engn,%a0
    movel   %a0@(0,%d0:l:4),%d0
    movel   %sp@(12),%d1
    movel   %d1,%sp@(4)           | buf
    movel   #0x40124b58,%d1       | "%s"
    movel   %d1,%sp@(8)
    movel   %d0,%sp@(12)          | the name
    jmp     0x40000e6e            | sprintf

| ---- Contour steps one engine a click ---------------------------------------
| From fine_hook (track-handle slot 0x14, "current + delta") for id 0x4a. On
| a PLAITS track any turn moves to the middle of the next or previous
| engine's 16 values, so each click is one engine, whatever the speed; on
| every other machine (Chord's Contour, the Sampler's Resonance) the step
| is the stock one.   args: sp@(4) this, sp@(8) id, sp@(12) current,
| sp@(16) delta
mi_eng_step:
    movea.l %sp@(4),%a0           | the track handle; +4 its track object
    movel   %a0@(4),%d0
    beqs    3f
    movea.l %d0,%a0
    movea.l %a0@,%a1
    movea.l %a1@(40),%a1          | vtable[40]: the sound
    movel   %d0,%sp@-
    jsr     %a1@
    addql   #4,%sp
    tstl    %d0
    beqs    3f
    movea.l %d0,%a0
    mvzb    %a0@(38),%d0
    cmpil   #MI_MACH,%d0
    bnes    3f
    movel   %sp@(16),%d1
    beqs    3f
    movel   %sp@(12),%d0
    asrl    #8,%d0
    asrl    #4,%d0                | this engine
    tstl    %d1
    bmis    1f
    addql   #1,%d0
    moveq   #7,%d1
    cmpl    %d0,%d1
    bges    2f
    movel   %d1,%d0
    bras    2f
1:  subql   #1,%d0
    bpls    2f
    moveq   #0,%d0
2:  moveq   #12,%d1
    lsll    %d1,%d0
    addil   #2048,%d0             | its middle
    subl    %sp@(12),%d0
    movel   %d0,%sp@(16)          | the delta that lands there
3:  jmp     0x4000c264

    .align 2
mi_swap:
    .long 2576, str_mih, str_mic, str_mihs, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, str_mit, str_mic, str_mits, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, str_mim, str_mic, str_mims, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, str_mie, str_mic, str_mies, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
| per engine: HARM, TIMB, MORP, ENG - each a name and a short label. Names
| are kept to 9 characters and labels to 4 (build.py checks), short of the
| stock ones, so nothing runs out of its box.
mi_labs:
    .long s_wsh, s_wshs, s_fld, s_flds, s_asy, s_asys, s_eng, s_engs
    .long s_rat, s_rats, s_idx, s_idxs, s_fdb, s_fdbs, s_eng, s_engs
    .long s_frt, s_frts, s_frm, s_frms, s_wdt, s_wdts, s_eng, s_engs
    .long s_rat, s_rats, s_dst, s_dsts, s_asy, s_asys, s_eng, s_engs
    .long s_chd, s_chds, s_arp, s_arps, s_syn, s_syns, s_eng, s_engs
    .long s_typ, s_typs, s_clk, s_clks, s_res, s_ress, s_eng, s_engs
    .long s_spr, s_sprs, s_den, s_dens, s_res, s_ress, s_eng, s_engs
    .long s_stf, s_stfs, s_brt, s_brts, s_dmp, s_dmps, s_eng, s_engs
| Contour's value, per engine (mi_eng_fmt)
mi_engn:
    .long s_e0, s_e1, s_e2, s_e3, s_e4, s_e5, s_e6, s_e7
str_mih:  .asciz "Harmonics"
str_mit:  .asciz "Timbre"
str_mim:  .asciz "Morph"
str_mie:  .asciz "Engine"
str_mic:  .asciz "Plaits"
str_mihs: .asciz "HARM"
str_mits: .asciz "TIMB"
str_mims: .asciz "MORP"
str_mies: .asciz "ENG"
s_eng:    .asciz "Engine"
s_engs:   .asciz "ENG"
s_e0:     .asciz "WSHP"
s_e1:     .asciz "FM"
s_e2:     .asciz "GRAN"
s_e3:     .asciz "PD"
s_e4:     .asciz "CHIP"
s_e5:     .asciz "NOIS"
s_e6:     .asciz "PART"
s_e7:     .asciz "STRG"
s_wsh:    .asciz "Waveshape"
s_wshs:   .asciz "SHPE"
s_fld:    .asciz "Fold"
s_flds:   .asciz "FOLD"
s_asy:    .asciz "Asymmetry"
s_asys:   .asciz "ASYM"
s_rat:    .asciz "Ratio"
s_rats:   .asciz "RATI"
s_idx:    .asciz "Mod Index"
s_idxs:   .asciz "INDX"
s_fdb:    .asciz "Feedback"
s_fdbs:   .asciz "FDBK"
s_frt:    .asciz "Formant 2"
s_frts:   .asciz "FRM2"
s_frm:    .asciz "Formant"
s_frms:   .asciz "FRMT"
s_wdt:    .asciz "Width"
s_wdts:   .asciz "WDTH"
s_dst:    .asciz "Distort"
s_dsts:   .asciz "DIST"
s_chd:    .asciz "Chord"
s_chds:   .asciz "CHRD"
s_arp:    .asciz "Arpeggio"
s_arps:   .asciz "ARP"
s_syn:    .asciz "Sync"
s_syns:   .asciz "SYNC"
s_typ:    .asciz "Filt Type"
s_typs:   .asciz "TYPE"
s_clk:    .asciz "Clock"
s_clks:   .asciz "CLK"
s_res:    .asciz "Resonance"
s_ress:   .asciz "RESO"
s_spr:    .asciz "Spread"
s_sprs:   .asciz "SPRD"
s_den:    .asciz "Density"
s_dens:   .asciz "DENS"
s_stf:    .asciz "Stiffness"
s_stfs:   .asciz "STIF"
s_brt:    .asciz "Bright"
s_brts:   .asciz "BRIG"
s_dmp:    .asciz "Damping"
s_dmps:   .asciz "DAMP"
    .align 2

| ---- state ------------------------------------------------------------------
    .balign 4
mi_p:      .space 6*16            | the four dials, Q15
mi_eng:    .long -1,-1,-1,-1,-1,-1 | the engine each track's state is for
mi_lab:    .long -1               | the engine labelled (mi_watch)
mi_fmt0:   .long 0x400456c8       | Contour's stock formatter (mi_fmtswap)
mi_seed:   .long 0x2545f491       | the noise's LCG
mi_dz:     .space 8               | a track's delay line is ours and zeroed
mi_st:     .space 6*MI_ST         | per track
| Its defaults (HARM 64, TIMB 64, MORP 64, ENG 0 = WSHP) are dr_defaults'
| fourth row, written by mc_commit_hook as the drums' are.
    .include "mi_tables.inc"
    .align 2
