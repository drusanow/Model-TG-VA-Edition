| ============================================================================
| ===========  VA KICK / VA SNARE / VA HIHAT: machines 8, 9 and 10  ===========
|
| See docs/DRUMS.md. Three analogue-style drum voices, built exactly as the VA
| (machine 7) is: sampler_pre sends them to va_pre, which reads their four
| dials (the Sampler's ids 0x2e/0x3e/0x49/0x4a -> trackData +22..+28, so they
| p-lock, slide, take LFOs and are saved) and hands the stock dispatch a
| Sampler voice; sampler_render then calls dr_fill instead of va_fill, into
| the same 64-sample, 96 kHz window, followed by the same tail: the stock
| decimator, Amp Decay (the stock Decay knob is the drum's decay), Attack,
| the common Filter and Resonance. The stock Pitch knob and the trig's note
| tune them (note 60 = the base frequencies below).
|
| Models (after the TR-909 / TR-808 and Elektron's analogue BD/SD/HH):
|   KICK   one parabolic sine (a slightly impure sine, like the 909's shaped
|          triangle) at 55 Hz x note, with an exponential pitch sweep from
|          up to 5x down to it; a click of noise, decaying in ~1.3 ms; drive
|          SWP sweep depth   STM sweep time (2..150 ms)   SAT drive   CLK click
|   SNARE  two dropping sines, 180 and 330 Hz x note (the 909's pair), with
|          their own ~85 ms decay, a 15 ms pitch drop; white noise through
|          a one-pole low-pass (TONE) and a 600 Hz low cut; drive
|          NOIS body/noise mix   PENV pitch drop depth   TONE noise colour
|          SAT drive
|   HIHAT  the 808's six square oscillators (205.3, 304.4, 369.6, 522.7, 540,
|          800 Hz x note) summed, optional noise, through a two-pole high-pass
|          (TONE); SPRD spreads the six around their mean (64 = the 808's own
|          ratios, 0 = one pitch, 127 = twice as clangorous); drive
|          TONE high-pass   SPRD spread   NOIS noise   SAT drive
| Everything is integer: per block a few multiplies set the increments and
| coefficients, per sample a handful of multiplies. The squares are naive
| (at 96 kHz the aliases sit far below the cluster); the sines are a
| parabola, whose few-percent third harmonic is the "grit".
| ============================================================================

| DR_KICK / DR_SNARE / DR_HAT (8, 9, 10) and MACH_LAST are set in model_tg.s

| ---- the four dials -> dr_p (per track: four longs, 0..127) -----------------
| From va_pre, as va_params: d2 = track, a2 = trackData. Clobbers d0/d1/a0.
dr_params:
    movel   %d2,%d0
    lsll    #4,%d0
    lea.l   dr_p,%a0
    adda.l  %d0,%a0
    movel   %a0,%sp@-
    mvsw    %a2@(22),%d1
    jsr     sw_p_dial
    movea.l %sp@,%a0
    movel   %d1,%a0@
    mvsw    %a2@(24),%d1
    jsr     sw_p_dial
    movea.l %sp@,%a0
    movel   %d1,%a0@(4)
    mvsw    %a2@(26),%d1
    jsr     sw_p_dial
    movea.l %sp@,%a0
    movel   %d1,%a0@(8)
    mvsw    %a2@(28),%d1
    jsr     sw_p_dial
    movea.l %sp@+,%a0
    movel   %d1,%a0@(12)
    rts

| d0 = an increment, d1 = a ratio, Q14 (< 2^17) -> d0 = their product, with
| no partial product past 32 bits. Keeps everything else.
dr_mq14:
    movel   %d2,%sp@-
    movel   %d0,%d2
    lsrl    #7,%d0
    lsrl    #7,%d0
    mulul   %d1,%d0
    andil   #0x3fff,%d2
    mulul   %d1,%d2
    lsrl    #7,%d2
    lsrl    #7,%d2
    addl    %d2,%d0
    movel   %sp@+,%d2
    rts

| d0 = the SAT dial -> dr_gq: the soft clip's input gain, Q12, 0 = bypass.
| Small signals get 1 + dial/16 (up to ~9x); the clip is y = u (2 - |u|) on
| u = x x gain / 2 (|u| <= 1), so a signal below the knee keeps that gain
| and the peak is full scale. Clobbers d0.
dr_setgq:
    tstl    %d0
    beqs    dsg_set
    lsll    #8,%d0
    addil   #4096,%d0
    lsrl    #1,%d0                | / 2, Q12
dsg_set:
    movel   %d0,dr_gq
    rts

| DR_SAT: d0 (s32, about +-32767) -> d0 driven and soft-clipped: one
| multiply for the gain, one for the curve. Clobbers d1.
    .macro DR_SAT
    movel   dr_gq,%d1
    beqs    1f
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #4,%d0                | u = x x gain / 2
    cmpil   #32767,%d0
    bles    2f
    movel   #32767,%d0
2:
    cmpil   #-32767,%d0
    bges    3f
    movel   #-32767,%d0
3:
    movel   %d0,%d1
    bpls    4f
    negl    %d1
4:
    negl    %d1
    addil   #65536,%d1            | 2 - |u|
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #7,%d0                | u (2 - |u|)
1:
    .endm

| DR_OUT: d0 -> clamped to +-32767, << 15, into a3@+ (the render's window,
| as va_fill writes it)
    .macro DR_OUT
    cmpil   #32767,%d0
    bles    1f
    movel   #32767,%d0
1:
    cmpil   #-32767,%d0
    bges    2f
    movel   #-32767,%d0
2:
    lsll    #8,%d0
    lsll    #7,%d0
    movel   %d0,%a3@+
    .endm

| DR_SINE: dN = a Q32 phase -> d0 = a parabolic sine of it, +-32766: x = the
| phase's top half, signed; y = x (32767 - |x|) >> 13. Clobbers d1.
    .macro DR_SINE ph
    movel   \ph,%d0
    swap    %d0
    extl    %d0
    movel   %d0,%d1
    bpls    1f
    negl    %d1
1:
    negl    %d1
    addil   #32767,%d1
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #5,%d0
    .endm

| DR_NOISE: the shared LCG in d3 -> d1 = white noise, s16. VA's constants.
    .macro DR_NOISE
    movel   #1664525,%d1
    mulul   %d1,%d3
    addil   #1013904223,%d3
    movel   %d3,%d1
    swap    %d1
    extl    %d1
    .endm

| ---- dr_fill(track, stepQ16, machine): one block into sampler_buf+32 -------
| Frame: 24 bytes of locals (sp@(0..23), per machine below), the ten saved
| registers at sp@(24); the arguments at sp@(68), sp@(72), sp@(76).
dr_fill:
    lea.l   %sp@(-64),%sp
    moveml  %d2-%d7/%a2-%a5,%sp@(24)
    movel   %sp@(68),%d6          | track
    movel   %sp@(72),%d1          | step, Q16 (1.0 = note 60)
    bnes    df_st
    movel   #0x10000,%d1
df_st:
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
    lea.l   dr_p,%a4
    adda.l  %d0,%a4               | a4 = this track's four dials
    movel   %a4@(12),%d0          | SAT is the 4th dial on the snare and hat,
    movel   %sp@(76),%d1          | the 3rd on the kick
    cmpil   #DR_KICK,%d1
    bnes    df_sat
    movel   %a4@(8),%d0
df_sat:
    bsr     dr_setgq
    lea.l   dr_trig,%a0
    moveq   #0,%d7
    moveb   %a0@(0,%d6:l),%d7     | d7 = a new note this block
    clrb    %a0@(0,%d6:l)
    movel   %d6,%sp@(20)          | the track, kept
    lea.l   sampler_buf+32,%a3
    movel   %sp@(76),%d0
    cmpil   #DR_SNARE,%d0
    beqw    df_snare
    bhiw    df_hat

| ---- KICK ----
| locals: sp@(0) = the swept increment's step a sample
df_kick:
    tstl    %d7
    beqs    dk_nt
    lea.l   dr_ph1,%a0
    clrl    %a0@(0,%d6:l:4)       | from the sine's zero crossing
    lea.l   dr_pe,%a0
    movel   #32768,%d0            | the sweep at its top
    movel   %d0,%a0@(0,%d6:l:4)
    movel   %a4@(12),%d0          | CLK: the click's level, Q30
    lsll    #8,%d0
    lsll    #8,%d0
    lsll    #7,%d0
    lea.l   dr_ce,%a0
    movel   %d0,%a0@(0,%d6:l:4)
dk_nt:
    movel   %d3,%d0
    movel   #KICK_R,%d1
    bsr     dr_mq14
    movel   %d0,%d7               | d7 = the base increment
    lea.l   dr_pe,%a0
    movel   %a0@(0,%d6:l:4),%d4   | the sweep now, Q15
    movel   %a4@(4),%d0           | STM -> this block's decay
    lea.l   dr_ktab,%a1
    movel   %a1@(0,%d0:l:4),%d0
    movel   %d4,%d5
    mulul   %d0,%d5
    lsrl    #8,%d5
    lsrl    #7,%d5                | the sweep at the block's end
    movel   %d5,%a0@(0,%d6:l:4)
    movel   %a4@,%d2              | SWP: depth, up to 4.0 (Q14): 5x at the top
    movel   #516,%d0
    mulul   %d0,%d2
    movel   %d4,%d0               | ratio = 1 + sweep x depth
    mulul   %d2,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #16384,%d0
    movel   %d0,%d1
    movel   %d7,%d0
    bsr     dr_mq14
    movel   %d0,%d4               | d4 = the increment now
    movel   %d5,%d0
    mulul   %d2,%d0
    lsrl    #8,%d0
    lsrl    #7,%d0
    addil   #16384,%d0
    movel   %d0,%d1
    movel   %d7,%d0
    bsr     dr_mq14
    subl    %d4,%d0
    asrl    #6,%d0
    movel   %d0,%d5               | d5 = its change a sample
    lea.l   dr_ce,%a0
    movel   %a0@(0,%d6:l:4),%d7   | d7 = the click's envelope, Q30
    lea.l   dr_ph1,%a0
    movel   %a0@(0,%d6:l:4),%d2   | d2 = the phase
    movel   dr_seed,%d3
    moveq   #63,%d6
dk_l:
    addl    %d4,%d2
    addl    %d5,%d4
    DR_SINE %d2
    tstl    %d7
    beqs    dk_nc
    movea.l %d0,%a1               | the sine, parked
    DR_NOISE
    movel   %d7,%d0
    asrl    #8,%d0
    asrl    #7,%d0                | the click's level, Q15
    mulsl   %d0,%d1
    asrl    #8,%d1
    asrl    #7,%d1
    adda.l  %d1,%a1
    movel   %d7,%d0               | -1/128 a sample: ~1.3 ms
    asrl    #7,%d0
    addql   #1,%d0
    subl    %d0,%d7
    bpls    dk_ce
    moveq   #0,%d7
dk_ce:
    movel   %a1,%d0
dk_nc:
    DR_SAT
    DR_OUT
    subql   #1,%d6
    bplw    dk_l
    movel   %sp@(20),%d6
    lea.l   dr_ce,%a0
    movel   %d7,%a0@(0,%d6:l:4)
    lea.l   dr_ph1,%a0
    movel   %d2,%a0@(0,%d6:l:4)
    braw    df_out

| ---- SNARE ----
| locals: sp@(4) = the body's gain this block (its envelope x (128 - NOIS),
| Q15), sp@(8) = the noise's weight (NOIS, 128 at the top), sp@(12) = the
| noise low-pass coefficient (TONE)
df_snare:
    tstl    %d7
    beqs    ds_nt
    lea.l   dr_ph1,%a0
    clrl    %a0@(0,%d6:l:4)
    lea.l   dr_pe,%a0
    movel   #32768,%d0
    movel   %d0,%a0@(0,%d6:l:4)
    lea.l   dr_be,%a0
    movel   #0x3fffffff,%d0       | the body at full, Q30
    movel   %d0,%a0@(0,%d6:l:4)
ds_nt:
    movel   %a4@,%d0              | NOIS
    moveq   #127,%d1
    cmpl    %d0,%d1
    bnes    ds_w
    movel   #128,%d0
ds_w:
    movel   %d0,%sp@(8)
    negl    %d0
    addil   #128,%d0
    movel   %d0,%sp@(4)
    movel   %a4@(8),%d0           | TONE: 300 Hz .. 16 kHz
    lea.l   dr_ftab,%a0
    movel   %a0@(0,%d0:l:4),%d0
    movel   %d0,%sp@(12)
    | the pitch drop: ratio = 1 + sweep x PENV (up to 2.0, Q14: 3x at the top)
    lea.l   dr_pe,%a0
    movel   %a0@(0,%d6:l:4),%d4
    movel   %d4,%d5
    movel   #SN_PEC,%d0
    mulul   %d0,%d5
    lsrl    #8,%d5
    lsrl    #7,%d5
    movel   %d5,%a0@(0,%d6:l:4)
    movel   %a4@(4),%d2
    movel   #258,%d0
    mulul   %d0,%d2               | depth, Q14
    mulul   %d2,%d4
    lsrl    #8,%d4
    lsrl    #7,%d4
    addil   #16384,%d4            | the ratio now
    mulul   %d2,%d5
    lsrl    #8,%d5
    lsrl    #7,%d5
    addil   #16384,%d5            | ... at the block's end
    movel   %d3,%d0               | the body: 180 Hz x note
    movel   #SN_R1,%d1
    bsr     dr_mq14
    movel   %d0,%d7
    movel   %d4,%d1
    bsr     dr_mq14
    movel   %d0,%d4               | d4 = its increment now
    movel   %d7,%d0
    movel   %d5,%d1
    bsr     dr_mq14
    subl    %d4,%d0
    asrl    #6,%d0
    movel   %d0,%d5               | d5 = its change a sample
    lea.l   dr_be,%a0             | the body's envelope (Q30), a block at a
    movel   %a0@(0,%d6:l:4),%d0   | time: -1/128 a block, ~85 ms
    movel   %d0,%d1
    asrl    #7,%d1
    subl    %d1,%d0
    movel   %d0,%a0@(0,%d6:l:4)
    asrl    #8,%d0
    asrl    #7,%d0                | Q15
    mulsl   %sp@(4),%d0
    asrl    #7,%d0
    movel   %d0,%sp@(4)           | x the body's weight
    lea.l   dr_ph1,%a0
    movel   %a0@(0,%d6:l:4),%d2   | d2 = the phase
    lea.l   dr_lp,%a0
    movea.l %a0@(0,%d6:l:4),%a4   | a4 = the noise low-pass
    lea.l   dr_hp,%a0
    movea.l %a0@(0,%d6:l:4),%a5   | a5 = the low cut's state
    movel   dr_seed,%d3
    moveq   #63,%d6
ds_l:
    addl    %d4,%d2
    addl    %d5,%d4
    DR_SINE %d2
    mulsl   %sp@(4),%d0           | x the body's gain
    asrl    #8,%d0
    asrl    #7,%d0
    movea.l %d0,%a0               | a0 = the body
    DR_NOISE
    movel   %a4,%d0
    subl    %d0,%d1
    mulsl   %sp@(12),%d1
    asrl    #8,%d1
    asrl    #7,%d1
    addl    %d1,%d0               | the low-pass
    movea.l %d0,%a4
    subl    %a5,%d0               | less its own low end: 1/32 a sample, ~480 Hz
    movel   %d0,%d1
    asrl    #5,%d1
    adda.l  %d1,%a5
    mulsl   %sp@(8),%d0           | x the noise's weight
    asrl    #7,%d0
    addl    %a0,%d0
    DR_SAT
    DR_OUT
    subql   #1,%d6
    bplw    ds_l
    movel   %sp@(20),%d6
    lea.l   dr_ph1,%a0
    movel   %d2,%a0@(0,%d6:l:4)
    lea.l   dr_lp,%a0
    movel   %a4,%a0@(0,%d6:l:4)
    lea.l   dr_hp,%a0
    movel   %a5,%a0@(0,%d6:l:4)
    braw    df_out

| ---- HIHAT ----
| locals: sp@(0) = the squares' weight (128 - NOIS), sp@(4) = the noise's,
| sp@(8) = the high-pass coefficient (TONE: 3 .. 12 kHz); d7 / a4 = one
| square's step and the six's top, both scaled by the squares' weight
df_hat:
    movel   %a4@(8),%d0           | NOIS
    movel   %d0,%sp@(4)
    negl    %d0
    addil   #128,%d0
    movel   %d0,%sp@(0)
    movel   %a4@,%d0              | TONE: 3 .. 12 kHz (dr_ftab 73 .. 117)
    mulu.w  #89,%d0
    lsrl    #8,%d0
    addil   #73,%d0
    lea.l   dr_ftab,%a0
    movel   %a0@(0,%d0:l:4),%d0
    movel   %d0,%sp@(8)
    | the six increments, spread around their mean by SPRD (64 = as is)
    lea.l   dr_hat_r,%a2
    lea.l   dr_hinc,%a1
    moveq   #0,%d2                | their sum
    moveq   #5,%d4
dh_i:
    movel   %d3,%d0
    movel   %a2@+,%d1
    bsr     dr_mq14
    movel   %d0,%a1@+
    addl    %d0,%d2
    subql   #1,%d4
    bpls    dh_i
    movel   %d2,%d0               | the mean
    moveq   #6,%d1
    divul   %d1,%d0
    movel   %d0,%d2
    movel   %a4@(4),%d5           | SPRD 0..127 -> 0..254 / 128
    addl    %d5,%d5
    lea.l   dr_hinc,%a1
    moveq   #5,%d4
dh_s:
    movel   %a1@,%d0
    subl    %d2,%d0
    asrl    #2,%d0
    mulsl   %d5,%d0
    asrl    #5,%d0
    addl    %d2,%d0
    movel   %d0,%a1@+
    subql   #1,%d4
    bpls    dh_s
    movel   %d6,%d0               | this track's six phases
    muluw   #24,%d0
    lea.l   dr_hph,%a1
    adda.l  %d0,%a1
    lea.l   dr_hinc,%a2
    lea.l   dr_lp,%a0
    movel   %a0@(0,%d6:l:4),%d2   | d2 = the first high-pass's state
    lea.l   dr_hp,%a0
    movel   %a0@(0,%d6:l:4),%d4   | d4 = the second's
    movel   %sp@(0),%d7           | each square +-5000 x weight / 128: the sum
    mulu.w  #10000,%d7            | is top - (squares low) x step
    asrl    #7,%d7
    movel   %d7,%d0
    addl    %d0,%d0
    addl    %d7,%d0
    movea.l %d0,%a4               | top = 3 steps
    movel   dr_seed,%d3
    moveq   #63,%d6
dh_l:
    moveq   #0,%d5                | how many of the six are low
    .rept 6
    movel   %a2@+,%d0
    addl    %d0,%a1@+
    bpls    1f
    addql   #1,%d5
1:
    .endr
    lea.l   %a1@(-24),%a1
    lea.l   %a2@(-24),%a2
    mulsw   %d7,%d5
    movel   %a4,%d0
    subl    %d5,%d0               | the six's sum
    DR_NOISE
    mulsl   %sp@(4),%d1
    asrl    #7,%d1
    addl    %d0,%d1               | x
    subl    %d2,%d1               | x - lp1
    movel   %d1,%d0
    mulsl   %sp@(8),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    addl    %d0,%d2               | lp1 +=
    subl    %d0,%d1               | h1 = x - lp1
    subl    %d4,%d1               | h1 - lp2
    movel   %d1,%d0
    mulsl   %sp@(8),%d0
    asrl    #8,%d0
    asrl    #7,%d0
    addl    %d0,%d4               | lp2 +=
    subl    %d1,%d0
    negl    %d0                   | h2 = h1 - lp2
    DR_SAT
    DR_OUT
    subql   #1,%d6
    bplw    dh_l
    movel   %sp@(20),%d6
    lea.l   dr_lp,%a0
    movel   %d2,%a0@(0,%d6:l:4)
    lea.l   dr_hp,%a0
    movel   %d4,%a0@(0,%d6:l:4)

df_out:
    movel   %d3,dr_seed
    moveml  %sp@(24),%d2-%d7/%a2-%a5
    lea.l   %sp@(64),%sp
    rts

| ---- the drums' labels for the four borrowed ids (apply_names d1 = 3/4/5) ---
| Rows as va_swap's: param_table entry offset, our name / category / short,
| then the stock three to put back.
    .align 2
kick_swap:
    .long 2576, str_dk1, str_dkc, str_dk1s, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, str_dk2, str_dkc, str_dk2s, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, str_dsat, str_dkc, str_dsats, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, str_dk4, str_dkc, str_dk4s, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
snare_swap:
    .long 2576, str_ds1, str_dsc, str_ds1s, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, str_ds2, str_dsc, str_ds2s, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, str_ds3, str_dsc, str_ds3s, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, str_dsat, str_dsc, str_dsats, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
hat_swap:
    .long 2576, str_dh1, str_dhc, str_ds3s, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, str_dh2, str_dhc, str_dh2s, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, str_dh3, str_dhc, str_ds1s, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, str_dsat, str_dhc, str_dsats, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
str_dk1:     .asciz "Sweep Depth"
str_dk2:     .asciz "Sweep Time"
str_dk4:     .asciz "Click"
str_dsat:    .asciz "Drive"
str_ds1:     .asciz "Noise Mix"
str_ds2:     .asciz "Pitch Drop"
str_ds3:     .asciz "Noise Tone"
str_dh1:     .asciz "Tone"
str_dh2:     .asciz "Spread"
str_dh3:     .asciz "Noise"
str_dkc:     .asciz "VA Kick"
str_dsc:     .asciz "VA Snare"
str_dhc:     .asciz "VA Hihat"
str_dk1s:    .asciz "SWP"
str_dk2s:    .asciz "STM"
str_dk4s:    .asciz "CLK"
str_dsats:   .asciz "SAT"
str_ds1s:    .asciz "NOIS"
str_ds2s:    .asciz "PENV"
str_ds3s:    .asciz "TONE"
str_dh2s:    .asciz "SPRD"
    .align 2

| ---- state: per track, six tracks --------------------------------------------
    .balign 4
dr_p:      .space 6*16            | the four dials, 0..127
dr_ph1:    .space 6*4             | the kick's / snare OSC 1's phase
dr_ph2:    .space 6*4             | snare OSC 2's
dr_pe:     .space 6*4             | the pitch sweep, Q15
dr_ce:     .space 6*4             | the kick's click, Q30
dr_be:     .space 6*4             | the snare body's envelope, Q30
dr_lp:     .space 6*4             | the snare's noise low-pass / the hat's 1st high-pass
dr_hp:     .space 6*4             | the snare's low cut / the hat's 2nd high-pass
dr_hph:    .space 6*6*4           | the hat's six phases
dr_hinc:   .space 6*4             | this block's six hat increments
dr_seed:   .long 0x1f123bb5       | the noise's LCG
dr_gq:     .long 0                | this block's drive gain (dr_setgq)
dr_trig:   .space 8               | a new note for the next block
| Written over sound +42..+48 (k = 11..14) when a track becomes one of them,
| by mc_commit_hook, as dial << 8.
dr_defaults:
    .word 48<<8, 40<<8, 24<<8, 40<<8   | KICK:  SWP 48  STM 40  SAT 24  CLK 40
    .word 64<<8, 32<<8, 88<<8, 16<<8   | SNARE: NOIS 64 PENV 32 TONE 88 SAT 16
    .word 80<<8, 64<<8, 16<<8, 0       | HIHAT: TONE 80 SPRD 64 NOIS 16 SAT 0
    .include "drum_tables.inc"
    .align 2
