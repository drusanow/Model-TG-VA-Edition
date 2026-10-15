| ============================================================================
| ========================  Alternative send effects  ========================
|
| See docs/SENDFX.md. SETTINGS + PAGE opens the Send FX menu: its Delay row
| puts PING (ping-pong), TAPE or CHORUS (chorus / flanger) in the delay's
| place, its Reverb row PHASER, SPRING, RING (ring modulator) or DIST
| (distortion) in the reverb's. The sends, the FX page's four knobs and the
| delay's feed into the reverb are the stock ones; only what sits on each
| bus changes. Each pattern keeps its own pair, saved with its kit.
|
| THE TYPES are two bytes of the kit's LFO 3/4 block (lfo2.inc: L34_BLK = 124,
| +120 the delay's, +121 the reverb's), so they go wherever a kit goes:
| saved at +646 of its storage record with their complement at +647 (a
| record without it - every older one - loads the stock effects), copied
| with it (kit_memcpy), and back to the stock effects when its FX are reset
| to their defaults (sfx_fxd, on 0x40061318: a new kit, a new project).
|
| THE AUDIO. The output stage calls the delay 0x40057488 (out 0x8000ba90, in
| 0x8000b990, params state+428: Time, Feedback) through fx_a, and the reverb
| 0x400579c4 (out 0x8000bb90, in, params state+432: Size, Tone) through fx_b.
| Both now go through sfx_dly / sfx_rev. With the stock type they are a
| jump to the stock code. With another, the stock effect still runs - fed
| silence (SFX_ZERO, cleared before each call: the stock reverb uses its
| input buffer as scratch) so that what it holds dies away, its rings,
| positions and DMA exactly as always - and ours then writes the bus's
| output in its place. A type that changes starts from silence: its lines
| are cleared, a 16 KiB chunk a block, while it puts out silence (32 blocks,
| 21 ms, for the delay's; 4 for the spring's). Samples are 32-bit (16-bit <<
| 16); the effects work on the top 16 bits, coefficients Q14.
|
| THE MEMORY. The delay lines (2 x 131072 samples, 16-bit: 2.7 s) sit at
| SFX_DBUF and the spring's eight short lines at SFX_RBUF, below the slide
| trigs, in the cached part of the sample region (model_tg.s: PCM_TOP).
| ============================================================================

SFX_DT    = 120               | in a kit's LFO 3/4 block: the delay's type
SFX_RT    = 121               | ... and the reverb's
SFX_ND    = 4                 | DELAY, PING, TAPE, CHORUS
SFX_NR    = 5                 | REVERB, PHASER, SPRING, RING, DIST
SFX_ST    = 646               | in the kit's storage record: the types (delay
                              | low nibble, reverb high), +647 their complement
SFX_DMASK = 0x1ffff           | a delay line: 131072 samples
SFX_RSZ   = 0x10000             | the spring's lines: 8 x 4096 samples
| past them in SFX_RBUF, cleared once (sfx_init): the stock effects' silent
| input, the delay loop's scratch, and the reverb's state (cleared at every
| change of its type)
SFX_ZERO  = SFX_RBUF + 0x1f000   | 256 bytes of silence
sfx_st    = SFX_ZERO + 256        | the delay loop's scratch (S_*)
sfx_rs    = SFX_ZERO + 512        | the reverb's state:
sfx_rph   = sfx_rs                | the phaser's LFO / the ring's carrier
sfx_phl   = sfx_rs + 4            | the phaser's sides: feedback, 6 x {x1, y1}
sfx_phr   = sfx_rs + 56
sfx_splp  = sfx_rs + 108          | the spring's combs' lowpasses, left
sfx_sprp  = sfx_rs + 116          | ... right
sfx_rlp   = sfx_rs + 124          | DIST's lowpasses
SFX_RSN   = 33                    | longs of it
SFX_CHUNK = 0x4000            | cleared a block
SFX_DMAX  = 130000            | PING / TAPE: the longest time, samples
SFX_WOW   = 1717987           | TAPE: wow at 0.6 Hz and flutter at 7 Hz,
SFX_FLT   = 20042862          | phase steps a block
SFX_TAPEA = 7373              | TAPE: the repeats' lowpass, Q14 (0.45)

| the delay loop's scratch, at a4
S_FB  = 0                     | feedback, Q14
S_RL  = 4                     | the lines' outputs
S_RR  = 8
S_XL  = 12                    | the inputs
S_XR  = 16
S_WL  = 20                    | what goes into the lines
S_WR  = 24
S_OL  = 28                    | what comes out
S_OR  = 32
S_LPL = 36                    | TAPE's lowpasses
S_LPR = 40
S_MODE = 44                   | 1 PING, 2 TAPE, 3 CHORUS

    .align 2
| sfx_kitb -> a0 = the playing kit's LFO 3/4 block (l34_blk). Clobbers d0/d1.
sfx_kitb:
    movel   CUR_KIT,%d0
    jmp     l34_blk

| sfx_type(d1 = SFX_DT or SFX_RT, d2 = how many types) -> d0 = the playing
| kit's type for that slot, 0 when out of range. Clobbers d1/a0.
sfx_type:
    movel   %d1,%sp@-
    bsr     sfx_kitb
    movel   %sp@+,%d1
    moveq   #0,%d0
    moveb   %a0@(0,%d1:l),%d0
    cmpl    %d2,%d0
    bcss    sty_ok
    moveq   #0,%d0
sty_ok:
    rts

| sfx_init: the first time an effect changes, SFX_ZERO .. the reverb's state
| cleared (the sample region is not, at power-on). Keeps d0.
sfx_init:
    tstl    sfx_ok
    bnes    sin_out
    movel   %d0,sfx_ok
    lea.l   SFX_ZERO,%a0
    movel   #160-1,%d1
sin_l:
    clrl    %a0@+
    subql   #1,%d1
    bpls    sin_l
    moveq   #1,%d1
    movel   %d1,sfx_ok
sin_out:
    rts

| sfx_z: SFX_ZERO silent again - the stock reverb works in its input buffer.
| Clobbers d1/a0.
sfx_z:
    lea.l   SFX_ZERO,%a0
    moveq   #63,%d1
sz_l:
    clrl    %a0@+
    subql   #1,%d1
    bpls    sz_l
    rts

| sfx_clr(a0 = a buffer, d0 = how much of it is clear) -> d0 = one chunk
| more. Clobbers d1/a0.
sfx_clr:
    adda.l  %d0,%a0
    addil   #SFX_CHUNK,%d0
    movel   #SFX_CHUNK/16-1,%d1
sfc_l:
    clrl    %a0@+
    clrl    %a0@+
    clrl    %a0@+
    clrl    %a0@+
    subql   #1,%d1
    bpls    sfc_l
    rts

| ---- the delay's slot: in place of 0x40057488 (out, in, params) -------------
sfx_dly:
    movel   %d2,%sp@-
    moveq   #SFX_DT,%d1
    moveq   #SFX_ND,%d2
    bsr     sfx_type
    movel   %sp@+,%d2
    cmpl    sfx_dt,%d0
    beqs    sd_same
    movel   %d0,sfx_dt            | another type: from silence
    bsr     sfx_init
    clrl    sfx_dcl
    clrl    sfx_db
    lea.l   sfx_st,%a0
    clrl    %a0@(S_LPL)
    clrl    %a0@(S_LPR)
sd_same:
    tstl    sfx_dt
    bnes    sd_alt
    jmp     0x40057488            | the stock delay
sd_alt:
    bsr     sfx_z
    movel   %sp@(12),%sp@-        | the stock delay, fed silence: what it holds
    pea     SFX_ZERO              | dies away, its ring and DMA as always
    movel   %sp@(12),%sp@-
    jsr     0x40057488
    lea.l   %sp@(12),%sp
    movel   sfx_dcl,%d0
    cmpil   #2*(SFX_DMASK+1)*2,%d0
    bccs    sfx_ddsp
    lea.l   SFX_DBUF,%a0          | still clearing the lines: silence
    bsr     sfx_clr
    movel   %d0,sfx_dcl
    movea.l %sp@(4),%a0
    jmp     fx_zero

| (out, in, params): PING, TAPE or CHORUS, 32 stereo frames.
sfx_ddsp:
    lea.l   %sp@(-44),%sp
    moveml  %d2-%d7/%a2-%a6,%sp@
    movea.l %sp@(56),%a0          | the params
    moveq   #0,%d4
    movew   %a0@,%d4              | Time
    andil   #0x7fff,%d4
    moveq   #0,%d5
    movew   %a0@(2),%d5           | Feedback
    andil   #0x7fff,%d5
    lea.l   sfx_st,%a4
    movel   sfx_dt,%d0
    movel   %d0,%a4@(S_MODE)
    subql   #3,%d0
    beqw    sd_chor
    | PING and TAPE: the stock delay's time - (Time + 256) x 48000 / 1024 x
    | 900 / tempo, the tempo in 1/120 BPM - capped to the lines
    movel   %d4,%d0
    addil   #256,%d0
    movel   #48000,%d1
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #2,%d0
    movel   #900,%d1
    mulsl   %d1,%d0
    movel   0x40149310,%d1
    beqs    sd_tmax
    divul   %d1,%d0
    cmpil   #SFX_DMAX,%d0
    blss    sd_tmin
sd_tmax:
    movel   #SFX_DMAX,%d0
sd_tmin:
    cmpil   #128,%d0
    bccs    sd_t8
    movel   #128,%d0
sd_t8:
    lsll    #8,%d0                | Q8
    movel   sfx_db,%d1            | the time glides there: a 16th of the
    bnes    sd_gl                 | way a block, 24 samples a block at most
    movel   %d0,%d1               | (a new type: there at once)
    movel   %d0,sfx_dl
    movel   %d0,sfx_dr
sd_gl:
    movel   %d0,%d2
    subl    %d1,%d2
    asrl    #4,%d2
    cmpil   #6144,%d2
    bles    sd_g1
    movel   #6144,%d2
sd_g1:
    cmpil   #-6144,%d2
    bges    sd_g2
    movel   #-6144,%d2
sd_g2:
    addl    %d2,%d1
    movel   %d1,sfx_db
    movel   %d1,%d2               | the targets, left and right
    movel   %d1,%d3
    movel   %d5,%d0               | feedback: the stock's, 0 .. 32512 -> Q14
    cmpil   #32512,%d0
    bles    sdf_1
    movel   #32512,%d0
sdf_1:
    asrl    #1,%d0
    movel   %d0,%a4@(S_FB)
    moveq   #2,%d0
    cmpl    %a4@(S_MODE),%d0
    bnew    sd_steps
    | TAPE: a little more feedback (the saturation holds it), and the time
    | wobbles: wow +-48 samples, flutter +-2
    movel   %d5,%d0
    movel   #17000,%d1
    mulsl   %d1,%d0
    asrl    #8,%d0
    asrl    #7,%d0
    movel   %d0,%a4@(S_FB)
    movel   sfx_ph1,%d0
    addil   #SFX_WOW,%d0
    movel   %d0,sfx_ph1
    bsr     sfx_tri
    movel   #12288,%d1
    mulsl   %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %d0,%d2
    movel   sfx_ph2,%d0
    addil   #SFX_FLT,%d0
    movel   %d0,sfx_ph2
    bsr     sfx_tri
    asrl    #5,%d0                | x 512 / 16384
    addl    %d0,%d2
    movel   %d2,%d3
    braw    sd_steps
    | CHORUS: Time is the LFO's speed. Feedback up to half: a chorus, 15 ms
    | +- up to 5 ms, no feedback; above: a flanger, 2 ms +- 1.8 ms, its
    | feedback rising to 0.9. Right's LFO a quarter turn on.
sd_chor:
    movel   %d4,%d0
    lsrl    #8,%d0
    lea.l   sfx_rate,%a0
    movel   %a0@(0,%d0:l:4),%d0
    addl    %d0,sfx_ph1
    cmpil   #16384,%d5
    bges    sd_flg
    movel   #720*256,%d1          | the centre, Q8
    movel   %d5,%d2               | the depth: Feedback x 15/4, Q8
    lsll    #4,%d2
    subl    %d5,%d2
    asrl    #2,%d2
    clrl    %a4@(S_FB)
    bras    sd_mod
sd_flg:
    movel   #96*256,%d1
    movel   #22528,%d2
    movel   %d5,%d0
    subil   #16384,%d0
    movel   #14746,%d3
    mulsl   %d3,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    movel   %d0,%a4@(S_FB)
sd_mod:
    movel   sfx_ph1,%d0
    bsr     sfx_tri
    mulsl   %d2,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %d1,%d0
    movel   %d0,%d6               | left's target
    movel   sfx_ph1,%d0
    addil   #0x40000000,%d0
    bsr     sfx_tri
    mulsl   %d2,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %d1,%d0
    movel   %d0,%d3               | right's
    movel   %d6,%d2
    tstl    sfx_db
    bnes    sd_steps
    moveq   #1,%d0                | a new type: there at once
    movel   %d0,sfx_db
    movel   %d2,sfx_dl
    movel   %d3,sfx_dr
    | each side's read point reaches its target over the block
sd_steps:
    movel   %d2,%d5
    subl    sfx_dl,%d5
    asrl    #5,%d5
    movel   %d3,%d6
    subl    sfx_dr,%d6
    asrl    #5,%d6
    movea.l %sp@(52),%a0          | in
    movea.l %sp@(48),%a1          | out
    lea.l   SFX_DBUF,%a2          | the left line
    lea.l   SFX_DBUF+(SFX_DMASK+1)*2,%a3 | the right
    movel   sfx_wp,%d2
    movel   sfx_dl,%d3
    movel   sfx_dr,%d4
    moveq   #32,%d7
sdl_l:
    movea.l %a2,%a5
    movel   %d3,%d1
    bsr     sfx_rd
    movel   %d0,%a4@(S_RL)
    movea.l %a3,%a5
    movel   %d4,%d1
    bsr     sfx_rd
    movel   %d0,%a4@(S_RR)
    movel   %a0@+,%d0
    swap    %d0
    extl    %d0
    movel   %d0,%a4@(S_XL)
    movel   %a0@+,%d0
    swap    %d0
    extl    %d0
    movel   %d0,%a4@(S_XR)
    movel   %a4@(S_MODE),%d0
    subql   #2,%d0
    beqw    sdl_tape
    bplw    sdl_chor
    | PING: the input, in mono, enters left; each side feeds the other
    movel   %a4@(S_RR),%d0
    bsr     sfx_fb
    movel   %a4@(S_XL),%d1
    addl    %a4@(S_XR),%d1
    asrl    #1,%d1
    addl    %d1,%d0
    movel   %d0,%a4@(S_WL)
    movel   %a4@(S_RL),%d0
    bsr     sfx_fb
    movel   %d0,%a4@(S_WR)
    braw    sdl_ro
    | TAPE: each side's repeats darker, and squashed as they pile up
sdl_tape:
    movel   #SFX_TAPEA,%d1
    movel   %a4@(S_RL),%d0
    subl    %a4@(S_LPL),%d0
    mulsl   %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %d0,%a4@(S_LPL)
    movel   %a4@(S_RR),%d0
    subl    %a4@(S_LPR),%d0
    mulsl   %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %d0,%a4@(S_LPR)
    movel   %a4@(S_LPL),%d0
    bsr     sfx_fb
    bsr     sfx_sat
    addl    %a4@(S_XL),%d0
    movel   %d0,%a4@(S_WL)
    movel   %a4@(S_LPR),%d0
    bsr     sfx_fb
    bsr     sfx_sat
    addl    %a4@(S_XR),%d0
    movel   %d0,%a4@(S_WR)
    movel   %a4@(S_LPL),%a4@(S_OL)
    movel   %a4@(S_LPR),%a4@(S_OR)
    bras    sdl_w
    | CHORUS: each side's own, the flanger's feedback on itself
sdl_chor:
    movel   %a4@(S_RL),%d0
    bsr     sfx_fb
    addl    %a4@(S_XL),%d0
    movel   %d0,%a4@(S_WL)
    movel   %a4@(S_RR),%d0
    bsr     sfx_fb
    addl    %a4@(S_XR),%d0
    movel   %d0,%a4@(S_WR)
sdl_ro:
    movel   %a4@(S_RL),%a4@(S_OL)
    movel   %a4@(S_RR),%a4@(S_OR)
sdl_w:
    movel   %a4@(S_WL),%d0
    bsr     sfx_c16
    movew   %d0,%a2@(0,%d2:l:2)
    movel   %a4@(S_WR),%d0
    bsr     sfx_c16
    movew   %d0,%a3@(0,%d2:l:2)
    movel   %a4@(S_OL),%d0
    swap    %d0
    clrw    %d0
    movel   %d0,%a1@+
    movel   %a4@(S_OR),%d0
    swap    %d0
    clrw    %d0
    movel   %d0,%a1@+
    addql   #1,%d2
    andil   #SFX_DMASK,%d2
    addl    %d5,%d3
    addl    %d6,%d4
    subql   #1,%d7
    bnew    sdl_l
    movel   %d2,sfx_wp
    movel   %d3,sfx_dl
    movel   %d4,sfx_dr
    moveml  %sp@,%d2-%d7/%a2-%a6
    lea.l   %sp@(44),%sp
    rts

| sfx_rd(a5 = a line, d1 = its delay in samples Q8, d2 = the write point)
| -> d0 = the line there, linearly interpolated. Clobbers d1/a6.
sfx_rd:
    movel   %d2,%d0
    lsll    #8,%d0
    subl    %d1,%d0               | the read point, Q8
    movel   %d0,%d1
    andil   #255,%d1              | its fraction
    asrl    #8,%d0
    andil   #SFX_DMASK,%d0
    movea.l %d0,%a6
    addql   #1,%d0
    andil   #SFX_DMASK,%d0
    movew   %a5@(0,%d0:l:2),%d0
    extl    %d0                   | the newer sample
    movea.w %a5@(0,%a6:l:2),%a6   | the older
    subl    %a6,%d0
    mulsl   %d1,%d0
    asrl    #8,%d0
    addl    %a6,%d0
    rts

| sfx_fb(d0) -> d0 x S_FB (Q14), a4 = sfx_st.
sfx_fb:
    mulsl   %a4@(S_FB),%d0
    asrl    #7,%d0
    asrl    #7,%d0
    rts

| sfx_tri(d0 = a phase) -> d0 = its triangle, -16384 .. 16383.
sfx_tri:
    tstl    %d0
    bpls    stri_p
    notl    %d0
stri_p:
    swap    %d0
    andil   #0x7fff,%d0
    subil   #16384,%d0
    rts

| sfx_c16(d0) -> d0 within +-32767.
sfx_c16:
    cmpil   #32767,%d0
    bles    sc16_lo
    movel   #32767,%d0
    rts
sc16_lo:
    cmpil   #-32767,%d0
    bges    sc16_ok
    movel   #-32767,%d0
sc16_ok:
    rts

| sfx_sat(d0) -> d0 soft-clipped: unity for small signals, 32767 at most
| (v = 2/3 d0 clipped to +-32767, then 3/2 v - 1/2 v^3). Clobbers d1.
sfx_sat:
    cmpil   #49151,%d0
    bles    ssat_lo
    movel   #49151,%d0
ssat_lo:
    cmpil   #-49151,%d0
    bges    ssat_ok
    movel   #-49151,%d0
ssat_ok:
    movel   #10923,%d1
    mulsl   %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0                | v
    movel   %d0,%sp@-
    movel   %d0,%d1
    mulsl   %d0,%d1
    asrl    #8,%d1
    asrl    #7,%d1
    mulsl   %d0,%d1
    asrl    #8,%d1
    asrl    #7,%d1                | v^3, 16-bit
    subl    %d1,%d0
    asrl    #1,%d0
    addl    %sp@+,%d0
    rts

| ---- the reverb's slot: in place of 0x400579c4 (out, in, params) ------------
sfx_rev:
    movel   %d2,%sp@-
    moveq   #SFX_RT,%d1
    moveq   #SFX_NR,%d2
    bsr     sfx_type
    movel   %sp@+,%d2
    cmpl    sfx_rt,%d0
    beqs    sr_same
    movel   %d0,sfx_rt            | another type: from silence
    bsr     sfx_init
    clrl    sfx_rcl
    lea.l   sfx_rs,%a0
    moveq   #SFX_RSN-1,%d1
sr_z:
    clrl    %a0@+
    subql   #1,%d1
    bpls    sr_z
sr_same:
    tstl    sfx_rt
    bnes    sr_alt
    jmp     0x400579c4            | the stock reverb
sr_alt:
    bsr     sfx_z
    movel   %sp@(12),%sp@-        | the stock reverb, fed silence
    pea     SFX_ZERO
    movel   %sp@(12),%sp@-
    jsr     0x400579c4
    lea.l   %sp@(12),%sp
    movel   sfx_rcl,%d0
    cmpil   #SFX_RSZ,%d0
    bccs    sfx_rdsp
    lea.l   SFX_RBUF,%a0          | still clearing the spring: silence
    bsr     sfx_clr
    movel   %d0,sfx_rcl
    movea.l %sp@(4),%a0
    jmp     fx_zero

| (out, in, params): PHASER, SPRING, RING or DIST, 32 stereo frames. Each
| side through sr_one: d0 = its input (16-bit) -> d0, then clipped, out.
sfx_rdsp:
    lea.l   %sp@(-44),%sp
    moveml  %d2-%d7/%a2-%a6,%sp@
    movea.l %sp@(56),%a0          | the params
    moveq   #0,%d4
    movew   %a0@,%d4              | Size
    andil   #0x7fff,%d4
    moveq   #0,%d5
    movew   %a0@(2),%d5           | Tone
    andil   #0x7fff,%d5
    movel   sfx_rt,%d0
    subql   #2,%d0
    bmiw    sr_phas
    beqw    sr_sprg
    subql   #1,%d0
    beqw    sr_ring
    | DIST: Size the drive (x1 .. x200), Tone a lowpass after it (dark to
    | open), -6 dB
    lsrl    #8,%d4
    lea.l   sfx_rate,%a0          | the LFO's curve: x1.06 .. x218, Q4
    movel   %a0@(0,%d4:l:4),%d4
    moveq   #13,%d0
    lsrl    %d0,%d4
    movel   #14745,%d0
    mulsl   %d0,%d5
    asrl    #8,%d5
    asrl    #7,%d5
    addil   #1638,%d5
    movea.l %sp@(52),%a0
    movea.l %sp@(48),%a1
    moveq   #32,%d7
srd_l:
    lea.l   sfx_rlp,%a2
    bsr     srd_one
    lea.l   sfx_rlp+4,%a2
    bsr     srd_one
    subql   #1,%d7
    bnes    srd_l
    braw    sr_out
srd_one:
    movel   %a0@+,%d0
    swap    %d0
    extl    %d0
    mulsl   %d4,%d0
    asrl    #4,%d0
    bsr     sfx_sat
    asrl    #1,%d0
    subl    %a2@,%d0
    mulsl   %d5,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %a2@,%d0
    movel   %d0,%a2@
    braw    sr_put

    | PHASER: six allpasses a side, swept 100 Hz .. 4 kHz by an LFO (Size its
    | speed, right a quarter turn on); Tone the feedback, up to 0.7
sr_phas:
    lsrl    #8,%d4
    lea.l   sfx_rate,%a0
    movel   %a0@(0,%d4:l:4),%d0
    addl    %d0,sfx_rph
    movel   #11469,%d4
    mulsl   %d5,%d4
    asrl    #8,%d4
    asrl    #7,%d4
    movel   sfx_rph,%d0
    bsr     sfx_apk
    movel   %d0,%d5
    movel   sfx_rph,%d0
    addil   #0x40000000,%d0
    bsr     sfx_apk
    movel   %d0,%d6
    movea.l %sp@(52),%a0
    movea.l %sp@(48),%a1
    moveq   #32,%d7
srp_l:
    movel   %a0@+,%d0
    swap    %d0
    extl    %d0
    lea.l   sfx_phl,%a5
    movel   %d5,%d1
    bsr     sfx_phs
    bsr     sr_put
    movel   %a0@+,%d0
    swap    %d0
    extl    %d0
    lea.l   sfx_phr,%a5
    movel   %d6,%d1
    bsr     sfx_phs
    bsr     sr_put
    subql   #1,%d7
    bnes    srp_l
    braw    sr_out

    | SPRING: four allpasses, then two damped combs a side; Size the combs'
    | feedback (0.5 .. 0.97), Tone their damping (dark to bright)
sr_sprg:
    movel   #7700,%d0
    mulsl   %d0,%d4
    asrl    #8,%d4
    asrl    #7,%d4
    addil   #8192,%d4
    movel   #13926,%d0
    mulsl   %d0,%d5
    asrl    #8,%d5
    asrl    #7,%d5
    addil   #2458,%d5
    lea.l   SFX_RBUF,%a4
    movel   sfx_rwp,%d6
    movea.l %sp@(52),%a0
    movea.l %sp@(48),%a1
    moveq   #32,%d7
srs_l:
    movel   %a0@+,%d0             | a spring is mono: (L + R) / 2 in
    swap    %d0
    extl    %d0
    movel   %a0@+,%d1
    swap    %d1
    extl    %d1
    addl    %d1,%d0
    asrl    #1,%d0
    lea.l   sfx_spl,%a5
    bsr     sfx_spa
    movea.l %d0,%a3
    lea.l   sfx_splp,%a6          | then a pair of combs a side
    bsr     sfx_spc
    bsr     sr_put
    lea.l   sfx_sprp,%a6
    bsr     sfx_spc
    bsr     sr_put
    addql   #1,%d6
    andil   #4095,%d6
    subql   #1,%d7
    bnes    srs_l
    movel   %d6,sfx_rwp
    braw    sr_out

    | RING: the input times a carrier, 20 Hz .. 4 kHz (Size); Tone shapes it
    | from a triangle to a square. Right's a quarter turn on.
sr_ring:
    lsrl    #8,%d4
    lea.l   sfx_rate,%a0          | the LFO's curve x 12.5: 20 Hz .. 4 kHz
    movel   %a0@(0,%d4:l:4),%d0
    movel   %d0,%d4
    lsrl    #1,%d4                | r / 2
    lsll    #2,%d0
    addl    %d0,%d4               | + 4 r
    addl    %d0,%d0
    addl    %d0,%d4               | + 8 r
    lsrl    #8,%d5
    lsrl    #3,%d5
    addql   #1,%d5
    lsll    #4,%d5                | x1 .. x16, Q4
    movel   sfx_rph,%d6
    movea.l %sp@(52),%a0
    movea.l %sp@(48),%a1
    moveq   #32,%d7
srr_l:
    movel   %d6,%d0
    bsr     srr_one
    movel   %d6,%d0
    addil   #0x40000000,%d0
    bsr     srr_one
    addl    %d4,%d6
    subql   #1,%d7
    bnes    srr_l
    movel   %d6,sfx_rph
    braw    sr_out
srr_one:                          | d0 = the carrier's phase
    bsr     sfx_tri
    mulsl   %d5,%d0
    asrl    #4,%d0
    cmpil   #16384,%d0
    bles    srr_c1
    movel   #16384,%d0
srr_c1:
    cmpil   #-16384,%d0
    bges    srr_c2
    movel   #-16384,%d0
srr_c2:
    movel   %a0@+,%d1
    swap    %d1
    extl    %d1
    mulsl   %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0
| sr_put(d0): clipped to 16 bits, out to a1. Clobbers d0.
sr_put:
    bsr     sfx_c16
    swap    %d0
    clrw    %d0
    movel   %d0,%a1@+
    rts
sr_out:
    moveml  %sp@,%d2-%d7/%a2-%a6
    lea.l   %sp@(44),%sp
    rts

| sfx_apk(d0 = the LFO's phase) -> d0 = the allpass coefficient there (Q14,
| sfx_apc interpolated). Clobbers d1/a6.
sfx_apk:
    movel   %d2,%sp@-
    tstl    %d0
    bpls    sapk_p
    notl    %d0
sapk_p:
    swap    %d0
    andil   #0x7fff,%d0           | 0 .. 32767
    movel   %d0,%d1
    andil   #511,%d1
    lsrl    #8,%d0
    lsrl    #1,%d0                | 0 .. 63
    lea.l   sfx_apc,%a6
    movew   %a6@(2,%d0:l:2),%d2
    extl    %d2
    movew   %a6@(0,%d0:l:2),%d0
    extl    %d0
    subl    %d0,%d2
    mulsl   %d1,%d2
    asrl    #8,%d2
    asrl    #1,%d2
    addl    %d2,%d0
    movel   %sp@+,%d2
    rts

| sfx_phs(d0 = x, d1 = the coefficient, a5 = {feedback, 6 x {x1, y1}}, d4 =
| the feedback amount) -> d0 = (x + phased) / 2. Clobbers d1-d3/a6.
sfx_phs:
    movel   %d0,%d3
    movel   %a5@,%d2
    mulsl   %d4,%d2
    asrl    #7,%d2
    asrl    #7,%d2
    addl    %d2,%d0
    bsr     sfx_c16
    lea.l   %a5@(4),%a6
    .rept 6                       | y = x1 + a (x - y1)
    movel   %d0,%d2
    subl    %a6@(4),%d2
    asrl    #1,%d2
    mulsl   %d1,%d2
    asrl    #6,%d2
    asrl    #7,%d2
    addl    %a6@,%d2
    movel   %d0,%a6@+
    movel   %d2,%a6@+
    movel   %d2,%d0
    .endr
    movel   %d0,%a5@
    addl    %d3,%d0
    asrl    #1,%d0
    rts

| sfx_spa(d0 = x; a5 = the lines {offset, delay}, a4 = SFX_RBUF, d6 = the
| write point) -> d0 through four allpasses (v = x - g b, y = b + g v, g =
| 5/8), a5 past them. Clobbers d1-d3/a2/a3.
sfx_spa:
    moveq   #3,%d3
ssp_ap:
    movea.l %a4,%a2
    adda.l  %a5@+,%a2
    movel   %d6,%d1
    subl    %a5@+,%d1
    andil   #4095,%d1
    movew   %a2@(0,%d1:l:2),%d1
    extl    %d1                   | b
    movea.l %d1,%a3
    movel   %d1,%d2
    asrl    #1,%d2
    asrl    #3,%d1
    addl    %d1,%d2
    subl    %d2,%d0
    bsr     sfx_c16               | v
    movew   %d0,%a2@(0,%d6:l:2)
    movel   %d0,%d2
    asrl    #1,%d2
    asrl    #3,%d0
    addl    %d2,%d0
    addl    %a3,%d0               | y
    subql   #1,%d3
    bpls    ssp_ap
    rts

| sfx_spc(a3 = x; a5 = two lines {offset, delay}, a6 = their lowpasses; a4,
| d6 as sfx_spa; d4 = the feedback, d5 = the damping) -> d0 = the two combs'
| outputs / 2, a5 and a6 past them. Clobbers d1/d3/a2.
sfx_spc:
    moveq   #0,%d3                | their sum
    .rept 2
    movea.l %a4,%a2
    adda.l  %a5@+,%a2
    movel   %d6,%d1
    subl    %a5@+,%d1
    andil   #4095,%d1
    movew   %a2@(0,%d1:l:2),%d1
    extl    %d1                   | b
    addl    %d1,%d3
    subl    %a6@,%d1              | the damping lowpass
    mulsl   %d5,%d1
    asrl    #7,%d1
    asrl    #7,%d1
    addl    %a6@,%d1
    movel   %d1,%a6@+
    mulsl   %d4,%d1
    asrl    #7,%d1
    asrl    #7,%d1
    addl    %a3,%d1
    movel   %d1,%d0
    bsr     sfx_c16
    movew   %d0,%a2@(0,%d6:l:2)
    .endr
    movel   %d3,%d0
    asrl    #1,%d0
    rts

| ---- the kit's FX back to their defaults: its send effects to the stock -----
| In place of 0x40061318's prologue `lea sp@(-28),sp ; moveml d2/a2-a4,sp@`:
| (the kit's FX block, kit + 628) - a new kit, a new project, a kit whose FX
| failed to load.
sfx_fxd:
    movel   %sp@(4),%d0
    subil   #628,%d0
    jsr     l34_blk
    tstl    %d0
    bmis    sfd_go
    clrw    %a0@(SFX_DT)          | both types
sfd_go:
    lea.l   %sp@(-28),%sp
    moveml  %d2/%a2-%a4,%sp@
    jmp     0x40061320

| ---- the Send FX menu (SETTINGS + PAGE) -------------------------------------
| The Granular menu's list (lm_build), two rows: Delay and Reverb. Each shows
| and turns the playing pattern's type.
sfx_menu_open:
    linkw   %fp,#-80
    lea.l   %sp@(-28),%sp
    moveml  %d2-%d4/%a2-%a5,%sp@
    lea.l   mm_opened,%a0
    clrl    %a0@
    moveq   #0,%d0
    movew   %sr,%d0
    andil   #0x0700,%d0
    bnes    sfm_done
    tstl    0x40fe4178
    beqs    sfm_done
    jsr     gm_live               | a list menu already up: no second copy
    tstl    %d0
    bnes    sfm_opened
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    lea.l   %sp@(8),%sp
    cmpil   #MAX_TRK,%d0
    bccs    sfm_done
    lea.l   gm_track,%a0
    movel   %d0,%a0@
    lea.l   sfx_desc,%a0
    lea.l   gm_cur,%a1
    movel   %a0,%a1@
    jmp     lm_build
sfm_opened:
    jmp     gm_opened
sfm_done:
    jmp     gm_done

sfx_lab_d:
    lea.l   sfx_s_dly,%a1
    jmp     gm_lab
sfx_lab_r:
    lea.l   sfx_s_rev,%a1
    jmp     gm_lab
sfx_val_d:
    movel   %d2,%sp@-
    moveq   #SFX_DT,%d1
    moveq   #SFX_ND,%d2
    bsr     sfx_type
    movel   %sp@+,%d2
    movel   %d0,sfx_v
    lea.l   sfx_v,%a0
    lea.l   sfx_n_d,%a1
    jmp     gm_val
sfx_val_r:
    movel   %d2,%sp@-
    moveq   #SFX_RT,%d1
    moveq   #SFX_NR,%d2
    bsr     sfx_type
    movel   %sp@+,%d2
    movel   %d0,sfx_v
    lea.l   sfx_v,%a0
    lea.l   sfx_n_r,%a1
    jmp     gm_val
| turn (closure, item, delta): one type a click, clamped at the ends
sfx_rot_d:
    moveq   #SFX_DT,%d1
    moveq   #SFX_ND,%d0
    bras    sfx_rot
sfx_rot_r:
    moveq   #SFX_RT,%d1
    moveq   #SFX_NR,%d0
sfx_rot:                          | d1 = the type's offset, d0 = how many
    lea.l   %sp@(-8),%sp
    moveml  %d2-%d3,%sp@
    movel   %d0,%d3
    movel   %d1,%d2
    movel   %sp@(20),%d0          | the turn
    beqs    srot_out
    movel   %d0,%sp@-
    bsr     sfx_kitb
    movel   %sp@+,%d1
    moveq   #0,%d0
    moveb   %a0@(0,%d2:l),%d0
    cmpl    %d3,%d0
    bcss    srot_in
    moveq   #0,%d0
srot_in:
    tstl    %d1
    bgts    srot_up
    subql   #1,%d0
    bpls    srot_set
    moveq   #0,%d0
    bras    srot_set
srot_up:
    addql   #1,%d0
    cmpl    %d3,%d0
    bcss    srot_set
    movel   %d3,%d0
    subql   #1,%d0
srot_set:
    moveb   %d0,%a0@(0,%d2:l)
srot_out:
    moveml  %sp@,%d2-%d3
    lea.l   %sp@(8),%sp
    rts

    .align 4
sfx_desc:     .long sfx_title, sfx_vname, 2, sfx_items
sfx_items:    .long sfx_lab_d, sfx_val_d, sfx_rot_d, new_slot2_invoker
              .long sfx_lab_r, sfx_val_r, sfx_rot_r, new_slot2_invoker
sfx_n_d:      .long sfx_s_d0, sfx_s_d1, sfx_s_d2, sfx_s_d3
sfx_n_r:      .long sfx_s_r0, sfx_s_r1, sfx_s_r2, sfx_s_r3, sfx_s_r4
| the spring's lines: {offset in SFX_RBUF, delay}, four allpasses, then two
| combs left and two right (8 KiB, 4096 samples, a line)
sfx_spl:      .long 0,142, 8192,107, 16384,379, 24576,277
              .long 32768,1557, 40960,1867, 49152,1617, 57344,1777
sfx_v:        .long 0          | a row's value, for gm_val
sfx_dt:       .long 0          | the delay slot's type running
sfx_dcl:      .long 0          | ... how much of its lines is clear
sfx_db:       .long 0          | PING / TAPE: the time, gliding (Q8); 0 = new
sfx_dl:       .long 0          | each side's read point (Q8 samples back)
sfx_dr:       .long 0
sfx_wp:       .long 0          | the lines' write point
sfx_ph1:      .long 0          | TAPE's wow / CHORUS's LFO
sfx_ph2:      .long 0          | TAPE's flutter
sfx_rt:       .long 0          | the reverb slot's type running
sfx_rcl:      .long 0          | ... how much of the spring's lines is clear
sfx_rwp:      .long 0          | the spring's write point
sfx_ok:       .long 0          | sfx_init has run
sfx_title:    .asciz "Send FX"
sfx_vname:    .asciz "SendFxMenuView"
sfx_s_dly:    .asciz "Delay"
sfx_s_rev:    .asciz "Reverb"
sfx_s_d0:     .asciz "DELAY"
sfx_s_d1:     .asciz "PING"
sfx_s_d2:     .asciz "TAPE"
sfx_s_d3:     .asciz "CHORUS"
sfx_s_r0:     .asciz "REVERB"
sfx_s_r1:     .asciz "PHASER"
sfx_s_r2:     .asciz "SPRING"
sfx_s_r3:     .asciz "RING"
sfx_s_r4:     .asciz "DIST"
    .align 2
    .include "sfx_tables.inc"
