
| ============================================================================
| ==================  VA: machine 7, a two-oscillator synth  =================
|
| See docs/VA.md. Machine 7 (after the Sampler, 6) is the Wave mode's
| oscillator with the wavetable taken out: the same per-track phase words
| (wv_ph: [0] OSC 1, [1] OSC 2, [2] unused), the same restart flag (wv_trig),
| the same pitch (voice+232 -> Q16 step -> the Q32 increment wave_fill uses,
| at the same 2x rate into sampler_buf+32), the same accumulator (gran_acc)
| and the same tail as every Sampler mode: the stock decimator, Amp Decay
| (0x400a9252 / 0x400a9430) and amp_hook - Attack, the common Filter and
| Resonance (gflt_run, as on the stock machines), MUT's and the mute's gain.
|
| What is new is only the waveform: per sample, a saw, square or triangle
| computed from the phase, the saw and square band-limited with PolyBLEP
| (a two-sample polynomial correction at each jump, from one reciprocal per
| oscillator per block), the two mixed by MIX.
|
| How the audio side tells it apart: sampler_pre publishes trackData+18 (the
| machine) into trk_mach for every track; sampler_pre sends machine 7 here
| (va_pre), which hands the stock dispatch voice@(0) = 6 exactly as the
| Sampler does, so no stock code in the audio thread ever sees an index
| past 6 (0x400a7df4's bound and the six-entry render tables 0x40118610 /
| 0x40118628 are unchanged). sampler_dispatch then runs sampler_render,
| which branches to sv_va on trk_mach = 7.
|
| The four dials are the Sampler's own four ids (0x2e/0x3e/0x49/0x4a ->
| trackData +22/+24/+26/+28, the machine-page slots 0x10..0x13), so they
| p-lock, slide, take LFOs and are saved exactly as the Sampler's are;
| apply_names labels them OSC1 / OSC2 / DTUN / MIX while a VA page is up.
| ============================================================================

| VA_MACH (7, the machine index: sound+38, trackData+18) is set in model_tg.s
VA_DRIFT  = 40                    | drift bound: +-40/65536 = +-1.06 cents
VA_SAW    = 0                     | waveform codes in va_w1 / va_w2
VA_SQR    = 1
VA_TRI    = 2

| ---- sampler_pre's branch for machine 7 ------------------------------------
| Entered (jmp) from sampler_pre with its registers: d2 = track, a2 =
| trackData, a3/a5 = last/this block's trigger flags, fp = voice. d0/d1/d4,
| a0/a1 are free; everything else belongs to the voice loop.
va_pre:
    cmpil   #VA_MACH,%d1          | 8..10: the drums' dials (drums.inc),
    beqs    vp_va                 | 11: PLAITS' (plaits.inc)
    cmpil   #MI_MACH,%d1
    bnes    vp_dr
    jsr     mi_params
    bras    vp_tr
vp_dr:
    jsr     dr_params
    bras    vp_tr
vp_va:
    bsr     va_params
vp_tr:
    tstl    %a5@                  | triggered this block...
    beqs    vp_nt
    tstl    %a3@                  | ...and not last block: a new note
    bnes    vp_nt
    lea.l   wv_trig,%a0           | the render restarts the phases (va_fill),
    moveq   #1,%d0                | as sampler_pre's sp_wave_trig does for Wave
    moveb   %d0,%a0@(0,%d2:l)
    lea.l   dr_trig,%a0           | the drums' envelopes restart (dr_fill)
    moveb   %d0,%a0@(0,%d2:l)
vp_nt:
    | The stock dispatch sees a Sampler voice: 0x400a7dcc compares this with
    | the voice's last machine and resets the voice when it differs, then
    | 0x400a7dee reloads d4 from it for the render-table bound - exactly the
    | Sampler's sp_nt_x. Machine 7 is told apart by trk_mach, not by this.
    moveq   #6,%d4
    movel   %d4,%fp@
    jmp     0x400a7dcc

| ---- the four dials -> this block's oscillator settings --------------------
| d2 = track, a2 = trackData. Every block (the words are smoothed by the OS,
| jumped by trigs and p-locks, written by LFOs and slides). Clobbers d0/d1/a0.
|   +22 OSC1  -> va_w1: 0..42 saw, 43..85 square, 86..127 triangle
|   +24 OSC2  -> va_w2: the same
|   +26 DTUN  -> va_rat: OSC 2's ratio, Q16, from va_det_tab (64 = unison)
|   +28 MIX   -> va_mix: OSC 2's share, 0..256 (OSC 1 gets the rest)
va_params:
    mvsw    %a2@(22),%d1
    jsr     sw_p_dial             | -> 0..127 (the Sampler modes' own helper)
    bsr     va_wave
    lea.l   va_w1,%a0
    movel   %d1,%a0@(0,%d2:l:4)
    mvsw    %a2@(24),%d1
    jsr     sw_p_dial
    bsr     va_wave
    lea.l   va_w2,%a0
    movel   %d1,%a0@(0,%d2:l:4)
    mvsw    %a2@(26),%d1
    jsr     sw_p_dial
    lea.l   va_det_tab,%a0
    movel   %a0@(0,%d1:l:4),%d0
    lea.l   va_rat,%a0
    movel   %d0,%a0@(0,%d2:l:4)
    mvsw    %a2@(28),%d1
    jsr     sw_p_dial
    addl    %d1,%d1               | 0..254, 64 -> 128: an even mix
    cmpil   #254,%d1
    bcss    vpm_ok
    movel   #256,%d1              | 127: OSC 2 alone
vpm_ok:
    lea.l   va_mix,%a0
    movel   %d1,%a0@(0,%d2:l:4)
    rts
| d1 = dial 0..127 -> waveform code
va_wave:
    cmpil   #43,%d1
    bcss    vw_saw
    cmpil   #86,%d1
    bcss    vw_sqr
    moveq   #VA_TRI,%d1
    rts
vw_sqr:
    moveq   #VA_SQR,%d1
    rts
vw_saw:
    moveq   #VA_SAW,%d1
    rts

| ---- va_fill(track, stepQ16): one block, 64 samples at 96 kHz --------------
| Into sampler_buf+32, as wave_fill fills it (s16 << 15). The step is the
| render's own: Q16, 1.0 = note 60 = 261.63 Hz.
|   - the increment per 96 kHz sample is step * 357.2031 / 2, as wave_fill
|   - OSC 2's is OSC 1's x va_rat, split so that no product passes 32 bits
|   - each oscillator drifts by a random walk of one step a block, bounded to
|     +-VA_DRIFT/65536 (about a cent): a slow, analogue-like wander
|   - a new note (wv_trig) restarts OSC 1 at half a cycle (a zero crossing)
|     and OSC 2 within +-1/16 cycle of it, a little differently each note, so unison oscillators do not
|     cancel and still never start exactly alike
|   - an oscillator MIX has turned all the way off is not computed at all:
|     its phase is just advanced by the block
| Frame: 8 bytes of locals (sp@(0) = OSC 2's increment, sp@(4) = track),
| then the ten saved registers; the arguments are at sp@(52) and sp@(56).
va_fill:
    lea.l   %sp@(-48),%sp
    moveml  %d2-%d7/%a2-%a5,%sp@(8)
    movel   %sp@(52),%d6          | track
    movel   %d6,%sp@(4)
    movel   %sp@(56),%d1          | step, Q16
    bnes    vf_st
    movel   #0x10000,%d1
vf_st:
    movel   %d1,%d3               | d3 = OSC 1's increment: step * 357.2031,
    movel   #357,%d0              | halved for the 2x rate (wave_fill's own)
    mulul   %d0,%d3
    moveq   #52,%d0
    mulul   %d0,%d1
    lsrl    #8,%d1
    addl    %d1,%d3
    lsrl    #1,%d3
    | OSC 2 = OSC 1 x ratio. ratio is Q16 up to 2.0, so take it as Q15 (at
    | most 65536) and multiply the increment's two parts separately:
    | (inc >> 15) * r and ((inc & 0x7fff) * r) >> 15 both stay under 2^32.
    lea.l   va_rat,%a0
    movel   %a0@(0,%d6:l:4),%d1
    lsrl    #1,%d1
    moveq   #15,%d5
    movel   %d3,%d4
    lsrl    %d5,%d4
    mulul   %d1,%d4
    movel   %d3,%d0
    andil   #0x7fff,%d0
    mulul   %d1,%d0
    lsrl    %d5,%d0
    addl    %d0,%d4               | d4 = OSC 2's increment
    | ---- drift: one LCG draw a block, its top two bits move the two walks
    lea.l   va_seed,%a0
    movel   %a0@,%d7
    movel   #1664525,%d0
    mulul   %d0,%d7
    addil   #1013904223,%d7
    movel   %d7,%a0@              | d7 = this block's random word
    movel   %d6,%d0
    lsll    #3,%d0
    lea.l   va_drift,%a1
    adda.l  %d0,%a1               | a1 = this track's two walks
    movel   %d7,%d0
    bsr     va_walk               | OSC 1: bit 31
    movel   %a1@,%d1
    movel   %d3,%d0
    clrw    %d0
    swap    %d0
    mulsl   %d1,%d0
    addl    %d0,%d3
    addql   #4,%a1
    movel   %d7,%d0
    addl    %d0,%d0
    bsr     va_walk               | OSC 2: bit 30
    movel   %a1@,%d1
    movel   %d4,%d0
    clrw    %d0
    swap    %d0
    mulsl   %d1,%d0
    addl    %d0,%d4
    movel   %d4,%sp@              | OSC 2's increment, parked
    | ---- the phases: wv_ph's three longs a track, [0] and [1] ours
    movel   %sp@(4),%d6
    movel   %d6,%d0
    addl    %d0,%d0
    addl    %d6,%d0
    lsll    #2,%d0
    lea.l   wv_ph,%a2
    adda.l  %d0,%a2               | a2 = &wv_ph[track * 3]
    lea.l   wv_trig,%a0
    tstb    %a0@(0,%d6:l)
    beqs    vf_notrig
    clrb    %a0@(0,%d6:l)
    movel   #0x80000000,%d0       | OSC 1 from half a cycle: the saw's and the
    movel   %d0,%a2@              | triangle's zero crossing, the square's edge
    movel   %d7,%d0               | (its PolyBLEP midpoint) - so a note never
    lsrl    #3,%d0                | starts on the saw's jump. OSC 2 within
    addil   #0x70000000,%d0       | +-1/16 cycle of it
    movel   %d0,%a2@(4)
vf_notrig:
    | ---- clear the accumulator (wave_fill's gran_acc: 64 longs)
    lea.l   gran_acc,%a0
    moveq   #63,%d0
    moveq   #0,%d1
vf_clr:
    movel   %d1,%a0@+
    subql   #1,%d0
    bpls    vf_clr
    | ---- OSC 1, weight 256 - MIX
    lea.l   va_mix,%a0
    movel   #256,%d5
    subl    %a0@(0,%d6:l:4),%d5
    lea.l   va_w1,%a0
    movel   %a0@(0,%d6:l:4),%d7
    movel   %a2@,%d2
    bsr     va_osc
    movel   %d2,%a2@
    | ---- OSC 2, weight MIX
    movel   %sp@(4),%d6
    lea.l   va_mix,%a0
    movel   %a0@(0,%d6:l:4),%d5
    lea.l   va_w2,%a0
    movel   %a0@(0,%d6:l:4),%d7
    movel   %sp@,%d3
    movel   %a2@(4),%d2
    bsr     va_osc
    movel   %d2,%a2@(4)
    | ---- out: the weights sum to 256, so >> 8 is the mix, clamped, << 15
    lea.l   gran_acc,%a0
    lea.l   sampler_buf+32,%a1    | the render's window (decimated next)
    moveq   #63,%d1
vf_o:
    movel   %a0@+,%d0
    asrl    #8,%d0
    cmpil   #32767,%d0
    bles    vf_o1
    movel   #32767,%d0
vf_o1:
    cmpil   #-32767,%d0
    bges    vf_o2
    movel   #-32767,%d0
vf_o2:
    lsll    #8,%d0
    lsll    #7,%d0
    movel   %d0,%a1@+
    subql   #1,%d1
    bpls    vf_o
    moveml  %sp@(8),%d2-%d7/%a2-%a5
    lea.l   %sp@(48),%sp
    rts

| a1 = a walk, d0 bit 31 = its direction: one step, kept to +-VA_DRIFT.
| Clobbers d0 only.
va_walk:
    tstl    %d0
    bmis    vk_dn
    moveq   #1,%d0
    addl    %a1@,%d0
    cmpil   #VA_DRIFT,%d0
    bgts    vk_out
    movel   %d0,%a1@
vk_out:
    rts
vk_dn:
    moveq   #-1,%d0
    addl    %a1@,%d0
    cmpil   #-VA_DRIFT,%d0
    blts    vk_out
    movel   %d0,%a1@
    rts

| ---- one oscillator into gran_acc -------------------------------------------
| d2 = phase (Q32, updated), d3 = increment, d5 = weight (0..256), d7 =
| waveform. Clobbers d0/d1/d4/d6/d7, a3.
| PolyBLEP: a jump at phase 0 is smoothed over the sample either side of
| it. For u, the phase's distance past the jump (u < inc), the correction is
| (1 - u/inc)^2 of the jump's half-height; for u = -phase before it, the
| same with the opposite sign. u/inc comes from one reciprocal per block,
| R = 2^31 / (inc >> 8): ((u >> 8) * R) >> 16 is u/inc in Q15, and with
| u < inc the product stays under 2^31. Only the two samples next to each
| jump take the slow path (vo_blep); the rest cost two compares.
va_osc:
    tstl    %d5
    bnes    vo_go
    movel   %d3,%d0               | weight 0: silent, so only move the phase
    lsll    #6,%d0                | 64 samples on
    addl    %d0,%d2
    rts
vo_go:
    lea.l   gran_acc,%a3
    movel   %d3,%d4
    lsrl    #8,%d4
    bnes    vo_r
    moveq   #1,%d4
vo_r:
    movel   #0x80000000,%d0
    divul   %d4,%d0
    movel   %d0,%d4               | d4 = R
    moveq   #63,%d6
    subql   #1,%d7
    bmiw    vo_saw
    beqw    vo_sqr

| triangle: 1 - 2|saw(phase + 1/4)| - 0 at phase 0, rising. Its corners are
| only a change of slope (harmonics falling 12 dB an octave): no correction.
vo_tri:
    movel   %d2,%d0
    addil   #0xc0000000,%d0       | + 1/4 cycle, + 1/2 for the signed saw
    swap    %d0
    extl    %d0
    bpls    vt_a
    negl    %d0
vt_a:
    addl    %d0,%d0
    negl    %d0
    addil   #32768,%d0
    mulsl   %d5,%d0
    addl    %d0,%a3@+
    addl    %d3,%d2
    subql   #1,%d6
    bpls    vo_tri
    rts

| saw: the phase's top half-word, signed - rising, jumping down at 0
vo_saw:
    movel   %d2,%d0
    addil   #0x80000000,%d0
    swap    %d0
    extl    %d0                   | -32768..32767
    cmpl    %d3,%d2
    bcss    vs_lo                 | just past the jump
    movel   %d2,%d1
    negl    %d1
    cmpl    %d3,%d1
    bcss    vs_hi                 | just before it
vs_acc:
    mulsl   %d5,%d0
    addl    %d0,%a3@+
    addl    %d3,%d2
    subql   #1,%d6
    bpls    vo_saw
    rts
vs_lo:
    movel   %d2,%d1
    bsr     vo_blep
    addl    %d1,%d0
    bras    vs_acc
vs_hi:
    bsr     vo_blep
    subl    %d1,%d0
    bras    vs_acc

| square: +32767 for the first half cycle, -32767 for the second - a jump up
| at 0 and down at 1/2, each with its correction
vo_sqr:
    movel   #32767,%d0
    tstl    %d2
    bpls    vq_p
    negl    %d0
vq_p:
    cmpl    %d3,%d2
    bcss    vq_a                  | just past the rise
    movel   %d2,%d1
    negl    %d1
    cmpl    %d3,%d1
    bcss    vq_b                  | just before it
    movel   %d2,%d1
    bchg    #31,%d1               | the phase from the fall
    cmpl    %d3,%d1
    bcss    vq_c                  | just past the fall
    negl    %d1
    cmpl    %d3,%d1
    bcss    vq_d                  | just before it
vq_acc:
    mulsl   %d5,%d0
    addl    %d0,%a3@+
    addl    %d3,%d2
    subql   #1,%d6
    bpls    vo_sqr
    rts
vq_a:
    movel   %d2,%d1
    bsr     vo_blep
    subl    %d1,%d0
    bras    vq_acc
vq_b:
    bsr     vo_blep
    addl    %d1,%d0
    bras    vq_acc
vq_c:
    bsr     vo_blep
    addl    %d1,%d0
    bras    vq_acc
vq_d:
    bsr     vo_blep
    subl    %d1,%d0
    bras    vq_acc

| d1 = u (< inc), d4 = R -> d1 = (32768 - u/inc in Q15)^2 >> 15, 0..32768.
| Clobbers d1 only.
vo_blep:
    lsrl    #8,%d1
    mulul   %d4,%d1
    clrw    %d1
    swap    %d1                   | u/inc, Q15
    negl    %d1
    addil   #32768,%d1            | 1 - u/inc
    mulul   %d1,%d1
    lsrl    #8,%d1
    lsrl    #7,%d1
    rts

| ---- the VA's labels for the four borrowed ids (apply_names, d1 = 2) --------
| Rows as name_swap's: param_table entry offset, our name / category / short,
| then the stock three to put back. The ids, offsets and stock strings are
| name_swap's own - only the labels differ.
    .align 2
va_swap:
    .long 2576, str_vo1,  str_vcat, str_vo1sh,  0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, str_vo2,  str_vcat, str_vo2sh,  0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, str_vdet, str_vcat, str_vdetsh, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, str_vmix, str_vcat, str_vmixsh, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
str_vo1:     .asciz "Osc1 Wave"
str_vo2:     .asciz "Osc2 Wave"
str_vdet:    .asciz "Osc2 Tune"
str_vmix:    .asciz "Osc Mix"
str_vcat:    .asciz "VA"
str_vo1sh:   .asciz "OSC1"
str_vo2sh:   .asciz "OSC2"
str_vdetsh:  .asciz "DTUN"
str_vmixsh:  .asciz "MIX"
    .align 2

| ---- the VA's state: per track, six tracks -----------------------------------
    .balign 4
va_w1:     .long 0,0,0,0,0,0      | OSC 1 waveform (VA_SAW / VA_SQR / VA_TRI)
va_w2:     .long 0,0,0,0,0,0      | OSC 2 waveform
va_rat:    .long 65536,65536,65536,65536,65536,65536 | OSC 2 ratio, Q16
va_mix:    .long 128,128,128,128,128,128 | OSC 2's weight, 0..256
va_drift:  .space 6*2*4           | per track: OSC 1's and OSC 2's walks
va_seed:   .long 0x6d2b79f5       | the drift's LCG
| Written over sound +42..+48 (k = 11..14) when a track becomes a VA, by
| mc_commit_hook: OSC1 saw, OSC2 saw, DTUN 68 (+7.9 cents), MIX 64 (even).
va_defaults:
    .word 0, 0, 68<<8, 64<<8
    .include "va_tables.inc"
    .align 2
