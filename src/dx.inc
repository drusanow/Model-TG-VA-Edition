| ============================================================================
| ===================  DX: machine 12, a 6-operator FM voice  ===================
|
| See docs/DX.md. One DX7-style voice per track, after Dexed's engine (msfa,
| Raph Levien, Apache 2.0: its envelope, level and scaling rules are followed
| here, rewritten in ColdFire assembly). It plays DX7 voices in the packed
| 128-byte cartridge format (VMEM): the track's own cartridge (loaded from a
| .syx through the preset browser) or the built-in bank (dx_cart, 63 voices:
| two free cartridges from carts/, then this project's own, dx_own; all put
| together by tools/gen_dx_tables.py).
|
|   Color   BRIG  the modulators' output levels, +-12 dB: darker / brighter
|   Shape   TIME  every envelope of the voice faster (down) or slower (up)
|   Sweep   FDBK  the feedback: less down (to none), more up (to 7)
|   (each at 64 the voice exactly as it is; the ratios are always the
|   voice's own, the algorithm too unless SETTINGS + Shape picks one)
|   Contour VOIC  the voice, 1..63 of the bank (1..32 of a track's own cartridge)
|
| Built as PLAITS is: sampler_pre sends it to va_pre (dx_params: the dials
| and the gate), sampler_render calls dx_fill into the same 64-sample, 96 kHz
| window, and the same tail follows. The voice runs at 48 kHz (32 samples a
| block, each written twice), in blocks as msfa does: the envelopes, the LFO
| and the pitch envelope step once a block, the gains ramp across it.
| ============================================================================

| DX_MACH (12) and the sample-region areas (DX_CARTS, DX_TST) are set in
| model_tg.s.
DX_TS   = 512                     | bytes of state per track (DX_TST)
DX_VELW = 0x40fe4cdc              | the OS's note velocity per track, << 8
DX_GMAX = 0x10000000              | a gain's largest step a block (Q14 << 16)
DX_OP   = 192                     | ... where the six operators start, 48 each
DX_OPS  = 48
DX_BANKV = 63                     | voices in the built-in bank (dx_cart, gen_dx_tables.py)
| track state:
|   +0 the voice loaded (-1 none)      +4 the note's velocity (0..127)
|   +12/+16 feedback's last two outputs
|   +20 key down                       +24 the note (MIDI number, transposed)
|   +28 LFO phase  +32 its delay phase  +36 its sample-and-hold value
|   +40 pitch EG level (Q24 octaves)  +44 its stage  +48 its target
|   +52 has played                     +56 its own cartridge loaded (dx_cv)
|   +60 voice changed (dx_load)        +64 the voice, 128 bytes (VMEM)
|   +192 six operators, OP6 first:
|        +0 phase  +4 increment  +8 gain (Q14 << 16: 2.0 the most)
|        +12 level (1/256 oct << 16)
|        +16 target  +20 its hold (samples)  +24 stage (4 = done)  +28 rising
|        +32 four target levels (words)  +40 four rates (bytes, qrate)
|        +44 ratio (Q16), or bit 31 + a fixed increment (48 kHz)
|   +480 LFO increment  +484/+488 its delay's two increments
|   +492 transpose, Q12               +496 pitch EG rising
|   +500 blocks the gate has been held since the note (dx_fill)
|   +504 Shape's rate offset, this block (dx_roff -> dx_env)
DX_LFOI = 480
DX_TRQ  = 492
DX_PEGU = 496
DX_GCNT = 500
DX_ROFF = 504                     | Shape's rate offset this block (dx_env)

| ---- the four dials (from va_pre: d2 = track, a2 = trackData, a5 = trig) --
| As mi_params, into dx_p, plus the gate: dx_gate[t] = the render loop's
| trigger flag this block (non-zero while the note is held), and the note's
| velocity, dx_vel[t]. Clobbers d0/a0.
    .globl  dx_params
dx_params:
    movel   %d2,%d0
    lsll    #4,%d0
    lea.l   dx_p,%a0
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
    lea.l   dx_gate,%a0
    movel   %a5@,%a0@(0,%d2:l:4)
    lea.l   dx_tdp,%a0            | where this track's words are (dx_reset)
    movel   %a2,%a0@(0,%d2:l:4)
    | the algorithm (param 0x0a, SETTINGS + Shape: trackData+52): (w >> 8) x
    | 33 / 128, 0 the voice's, 1..32. Turned while SETTINGS is down, it is a
    | chord: SETTINGS' release then opens no Config Menu.
    mvsw    %a2@(52),%d0
    bpls    6f
    moveq   #0,%d0
6:  lsrl    #8,%d0
    mulu.w  #33,%d0
    lsrl    #7,%d0
    lea.l   dx_algo,%a0
    cmpb    %a0@(0,%d2:l),%d0
    beqs    7f
    moveb   %d0,%a0@(0,%d2:l)
    tstl    set_held
    beqs    7f
    moveq   #1,%d0
    movel   %d0,mod_used
7:
    | the note's velocity: the audio side's per-track word (velocity << 8),
    | written from the note-on event (0x40059136) before this block renders;
    | dx_fill takes it at the note
    lea.l   DX_VELW,%a0
    mvsw    %a0@(0,%d2:l:2),%d0
    bpls    5f
    moveq   #0,%d0
5:  lsrl    #8,%d0
    lea.l   dx_vel,%a0
    moveb   %d0,%a0@(0,%d2:l)
    rts

| ---- helpers ------------------------------------------------------------------

| d0 = x (u32), d1 = r (Q16, u32) -> d0 = (x r) >> 16, mod 2^32. Keeps the rest.
dx_mul16:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    movel   %d1,%d2
    clrw    %d2
    swap    %d2                   | rh
    andil   #0xffff,%d1           | rl
    movel   %d0,%d3
    mulul   %d2,%d3               | x rh, mod 2^32
    movel   %d0,%d2
    clrw    %d2
    swap    %d2
    mulul   %d1,%d2               | (x >> 16) rl
    addl    %d2,%d3
    andil   #0xffff,%d0
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0                   | ((x & 0xffff) rl) >> 16
    addl    %d3,%d0
    movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| d0 = a level 0..99 -> d0 = msfa's scaleoutlevel (0..127). Clobbers a0.
dx_solv:
    cmpil   #20,%d0
    bcss    1f
    addil   #28,%d0
    rts
1:  lea.l   dx_sol,%a0
    mvzb    %a0@(0,%d0:l),%d0
    rts

| DX_MAX r, m: \r held to at most \m (signed)
    .macro  DX_MAX r, m
    cmpil   #\m,\r
    bles    .Ldm\@
    movel   #\m,\r
.Ldm\@:
    .endm

| a0 = an operator's 17 bytes, d0 = the note -> d0 = its keyboard level
| scaling (msfa's ScaleLevel), signed. Clobbers d1, a1.
dx_lscale:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    mvzb    %a0@(8),%d1           | break point
    DX_MAX  %d1, 99
    subl    %d1,%d0
    subil   #17,%d0               | offset
    mvzb    %a0@(11),%d3
    tstl    %d0
    bmis    3f
    addql   #1,%d0                | right: (offset + 1) / 3, depth, curve
    mvzb    %a0@(10),%d2
    lsrl    #2,%d3
    bras    4f
3:  negl    %d0                   | left: -(offset - 1) / 3
    addql   #1,%d0
    mvzb    %a0@(9),%d2
4:  andil   #3,%d3                | the curve
    DX_MAX  %d2, 99
    movel   %d0,%d1
    mulu.w  #21846,%d1            | / 3
    clrw    %d1
    swap    %d1                   | the group
    cmpil   #3,%d3
    beqs    5f
    tstl    %d3
    bnes    6f
5:  mulu.w  %d2,%d1               | linear: (group depth 329) >> 12
    mulu.w  #329,%d1
    moveq   #12,%d0
    lsrl    %d0,%d1
    bras    7f
6:  cmpil   #32,%d1               | exponential: (exps[group] depth 329) >> 15
    blss    8f
    moveq   #32,%d1
8:  lea.l   dx_exps,%a1
    mvzb    %a1@(0,%d1:l),%d1
    mulu.w  %d2,%d1
    mulu.w  #329,%d1
    moveq   #15,%d0
    lsrl    %d0,%d1
7:  movel   %d1,%d0
    cmpil   #2,%d3                | curves 0 and 1 go down
    bccs    9f
    negl    %d0
9:  movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| ---- the voice -> this note's operators (msfa's Dx7Note::init) ---------------
| a5 = the track's state (voice at +64, note at +24). Keeps a2-a6 and d3-d7.
dx_setup:
    lea.l   %sp@(-24),%sp
    moveml  %d2-%d6/%a2,%sp@
    lea.l   %a5@(64),%a0          | OP6's bytes
    lea.l   %a5@(DX_OP),%a2
    moveq   #5,%d6
ds_op:
    | the output level: (scaleoutlevel(OL) + level scaling) << 5
    movel   %a0,%sp@-
    mvzb    %a0@(14),%d0
    DX_MAX  %d0, 99
    bsr     dx_solv
    movel   %d0,%d4
    movea.l %sp@,%a0
    movel   %a5@(24),%d0
    bsr     dx_lscale
    movea.l %sp@+,%a0
    addl    %d0,%d4
    bpls    1f
    moveq   #0,%d4
1:  DX_MAX  %d4, 127
    lsll    #5,%d4                | OUTL
    | + the velocity, by this operator's sensitivity (msfa's ScaleVelocity):
    | ((KVS (veld[vel / 2] - 239) + 7) >> 3) << 4, at least 0 in all
    movel   %a5@(4),%d0           | the note's velocity
    lsrl    #1,%d0
    lea.l   dx_veld,%a1
    mvzb    %a1@(0,%d0:l),%d0
    subil   #239,%d0
    mvzb    %a0@(13),%d1
    lsrl    #2,%d1
    andil   #7,%d1
    mulsl   %d1,%d0
    addql   #7,%d0
    asrl    #3,%d0
    lsll    #4,%d0
    addl    %d0,%d4
    bpls    20f
    moveq   #0,%d4
20:
    | the four target levels: ((scaleoutlevel(L) >> 1) << 6) + OUTL - 4256
    moveq   #0,%d5
2:  mvzb    %a0@(4,%d5:l),%d0
    DX_MAX  %d0, 99
    movel   %a0,%sp@-
    bsr     dx_solv
    movea.l %sp@+,%a0
    lsrl    #1,%d0
    lsll    #6,%d0
    addl    %d4,%d0
    subil   #4256,%d0
    cmpil   #16,%d0
    bges    3f
    moveq   #16,%d0
3:  movel   %d5,%d1
    addl    %d1,%d1
    movew   %d0,%a2@(32,%d1:l)
    addql   #1,%d5
    moveq   #4,%d0
    cmpl    %d5,%d0
    bnes    2b
    | the rates: (R 41) >> 6 + rate scaling ((RS x) >> 3, x = note / 3 - 7)
    movel   %a5@(24),%d0
    mulu.w  #21846,%d0
    clrw    %d0
    swap    %d0
    subql   #7,%d0
    bpls    4f
    moveq   #0,%d0
4:  DX_MAX  %d0, 31
    mvzb    %a0@(12),%d1
    andil   #7,%d1
    mulu.w  %d1,%d0
    lsrl    #3,%d0
    movel   %d0,%d4               | the rate scaling
    moveq   #0,%d5
5:  mvzb    %a0@(0,%d5:l),%d0
    DX_MAX  %d0, 99
    mulu.w  #41,%d0
    lsrl    #6,%d0
    addl    %d4,%d0
    DX_MAX  %d0, 63
    moveb   %d0,%a2@(40,%d5:l)
    addql   #1,%d5
    moveq   #4,%d0
    cmpl    %d5,%d0
    bnes    5b
    | the frequency
    mvzb    %a0@(15),%d1
    mvzb    %a0@(16),%d2          | fine
    DX_MAX  %d2, 99
    mvzb    %a0@(12),%d3
    lsrl    #3,%d3
    DX_MAX  %d3, 14               | detune
    btst    #0,%d1
    bnes    7f
    lsrl    #1,%d1                | ratio: coarse (0 = 0.5) x (1 + fine / 100)
    andil   #31,%d1
    beqs    6f
    swap    %d1
    clrw    %d1
    bras    8f
6:  movel   #0x8000,%d1
8:  addil   #100,%d2
    mulul   %d2,%d1
    moveq   #100,%d2
    divul   %d2,%d1
    movel   %a5@(24),%d0          | detune (msfa's): x (1 + (D - 7) dx_dtn[note])
    lea.l   dx_dtn,%a1
    mvzw    %a1@(0,%d0:l:2),%d0
    subql   #7,%d3
    mulsl   %d3,%d0
    addil   #65536,%d0
    movel   %d0,%d2
    movel   %d1,%d0
    movel   %d2,%d1
    bsr     dx_mul16
    movel   %d0,%a2@(44)
    bras    9f
7:  lsrl    #1,%d1                | fixed (msfa's): log2 Hz, Q24, is
    andil   #3,%d1                | (4458616 (100 (coarse & 3) + fine)) >> 3
    mulu.w  #100,%d1              | + 13457 (D - 7) when D > 7
    addl    %d2,%d1
    movel   #4458616,%d0
    mulul   %d0,%d1
    lsrl    #3,%d1
    subql   #7,%d3
    bles    10f
    mulu.w  #13457,%d3
    addl    %d3,%d1
10: addil   #275972674,%d1        | + log2(2^32 / 48000): the increment's log2
    movel   %d1,%d3
    moveq   #24,%d0
    lsrl    %d0,%d3               | its octave (16..29)
    andil   #0xffffff,%d1
    movel   %d1,%d2
    clrw    %d2
    swap    %d2                   | 2^fraction from dx_x2, interpolated
    andil   #0xffff,%d1
    lea.l   dx_x2,%a1
    lea.l   %a1@(0,%d2:l:4),%a1
    movel   %a1@,%d0
    movel   %a1@(4),%d2
    subl    %d0,%d2
    mulul   %d1,%d2
    clrw    %d2
    swap    %d2
    addl    %d2,%d0
    subil   #16,%d3
    lsll    %d3,%d0
    bset    #31,%d0
    movel   %d0,%a2@(44)
9:  lea.l   %a0@(17),%a0
    lea.l   %a2@(DX_OPS),%a2
    subql   #1,%d6
    bplw    ds_op
    | the LFO: msfa's rate and delay, in this block's units (16794)
    mvzb    %a5@(64+112),%d0      | speed: msfa's measured rates (dx_lfor)
    DX_MAX  %d0, 99
    lea.l   dx_lfor,%a1
    movel   %a1@(0,%d0:l:4),%d0
    movel   %d0,%a5@(DX_LFOI)
    mvzb    %a5@(64+113),%d0      | delay
    DX_MAX  %d0, 99
    moveq   #99,%d1
    subl    %d0,%d1
    cmpil   #99,%d1
    bnes    14f
    moveq   #-1,%d0
    movel   %d0,%a5@(DX_LFOI+4)
    movel   %d0,%a5@(DX_LFOI+8)
    bras    15f
14: movel   %d1,%d0               | (16 + (a & 15)) << (1 + (a >> 4))
    andil   #15,%d0
    addil   #16,%d0
    lsrl    #4,%d1
    addql   #1,%d1
    lsll    %d1,%d0
    movel   %d0,%d1
    movel   #16794,%d2
    mulul   %d2,%d0
    movel   %d0,%a5@(DX_LFOI+4)
    andil   #0xff80,%d1
    cmpil   #0x80,%d1
    bccs    16f
    movel   #0x80,%d1
16: mulul   %d2,%d1
    movel   %d1,%a5@(DX_LFOI+8)
15: mvzb    %a5@(64+117),%d0      | transpose, 24 = none
    DX_MAX  %d0, 48
    subil   #24,%d0
    lsll    #8,%d0
    bsr     mi_exp2
    lsrl    #2,%d0
    movel   %d0,%a5@(DX_TRQ)
    moveml  %sp@,%d2-%d6/%a2
    lea.l   %sp@(24),%sp
    rts

| ---- envelopes ----------------------------------------------------------------
| a2 = an operator, d1 = its new stage (msfa's Env::advance). Clobbers d0/d1.
| A stage whose target is the level already (or an attack to level 0) holds
| first, as the DX7 does: msfa's ACCURATE_ENVELOPE "static" count, from
| dx_stat by the stage's rate + rate scaling (a twentieth for the attack),
| in samples, at +20.
dx_adv:
    movel   %d1,%a2@(24)
    clrl    %a2@(20)
    cmpil   #4,%d1
    bccw    dxa_out
    lea.l   %sp@(-16),%sp
    moveml  %d2-%d3/%a0-%a1,%sp@
    movel   %d1,%d2               | the stage
    addl    %d1,%d1
    mvzw    %a2@(32,%d1:l),%d0
    swap    %d0
    clrw    %d0
    movel   %d0,%a2@(16)
    moveq   #0,%d3                | static: 1 equal, 2 an attack to 0
    moveq   #0,%d1
    cmpl    %a2@(12),%d0
    bnes    1f
    moveq   #1,%d3
    bras    2f
1:  bles    2f
    moveq   #1,%d1
2:  movel   %d1,%a2@(28)
    movel   %a2,%d0               | the operator's VMEM bytes: a5 + 64 +
    subl    %a5,%d0               | 17 x (its index, (a2 - a5 - 192) / 48)
    subil   #DX_OP,%d0
    mulu.w  #1366,%d0
    clrw    %d0
    swap    %d0
    mulu.w  #17,%d0
    lea.l   %a5@(64,%d0:l),%a0
    tstl    %d2
    bnes    3f
    tstb    %a0@(4)               | L1 = 0
    bnes    3f
    moveq   #2,%d3
3:  tstl    %d3
    beqs    8f
    movel   %a5@(24),%d0          | the rate + its scaling, at most 99
    mulu.w  #21846,%d0
    clrw    %d0
    swap    %d0
    subql   #7,%d0
    bpls    4f
    moveq   #0,%d0
4:  DX_MAX  %d0, 31
    mvzb    %a0@(12),%d1
    andil   #7,%d1
    mulu.w  %d1,%d0
    lsrl    #3,%d0
    mvzb    %a0@(0,%d2:l),%d1
    addl    %d1,%d0
    DX_MAX  %d0, 99
    lea.l   dx_stat,%a1
    movel   %a1@(0,%d0:l:4),%d1
    cmpil   #2,%d3
    bnes    5f
    cmpil   #77,%d0
    bccs    5f
    moveq   #20,%d0
    divul   %d0,%d1
5:  movel   %d1,%a2@(20)
8:  moveml  %sp@,%d2-%d3/%a0-%a1
    lea.l   %sp@(16),%sp
dxa_out:
    rts

| a5 = the track: a new note (msfa's keydown(true) / Dx7Note::init).
dx_noteon:
    lea.l   %sp@(-16),%sp
    moveml  %d2-%d4/%a2,%sp@
    | the note: the largest n whose lower half-semitone edge is <= d3
    moveq   #0,%d2
    moveq   #127,%d4
1:  cmpl    %d4,%d2
    bges    3f
    movel   %d2,%d0
    addl    %d4,%d0
    addql   #1,%d0
    lsrl    #1,%d0                | mid
    movel   %d0,%sp@-
    lsll    #8,%d0
    subil   #128,%d0
    bsr     mi_n2inc
    movel   %sp@+,%d1
    cmpl    %d3,%d0
    bhis    2f
    movel   %d1,%d2
    bras    1b
2:  movel   %d1,%d4
    subql   #1,%d4
    bras    1b
3:  mvzb    %a5@(64+117),%d0      | the voice's transpose, for the scaling
    DX_MAX  %d0, 48
    addl    %d0,%d2
    subil   #24,%d2
    bpls    4f
    moveq   #0,%d2
4:  DX_MAX  %d2, 127
    movel   %d2,%a5@(24)
    moveq   #1,%d0
    movel   %d0,%a5@(20)          | down
    movel   %d0,%a5@(52)          | has played
    bsr     dx_setup
    mvzb    %a5@(64+111),%d4      | osc key sync
    lea.l   %a5@(DX_OP),%a2
    moveq   #5,%d2
5:  clrl    %a2@(12)              | from level 0, as msfa's Env::init (the
    moveq   #0,%d1                | gain still ramps down from the last note's)
    bsr     dx_adv
    btst    #3,%d4                | key sync: from phase 0 - unless it still
    beqs    6f                    | sounds (over 1/64 of full), where the jump
    movel   %a2@(8),%d0           | would click
    cmpil   #0x01000000,%d0
    bccs    6f
    clrl    %a2@
6:  lea.l   %a2@(DX_OPS),%a2
    subql   #1,%d2
    bpls    5b
    mvzb    %a5@(64+116),%d0      | LFO sync
    btst    #0,%d0
    beqs    7f
    movel   #0x7fffffff,%d0
    movel   %d0,%a5@(28)
7:  clrl    %a5@(32)
    mvzb    %a5@(64+109),%d0      | the pitch EG starts at L4
    bsr     dx_peg_lv
    movel   %d0,%a5@(40)
    moveq   #0,%d1
    bsr     dx_peg_adv
    moveml  %sp@,%d2-%d4/%a2
    lea.l   %sp@(16),%sp
    rts

| a5 = the track: the note released (keydown(false): every envelope to R4)
dx_noteoff:
    movel   %d2,%sp@-
    movel   %a2,%sp@-
    clrl    %a5@(20)
    lea.l   %a5@(DX_OP),%a2
    moveq   #5,%d2
1:  moveq   #3,%d1
    bsr     dx_adv
    lea.l   %a2@(DX_OPS),%a2
    subql   #1,%d2
    bpls    1b
    moveq   #3,%d1
    bsr     dx_peg_adv
    movea.l %sp@+,%a2
    movel   %sp@+,%d2
    rts

| the pitch EG (msfa's PitchEnv): a level 0..99 is pitchenv_tab[L] << 19,
| Q24 octaves (+-4); d0 = L -> d0. Clobbers a0.
dx_peg_lv:
    DX_MAX  %d0, 99
    lea.l   dx_pegl,%a0
    moveb   %a0@(0,%d0:l),%d0
    extbl   %d0
    swap    %d0
    clrw    %d0
    lsll    #3,%d0
    rts
| d1 = the pitch EG's new stage (PitchEnv::advance). Clobbers d0/d1, a0.
dx_peg_adv:
    movel   %d1,%a5@(44)
    cmpil   #4,%d1
    bccs    1f
    lea.l   %a5@(64+106),%a0
    mvzb    %a0@(0,%d1:l),%d0
    bsr     dx_peg_lv
    movel   %d0,%a5@(48)
    moveq   #0,%d1
    cmpl    %a5@(40),%d0
    bles    2f
    moveq   #1,%d1
2:  movel   %d1,%a5@(DX_PEGU)
1:  rts

| d0 = Shape (Q15) -> d0 = its rate offset, for every stage of every
| operator: 0 at 64, up to +20 steps (32x faster) at 0, down to -40 (1000x
| slower) at 127 - 4 steps double a time; dx_env never takes a rate below 8
| (a 10 s attack) unless the voice's own is. Keeps the rest.
dx_roff:
    asrl    #8,%d0
    subil   #64,%d0
    negl    %d0                   | 64 - k
    bmis    1f
    mulsw   #20,%d0               | (64 - k) x 20 / 64
    asrl    #6,%d0
    rts
1:  mulsw   #162,%d0              | (64 - k) x 40 / 63
    asrl    #8,%d0
    rts

| ---- dx_fill(track, stepQ16, machine): one block into sampler_buf+32 -------
| Frame: 64 bytes of locals, d2-d7/a2-a6 at sp@(64), the arguments at
| sp@(112), sp@(116), sp@(120).
|   locals: 0 track, 4 this block's increment (48 kHz, transposed, pitch
|   modulated), 8 the modulators' ratio factor (Q16), 12 the rate offset
|   (SHAPE), 16 amplitude modulation (Q24), 20 the operator, 24 its flags'
|   row (dx_alg), 28 the feedback shift (0 = none), 32 the trig, 36 the gate,
|   40 the feedback shift, as a count
DXL = 64
    .globl  dx_fill
dx_fill:
    lea.l   %sp@(-108),%sp
    moveml  %d2-%d7/%a2-%a6,%sp@(64)
    movel   %sp@(112),%d6         | track
    movel   %d6,%sp@(0)
    movel   %sp@(116),%d1         | step, Q16 (1.0 = note 60)
    bnes    1f
    movel   #0x10000,%d1
1:  movel   %d1,%d3               | the note's increment at 96 kHz, as mi_fill
    movel   #357,%d0
    mulul   %d0,%d3
    moveq   #52,%d0
    mulul   %d0,%d1
    lsrl    #8,%d1
    addl    %d1,%d3
    lsrl    #1,%d3
    movel   %d6,%d0
    lsll    #4,%d0
    lea.l   dx_p,%a4
    adda.l  %d0,%a4
    movel   %d6,%d0
    moveq   #9,%d1
    lsll    %d1,%d0               | x DX_TS
    lea.l   DX_TST,%a5
    adda.l  %d0,%a5
    lea.l   dr_trig,%a0
    moveq   #0,%d7
    moveb   %a0@(0,%d6:l),%d7
    clrb    %a0@(0,%d6:l)
    movel   %d7,%sp@(32)
    lea.l   dx_gate,%a0
    movel   %a0@(0,%d6:l:4),%d0
    movel   %d0,%sp@(36)
    lea.l   sampler_buf+32,%a3
    | ---- the voice: Contour picks one of the built-in bank or of the
    | track's own cartridge ----
    movel   %a4@(12),%d0
    moveq   #32,%d1
    tstl    %a5@(56)
    bnes    1f
    moveq   #DX_BANKV,%d1
1:  bsr     dx_vidx
    cmpl    %a5@,%d0
    bnes    2f
    tstl    %a5@(60)
    beqs    3f
2:  movel   %d0,%a5@
    clrl    %a5@(60)
    lsll    #7,%d0
    lea.l   dx_cart,%a0           | the built-in bank...
    tstl    %a5@(56)
    beqs    4f
    movel   %d6,%d1               | ...or the track's own
    moveq   #12,%d2
    lsll    %d2,%d1
    lea.l   DX_CARTS,%a0
    adda.l  %d1,%a0
4:  adda.l  %d0,%a0
    lea.l   %a5@(64),%a1
    moveq   #31,%d1
5:  movel   %a0@+,%a1@+
    subql   #1,%d1
    bpls    5b
    tstl    %a5@(52)              | a voice while a note plays: retuned now
    beqs    3f
    bsr     dx_setup
3:  | ---- the note ----
    tstl    %sp@(32)
    beqs    6f
    lea.l   dx_vel,%a0            | its velocity (dx_params)
    moveq   #0,%d0
    moveb   %a0@(0,%d6:l),%d0
    DX_MAX  %d0, 127
    movel   %d0,%a5@(4)
    bsr     dx_noteon
    clrl    %a5@(DX_GCNT)
6:  tstl    %a5@(20)              | the note released when its gate falls -
    beqs    7f                    | if it was held: a gate seen for under
    tstl    %sp@(36)              | three blocks is a trig's pulse, not a
    beqs    9f                    | length, and the note holds on (Amp Decay
    addql   #1,%a5@(DX_GCNT)      | shapes it) until the next
    bras    7f
9:  moveq   #3,%d0
    cmpl    %a5@(DX_GCNT),%d0
    bgts    7f
    bsr     dx_noteoff
7:  tstl    %a5@(52)
    bnew    dx_play
    movea.l %a3,%a0               | nothing played yet: silence
    moveq   #63,%d0
8:  clrl    %a0@+
    subql   #1,%d0
    bpls    8b
    braw    dx_out
dx_play:
    | ---- the algorithm: the voice's, or the track's choice (dx_algo,
    | SETTINGS + Shape: 1..32, 0 the voice's) ----
    lea.l   dx_algo,%a0
    moveq   #0,%d0
    moveb   %a0@(0,%d6:l),%d0
    subql   #1,%d0
    bpls    1f
    mvzb    %a5@(64+110),%d0
1:  andil   #31,%d0
    mulu.w  #6,%d0
    lea.l   dx_alg,%a0
    adda.l  %d0,%a0
    movel   %a0,%sp@(24)
    | ---- the feedback: Sweep, the voice's at 64; below, down to 0 at 0
    | (fb (64 + s) / 64), above, up to 7 at 127 (fb + (7 - fb) s / 63) ----
    mvzb    %a5@(64+111),%d2
    andil   #7,%d2
    movel   %a4@(8),%d0
    asrl    #8,%d0
    DX_MAX  %d0, 127
    subil   #64,%d0
    beqs    3f
    bpls    2f
    addil   #64,%d0
    mulu.w  %d0,%d2
    addil   #32,%d2
    lsrl    #6,%d2
    bras    3f
2:  moveq   #7,%d1
    subl    %d2,%d1
    mulu.w  %d0,%d1
    addil   #31,%d1
    mulu.w  #1041,%d1             | / 63
    clrw    %d1
    swap    %d1
    addl    %d1,%d2
3:  movel   %d2,%sp@(28)
    moveq   #0,%d1
    tstl    %d2
    beqs    4f
    moveq   #9,%d1                | (y0 + y1) << (9 + fb)
    addl    %d2,%d1
4:  movel   %d1,%sp@(40)
    | ---- the macros, each 64 = the voice as it is ----
    | Color, Bright: the modulators' levels, (c - 64) x 8 (1/256 octave): +-16
    | output-level steps, +-12 dB
    movel   %a4@,%d0
    lsrl    #8,%d0
    subil   #64,%d0
    lsll    #3,%d0
    movel   %d0,%sp@(8)
    | Shape, Env Time: every envelope of the voice faster or slower
    movel   %a4@(4),%d0
    bsr     dx_roff
    movel   %d0,%a5@(DX_ROFF)
    | ---- the LFO ----
    bsr     dx_lfo                | -> d0 pitch (Q8 semitones), d1 amp (Q24)
    movel   %d1,%sp@(16)
    | ---- the pitch EG ----
    bsr     dx_peg                | -> d0 += its offset, Q8 semitones
    | ---- this block's increment: the note x transpose x pitch ----
    movel   %d0,%d4
    movel   %d3,%d0
    addl    %d0,%d0               | 48 kHz
    movel   %a5@(DX_TRQ),%d1
    bsr     mi_mulr
    tstl    %d4
    beqs    5f
    movel   %d0,%d5
    movel   %d4,%d0
    bsr     mi_exp2
    lsll    #2,%d0
    movel   %d0,%d1
    movel   %d5,%d0
    bsr     dx_mul16
5:  movel   %d0,%sp@(4)
    | ---- the six operators ----
    movea.l %a3,%a0               | the output, summed in the window's first half
    moveq   #31,%d0
6:  clrl    %a0@+
    subql   #1,%d0
    bpls    6b
    lea.l   %a5@(DX_OP),%a6
    clrl    %sp@(20)
dx_opl:
    movea.l %sp@(24),%a0
    movel   %sp@(20),%d0
    mvzb    %a0@(0,%d0:l),%d7     | its flags
    | the increment
    movel   %a6@(44),%d1
    bpls    1f
    bclr    #31,%d1               | fixed
    movel   %d1,%d2
    bras    3f
1:  movel   %sp@(4),%d0
    bsr     dx_mul16
    movel   %d0,%d2
3:  movel   %d2,%a6@(4)
    | the envelope, one step (its rates offset by Shape), and a modulator's
    | level by Bright
    movea.l %a6,%a2
    bsr     dx_env                | -> d0 = the level (1/256 octave)
    movel   %d7,%d1
    andil   #3,%d1
    beqs    4f
    addl    %sp@(8),%d0
    bpls    4f
    moveq   #0,%d0
4:  | amplitude modulation, msfa's: level -= level x amf[(amd x ams) >> 42]
    movel   %sp@(20),%d2          | (this operator's byte 13: its AMS)
    mulu.w  #17,%d2
    mvzb    %a5@(64+13,%d2:l),%d1
    andil   #3,%d1
    beqs    5f
    lea.l   dx_amsq,%a0
    movel   %a0@(0,%d1:l:4),%d1   | Q24
    lsrl    #8,%d1
    movel   %sp@(16),%d2          | amd, Q24
    lsrl    #8,%d2
    mulul   %d1,%d2
    moveq   #26,%d1               | (amd >> 8)(sens >> 8) >> 26 = (amd x
    lsrl    %d1,%d2               | sens) >> 24 >> 18: 0..64
    DX_MAX  %d2, 64
    lea.l   dx_amf,%a0
    mvzw    %a0@(0,%d2:l:2),%d1
    movel   %d0,%d2
    mulul   %d1,%d2
    clrw    %d2
    swap    %d2
    subl    %d2,%d0
5:  movel   #4096,%d1             | the gain: 2^((L - 3840) / 256), Q14 - an
    subl    %d0,%d1               | octave over full scale for velocity
    bgts    5f
    movel   #32767,%d0
    bras    6f
5:  movel   %d1,%d0
    lsrl    #8,%d0
    cmpil   #15,%d0
    bcss    7f
    moveq   #0,%d0
    bras    6f
7:  andil   #255,%d1
    lea.l   dx_e2,%a0
    mvzw    %a0@(0,%d1:l:2),%d1
    lsrl    %d0,%d1
    movel   %d1,%d0
6:  swap    %d0
    clrw    %d0                   | gain2, Q14 << 16
    movel   %a6@(8),%d4           | gain1
    movel   %d0,%d1               | at most a quarter of full scale a block
    subl    %d4,%d1               | either way (0 to full in ~2.7 ms): an
    cmpil   #DX_GMAX,%d1          | envelope's jump would click
    bles    7f
    movel   %d4,%d0
    addil   #DX_GMAX,%d0
    bras    8f
7:  cmpil   #-DX_GMAX,%d1
    bges    8f
    movel   %d4,%d0
    subil   #DX_GMAX,%d0
8:
    movel   %d0,%a6@(8)
    movel   %d0,%d5
    subl    %d4,%d5
    addil   #16,%d5
    asrl    #5,%d5                | its step a sample
    movel   %a6@,%d2              | phase
    movel   %a6@(4),%d3           | increment
    | where it writes and reads
    lea.l   dx_bus1,%a2
    movel   %d7,%d0
    andil   #3,%d0
    bnes    8f
    movea.l %a3,%a2               | the output
    bras    9f
8:  cmpil   #2,%d0
    bnes    9f
    lea.l   dx_bus2,%a2
9:  suba.l  %a1,%a1
    movel   %d7,%d0
    lsrl    #4,%d0
    andil   #3,%d0
    beqs    10f
    lea.l   dx_bus1,%a1
    cmpil   #2,%d0
    bnes    10f
    lea.l   dx_bus2,%a1
10: | silent (both gains under 2/32768): nothing to add, or an empty bus
    movel   %d4,%d0
    cmpil   #0x20000,%d0
    bccs    dx_go
    movel   %a6@(8),%d0
    cmpil   #0x20000,%d0
    bccs    dx_go
    movel   %d3,%d0               | the phase still turns
    lsll    #5,%d0
    addl    %d0,%d2
    btst    #2,%d7
    bnew    dx_nx
    movel   %d7,%d0
    andil   #3,%d0
    beqw    dx_nx
    moveq   #31,%d0
11: clrl    %a2@+
    subql   #1,%d0
    bpls    11b
    braw    dx_nx
dx_go:
    moveq   #31,%d6
    lea.l   dx_sin,%a0
    movel   %d7,%d0
    andil   #0xc0,%d0
    cmpil   #0xc0,%d0
    bnes    1f
    tstl    %sp@(28)
    bnew    dx_fb
1:  cmpa.l  #0,%a1
    bnew    dx_in
    | -- no input --
    btst    #2,%d7
    beqs    dx_n0
dx_n1:
    movel   %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    addl    %d0,%a2@+
    subql   #1,%d6
    bpls    dx_n1
    braw    dx_nx
dx_n0:
    movel   %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    movel   %d0,%a2@+
    subql   #1,%d6
    bpls    dx_n0
    braw    dx_nx
    | -- reading a bus: the phase + 2 cycles x (bus / 32768) --
dx_in:
    btst    #2,%d7
    beqs    dx_i0
dx_i1:
    movel   %a1@+,%d0
    moveq   #18,%d1
    lsll    %d1,%d0
    addl    %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    addl    %d0,%a2@+
    subql   #1,%d6
    bpls    dx_i1
    braw    dx_nx
dx_i0:
    movel   %a1@+,%d0
    moveq   #18,%d1
    lsll    %d1,%d0
    addl    %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    movel   %d0,%a2@+
    subql   #1,%d6
    bpls    dx_i0
    braw    dx_nx
    | -- feedback on itself (msfa's compute_fb): phase + (y0 + y1) << (9 + fb)
dx_fb:
    movea.l %a5@(12),%a1          | y0
    btst    #2,%d7                | whether it adds (branched on before d7
    bnes    1f                    | takes y1: the move sets the flags)
    movel   %a5@(16),%d7          | y1
    bras    dx_f0
1:  movel   %a5@(16),%d7
dx_f1:
    movel   %a1,%d0
    addl    %d7,%d0
    movel   %sp@(40),%d1
    lsll    %d1,%d0
    addl    %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    movea.l %d7,%a1
    movel   %d0,%d7
    addl    %d0,%a2@+
    subql   #1,%d6
    bpls    dx_f1
    bras    dx_fe
dx_f0:
    movel   %a1,%d0
    addl    %d7,%d0
    movel   %sp@(40),%d1
    lsll    %d1,%d0
    addl    %d2,%d0
    addl    %d3,%d2
    moveq   #19,%d1
    lsrl    %d1,%d0
    mvsw    %a0@(0,%d0:l:2),%d0   | dx_sin
    movel   %d4,%d1
    swap    %d1
    extl    %d1
    mulsw   %d1,%d0
    asrl    #8,%d0
    asrl    #6,%d0
    addl    %d5,%d4
    movea.l %d7,%a1
    movel   %d0,%d7
    movel   %d0,%a2@+
    subql   #1,%d6
    bpls    dx_f0
dx_fe:
    movel   %a1,%a5@(12)
    movel   %d7,%a5@(16)
dx_nx:
    movel   %d2,%a6@              | the phase, kept
    lea.l   %a6@(DX_OPS),%a6
    addql   #1,%sp@(20)
    moveq   #6,%d0
    cmpl    %sp@(20),%d0
    bnew    dx_opl
    | ---- the sum (x 1/4, Q15: a full carrier at -12 dB, so several add up
    | before they clip, as in Dexed) to 96 kHz, from the end ----
    lea.l   %a3@(128),%a1
    lea.l   %a3@(256),%a0
    moveq   #31,%d2
1:  movel   %a1@-,%d0
    asrl    #2,%d0
    MI_CLAMP %d0, 32767
    lsll    #8,%d0
    lsll    #7,%d0
    movel   %d0,%a0@-
    movel   %d0,%a0@-
    subql   #1,%d2
    bpls    1b
dx_out:
    moveml  %sp@(64),%d2-%d7/%a2-%a6
    lea.l   %sp@(108),%sp
    rts

| a2 = an operator: one block of its envelope (msfa's Env::getsample), its
| rate offset by Shape (DX_ROFF) -> d0 = the level, in 1/256 octave. Keeps
| d2-d7. Clobbers d1, a0.
dx_env:
    movel   %d2,%sp@-
    movel   %a2@(20),%d0          | holding (dx_adv): 32 samples a block
    beqs    12f
    subil   #32,%d0
    movel   %d0,%a2@(20)
    bgtw    dxe_out
    clrl    %a2@(20)
    movel   %a2@(24),%d1
    addql   #1,%d1
    bsr     dx_adv
    tstl    %a2@(20)
    bnew    dxe_out
12:
    movel   %a2@(24),%d1          | the stage
    cmpil   #3,%d1
    bcss    1f
    bhiw    dxe_out                    | done
    tstl    %a5@(20)
    bnew    dxe_out                    | stage 3 waits for the key to lift
1:  mvzb    %a2@(40,%d1:l),%d0    | its rate, offset by Shape - not below 8
    movel   %d0,%d2               | unless the voice's own is
    addl    %a5@(DX_ROFF),%d0
    cmpil   #8,%d0
    bges    2f
    moveq   #8,%d0
    cmpl    %d2,%d0
    bles    2f
    movel   %d2,%d0
2:  DX_MAX  %d0, 63
    movel   %d0,%d2               | inc = ((4 + (q & 3)) << (q >> 2)) x 118
    andil   #3,%d2
    addql   #4,%d2
    lsrl    #2,%d0
    lsll    %d0,%d2
    moveq   #118,%d0
    mulul   %d0,%d2
    movel   %a2@(12),%d0          | the level
    tstl    %a2@(28)
    beqs    5f
    cmpil   #1716<<16,%d0         | rising: from 1716 at least, faster the
    bges    3f                    | lower it is
    movel   #1716<<16,%d0
3:  movel   #17<<24,%d1
    subl    %d0,%d1
    asrl    #8,%d1
    asrl    #8,%d1
    asrl    #8,%d1
    mulsl   %d1,%d2
    addl    %d2,%d0
    cmpl    %a2@(16),%d0
    blts    8f
    bras    6f
5:  subl    %d2,%d0               | falling
    cmpl    %a2@(16),%d0
    bgts    8f
6:  movel   %a2@(16),%d0          | there: the next stage
    movel   %d0,%a2@(12)
    movel   %a2@(24),%d1
    addql   #1,%d1
    bsr     dx_adv
    bras    dxe_out
8:  movel   %d0,%a2@(12)
dxe_out:
    movel   %a2@(12),%d0
    swap    %d0
    extl    %d0
    movel   %sp@+,%d2
    rts

| the LFO, one block (msfa's Lfo) -> d0 = pitch modulation (Q8 semitones,
| signed), d1 = amplitude modulation (Q24). a5 = the track. Keeps d2-d7.
dx_lfo:
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d4,%sp@
    movel   %a5@(28),%d2          | the phase
    movel   %a5@(DX_LFOI),%d3
    addl    %d3,%d2
    movel   %d2,%a5@(28)
    mvzb    %a5@(64+116),%d0
    lsrl    #1,%d0
    andil   #7,%d0                | the wave
    beqs    l_tri
    subql   #1,%d0
    beqs    l_sdn
    subql   #1,%d0
    beqs    l_sup
    subql   #1,%d0
    beqs    l_sqr
    subql   #1,%d0
    beqs    l_sin
    cmpl    %d3,%d2               | sample and hold: a new value each turn
    bccs    1f
    movel   %a5@(36),%d0
    mulu.w  #179,%d0
    addil   #17,%d0
    andil   #255,%d0
    movel   %d0,%a5@(36)
1:  movel   %a5@(36),%d0
    eoril   #0x80,%d0
    addql   #1,%d0
    swap    %d0
    clrw    %d0
    bras    l_got
l_tri:
    movel   %d2,%d0
    lsrl    #7,%d0
    tstl    %d2
    bpls    1f
    notl    %d0
1:  andil   #0xffffff,%d0
    bras    l_got
l_sdn:
    movel   %d2,%d0
    notl    %d0
    eoril   #0x80000000,%d0
    lsrl    #8,%d0
    bras    l_got
l_sup:
    movel   %d2,%d0
    eoril   #0x80000000,%d0
    lsrl    #8,%d0
    bras    l_got
l_sqr:
    movel   %d2,%d0
    notl    %d0
    lsrl    #7,%d0
    andil   #0x1000000,%d0
    bras    l_got
l_sin:
    MI_SIN  %d2
    lsll    #8,%d0
    addil   #0x800000,%d0
l_got:
    movel   %d0,%d4               | the LFO, 0..2^24
    | its delay (msfa's getdelay) -> d3, 0..2^24
    movel   %a5@(32),%d0
    movel   %a5@(DX_LFOI+4),%d1
    tstl    %d0
    bpls    2f
    movel   %a5@(DX_LFOI+8),%d1
2:  addl    %d1,%d0
    bccs    3f
    movel   #0x1000000,%d3        | over: full
    bras    5f
3:  movel   %d0,%a5@(32)
    moveq   #0,%d3
    tstl    %d0
    bpls    5f
    movel   %d0,%d3
    lsrl    #7,%d3
    andil   #0xffffff,%d3
5:  | pitch: ((PMD 165 >> 6) delay) >> 16 x (PMS (lfo - 2^23)) >> 16, >> 7
    mvzb    %a5@(64+114),%d0
    DX_MAX  %d0, 99
    mulu.w  #165,%d0
    lsrl    #6,%d0
    mulul   %d3,%d0
    clrw    %d0
    swap    %d0
    mvzb    %a5@(64+116),%d1
    lsrl    #4,%d1
    andil   #7,%d1
    lea.l   dx_pms,%a0
    mvzb    %a0@(0,%d1:l),%d1
    movel   %d4,%d2
    subil   #0x800000,%d2
    mulsl   %d1,%d2
    asrl    #8,%d2
    asrl    #8,%d2
    mulsl   %d2,%d0
    asrl    #7,%d0                | Q24 octaves
    movel   %d0,%d1
    addl    %d0,%d0
    addl    %d1,%d0
    moveq   #14,%d1
    asrl    %d1,%d0               | x 3072 / 2^24: Q8 semitones
    | amplitude: ((AMD 165 >> 6) delay >> 8) (2^24 - lfo) >> 24, as msfa
    negl    %d4
    addil   #0x1000000,%d4
    mvzb    %a5@(64+115),%d1
    DX_MAX  %d1, 99
    mulu.w  #165,%d1
    lsrl    #6,%d1
    lsrl    #8,%d3
    mulul   %d3,%d1               | <= 2^24
    lsrl    #8,%d1
    lsrl    #8,%d4                | (x the LFO, >> 24)
    mulul   %d4,%d1
    lsrl    #8,%d1
    moveml  %sp@,%d2-%d4
    lea.l   %sp@(12),%sp
    rts

| the pitch EG, one block (PitchEnv::getsample, its step pitchenv_rate[R] x
| 525: 2^24 x 32 / (21.3 x 48000)): d0 += its level, as Q8 semitones
| (Q24 octaves x 3 >> 14). a5 = the track. Keeps d2-d7. Clobbers d1, a0.
dx_peg:
    movel   %d2,%sp@-
    movel   %d0,%sp@-
    movel   %a5@(44),%d1
    cmpil   #3,%d1
    bcss    1f
    bhis    4f
    tstl    %a5@(20)
    bnes    4f                    | stage 3 waits for the key to lift
1:  lea.l   %a5@(64+102),%a0
    mvzb    %a0@(0,%d1:l),%d0
    DX_MAX  %d0, 99
    lea.l   dx_pegr,%a0
    mvzb    %a0@(0,%d0:l),%d2
    mulu.w  #525,%d2
    movel   %a5@(40),%d0
    movel   %a5@(48),%d1
    tstl    %a5@(DX_PEGU)
    beqs    2f
    addl    %d2,%d0               | up to it
    cmpl    %d1,%d0
    blts    3f
    bras    5f
2:  subl    %d2,%d0               | down to it
    cmpl    %d1,%d0
    bgts    3f
5:  movel   %d1,%a5@(40)
    movel   %a5@(44),%d1
    addql   #1,%d1
    bsr     dx_peg_adv
    bras    4f
3:  movel   %d0,%a5@(40)
4:  movel   %a5@(40),%d0
    movel   %d0,%d1
    addl    %d0,%d0
    addl    %d1,%d0
    asrl    #7,%d0
    asrl    #7,%d0
    addl    %sp@+,%d0
    movel   %sp@+,%d2
    rts

| ---- cartridges: a file from the drive onto a track -------------------------
| d0 = track, d1 = a file's hash (the browser's sel_hash, or a pattern's
| token) -> d0 = 0 loaded, 1 not found, 2 not a DX7 file, 3 a read error.
| The file is found in the inode table and its first 16 KiB read as the
| sample loader does (load_at: TBL_BUF, readBlocks 0x4008b24a), into DX_FBUF.
| Then the first of these, scanning it, becomes the track's cartridge:
|   F0 43 0n 09 20 00 + 4096 bytes   a 32-voice bulk dump (VMEM)
|   F0 43 0n 00 01 1B + 155 bytes    one voice (VCED): voice 1, INIT after it
|   exactly 4096 bytes               a bare VMEM bank
| UI context only (the browser, dx_probe). Keeps d2-d7/a2-a6.
    .globl  dx_cload
dx_cload:
    lea.l   %sp@(-32),%sp
    moveml  %d2-%d7/%a2-%a3,%sp@
    movel   %d0,%d6               | track
    movel   %d1,%d2               | hash
    beqw    dcl_nf
    movea.l #TBL_BUF,%a3
    movel   #TABLE_BLOCK,%d7
dcl_page:
    movel   #TABLE_END,%d0
    cmpl    %d7,%d0
    blsw    dcl_nf
    movel   %a3,%sp@-
    pea     65536
    movel   %d7,%sp@-
    jsr     0x4008b24a
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnew    dcl_rd
    movea.l %a3,%a2
    movel   #512,%d3
dcl_rec:
    movel   %a2@,%d0
    cmpil   #0x00020001,%d0
    bnes    1f
    movel   %a2@(12),%d0
    cmpl    %d2,%d0
    beqs    dcl_found
1:  lea.l   %a2@(128),%a2
    subql   #1,%d3
    bnes    dcl_rec
    addil   #128,%d7
    bras    dcl_page
dcl_found:
    movel   %a2@(4),%d3           | its size, at most 16 KiB of it
    beqw    dcl_no
    cmpil   #DX_FBSZ,%d3
    blss    2f
    movel   #DX_FBSZ,%d3
2:  movel   %a2@(40),%d7          | its first chunk -> block
    lsll    #5,%d7
    addil   #DATA_BASE,%d7
    movel   %d3,%d0
    addil   #511,%d0
    andil   #0xfffffe00,%d0
    pea     DX_FBUF
    movel   %d0,%sp@-
    movel   %d7,%sp@-
    jsr     0x4008b24a
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnew    dcl_rd
    | a bulk dump?
    lea.l   DX_FBUF,%a2
    movel   %d3,%d4
    subil   #4104,%d4             | the last start it could have
    bmis    dcl_one
3:  bsr     dx_is_hdr
    cmpil   #0x0920,%d0
    bnes    4f
    tstb    %a2@(5)
    beqs    dcl_bulk
4:  addql   #1,%a2
    subql   #1,%d4
    bpls    3b
dcl_one:
    lea.l   DX_FBUF,%a2           | one voice?
    movel   %d3,%d4
    subil   #163,%d4
    bmis    dcl_raw
5:  bsr     dx_is_hdr
    cmpil   #0x0001,%d0
    bnes    6f
    moveb   %a2@(5),%d0
    cmpib   #0x1b,%d0
    beqs    dcl_vced
6:  addql   #1,%a2
    subql   #1,%d4
    bpls    5b
dcl_raw:
    cmpil   #4096,%d3             | a bare bank
    bnew    dcl_no
    lea.l   DX_FBUF-6,%a2
dcl_bulk:
    bsr     dx_cart_of            | -> a1 = the track's cartridge, unloaded
    lea.l   %a2@(6),%a0
    movel   #4095,%d0
7:  moveb   %a0@+,%d1
    andil   #0x7f,%d1
    moveb   %d1,%a1@+
    subql   #1,%d0
    bpls    7b
    braw    dcl_ok
dcl_vced:
    bsr     dx_cart_of
    lea.l   %a2@(6),%a0
    bsr     dx_vced               | voice 1
    lea.l   %a1@(128),%a1         | INIT VOICE for the rest
    moveq   #30,%d1
8:  lea.l   dx_own,%a0            | (INIT VOICE)
    moveq   #31,%d0
9:  movel   %a0@+,%a1@+
    subql   #1,%d0
    bpls    9b
    subql   #1,%d1
    bpls    8b
dcl_ok:
    movel   %d6,%d0               | in use: the track reloads its voice
    moveq   #9,%d1
    lsll    %d1,%d0
    lea.l   DX_TST,%a0
    adda.l  %d0,%a0
    moveq   #1,%d0
    movel   %d0,%a0@(56)
    movel   %d0,%a0@(60)
    moveq   #0,%d0
    bras    dcl_out
dcl_nf:
    moveq   #1,%d0
    bras    dcl_out
dcl_no:
    moveq   #2,%d0
    bras    dcl_out
dcl_rd:
    moveq   #3,%d0
dcl_out:
    moveml  %sp@,%d2-%d7/%a2-%a3
    lea.l   %sp@(32),%sp
    rts

| a2 -> d0 = (byte 3 << 8) | byte 4 if a2 holds F0 43 0n (n a channel),
| else -1. Clobbers d1.
dx_is_hdr:
    moveq   #-1,%d0
    moveb   %a2@,%d1
    cmpib   #0xf0,%d1
    bnes    1f
    moveb   %a2@(1),%d1
    cmpib   #0x43,%d1
    bnes    1f
    moveb   %a2@(2),%d1
    andil   #0xf0,%d1
    bnes    1f
    moveq   #0,%d0
    moveb   %a2@(3),%d0
    lsll    #8,%d0
    moveb   %a2@(4),%d1
    andil   #0xff,%d1
    orl     %d1,%d0
1:  rts

| d6 = track -> a1 = its cartridge (DX_CARTS + 4 KiB x track), marked not
| loaded while it is written (the audio reads the built-in one meanwhile).
| Clobbers d0/a0.
dx_cart_of:
    movel   %d6,%d0
    moveq   #9,%d1
    lsll    %d1,%d0
    lea.l   DX_TST,%a0
    clrl    %a0@(56,%d0:l)
    movel   %d6,%d0
    moveq   #12,%d1
    lsll    %d1,%d0
    lea.l   DX_CARTS,%a1
    adda.l  %d0,%a1
    rts

| a0 = one voice unpacked (VCED, 155 bytes) -> a1 = it packed (VMEM, 128).
| Keeps a0/a1. Clobbers d0/d1.
dx_vced:
    movel   %a2,%sp@-
    movel   %d2,%sp@-
    movel   %a0,%sp@-
    movel   %a1,%sp@-
    moveq   #5,%d2
dxv_op:
    moveq   #10,%d1               | rates, levels, break point, depths
2:  moveb   %a0@+,%d0
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    2b
    moveb   %a0@(1),%d0           | right curve << 2 | left curve
    andil   #3,%d0
    lsll    #2,%d0
    moveb   %a0@,%d1
    andil   #3,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(9),%d0           | detune << 3 | rate scaling
    andil   #15,%d0
    lsll    #3,%d0
    moveb   %a0@(2),%d1
    andil   #7,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(4),%d0           | key velocity sens << 2 | amp mod sens
    andil   #7,%d0
    lsll    #2,%d0
    moveb   %a0@(3),%d1
    andil   #3,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(5),%d0           | output level
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(7),%d0           | coarse << 1 | mode
    andil   #31,%d0
    addl    %d0,%d0
    moveb   %a0@(6),%d1
    andil   #1,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(8),%d0           | fine
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    lea.l   %a0@(10),%a0          | the next operator (21 bytes in all)
    subql   #1,%d2
    bplw    dxv_op
    moveq   #7,%d1                | the pitch EG
3:  moveb   %a0@+,%d0
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    3b
    moveb   %a0@+,%d0             | algorithm
    andil   #31,%d0
    moveb   %d0,%a1@+
    moveb   %a0@(1),%d0           | osc key sync << 3 | feedback
    andil   #1,%d0
    lsll    #3,%d0
    moveb   %a0@,%d1
    andil   #7,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    addql   #2,%a0
    moveq   #3,%d1                | LFO speed, delay, PMD, AMD
4:  moveb   %a0@+,%d0
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    4b
    moveb   %a0@(2),%d0           | PMS << 4 | wave << 1 | sync
    andil   #7,%d0
    lsll    #4,%d0
    moveb   %a0@(1),%d1
    andil   #7,%d1
    addl    %d1,%d1
    orl     %d1,%d0
    moveb   %a0@,%d1
    andil   #1,%d1
    orl     %d1,%d0
    moveb   %d0,%a1@+
    addql   #3,%a0
    moveq   #10,%d1               | transpose and the name
5:  moveb   %a0@+,%d0
    andil   #0x7f,%d0
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    5b
    movea.l %sp@+,%a1
    movea.l %sp@+,%a0
    movel   %sp@+,%d2
    movea.l %sp@+,%a2
    rts

| ---- a pattern's cartridge: its token in the sound's name -------------------
| As the Sampler's "SMP" token (name_store): the 16 bytes at sound+4 are the
| preset name, saved with the pattern. A DX track carries "DX7" + the
| cartridge file's hash in 8 hex digits; none means the built-in cartridge.
| d0 = track, d1 = hash. Plain stores. Keeps d2-d7/a2-a6.
    .globl  dx_nstore
dx_nstore:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    movel   %d1,%d3
    lea.l   dx_hash,%a0           | what the track holds now, so dx_probe
    movel   %d1,%a0@(0,%d0:l:4)   | does not read it again
    jsr     sound_obj
    tstl    %d0
    beqs    3f
    movea.l %d0,%a0
    addql   #4,%a0
    moveb   #68,%a0@+             | DX7
    moveb   #88,%a0@+
    moveb   #55,%a0@+
    moveq   #28,%d2
1:  movel   %d3,%d0
    lsrl    %d2,%d0
    andil   #15,%d0
    addil   #48,%d0
    cmpil   #58,%d0
    bcss    2f
    addql   #7,%d0
2:  moveb   %d0,%a0@+
    subql   #4,%d2
    bpls    1b
    clrb    %a0@
3:  movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| a0 = a sound -> d0 = the hash its "DX7" token names, or 0. Clobbers d1, a0.
dx_tok:
    movel   %d2,%sp@-
    moveq   #0,%d0
    moveb   %a0@(4),%d1
    cmpib   #68,%d1
    bnes    9f
    moveb   %a0@(5),%d1
    cmpib   #88,%d1
    bnes    9f
    moveb   %a0@(6),%d1
    cmpib   #55,%d1
    bnes    9f
    lea.l   %a0@(7),%a0
    moveq   #7,%d2
1:  moveq   #0,%d1
    moveb   %a0@+,%d1
    subil   #48,%d1
    bmis    8f
    cmpil   #10,%d1
    bcss    2f
    subql   #7,%d1
    cmpil   #10,%d1
    bcss    8f
    cmpil   #16,%d1
    bccs    8f
2:  lsll    #4,%d0
    orl     %d1,%d0
    subql   #1,%d2
    bpls    1b
    bras    9f
8:  moveq   #0,%d0
9:  movel   %sp@+,%d2
    rts

| From led_hook, every 16th block (UI context): each DX track whose token
| names another cartridge than the one it holds - a pattern changed, a
| project loaded - reads it (dx_cload), or goes back to the built-in one.
| A file that cannot be read is not tried again until the token changes.
    .globl  dx_probe
dx_probe:
    addql   #1,dx_ptick
    moveq   #15,%d0
    andl    dx_ptick,%d0
    bnew    dpr_out
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d3/%d6,%sp@
    moveq   #0,%d6
dpr_l:
    movel   %d6,%d0
    jsr     sound_obj
    tstl    %d0
    beqs    dpr_n
    movea.l %d0,%a0
    moveq   #0,%d1
    moveb   %a0@(38),%d1
    cmpil   #DX_MACH,%d1
    bnes    dpr_nd
    movel   %a0,%sp@-
    bsr     dx_vwatch             | a new voice picked: the preset as it is
    movea.l %sp@+,%a0
    bsr     dx_tok
    movel   %d0,%d2
    lea.l   dx_hash,%a0
    cmpl    %a0@(0,%d6:l:4),%d2
    beqs    dpr_n
    movel   %d2,%a0@(0,%d6:l:4)
    tstl    %d2
    beqs    dpr_bi
    movel   %d6,%d0
    movel   %d2,%d1
    bsr     dx_cload
    tstl    %d0
    beqs    dpr_n
dpr_bi:
    movel   %d6,%d0               | the built-in cartridge
    moveq   #9,%d1
    lsll    %d1,%d0
    lea.l   DX_TST,%a0
    adda.l  %d0,%a0
    clrl    %a0@(56)
    moveq   #1,%d0
    movel   %d0,%a0@(60)
    bras    dpr_n
dpr_nd:
    lea.l   dx_snv,%a0            | not DX: no snapshot to compare with
    clrb    %a0@(0,%d6:l)
dpr_n:
    addql   #1,%d6
    moveq   #6,%d0
    cmpl    %d6,%d0
    bnes    dpr_l
    moveml  %sp@,%d2-%d3/%d6
    lea.l   %sp@(12),%sp
dpr_out:
    rts

| a0 = DX track d6's sound: a voice picked by hand starts as its preset. The
| sound's 66 parameter bytes (sound+20.., k = 0..32) are compared with the
| last look's (dx_snap): when Contour (k=14) is the only one that changed,
| the knob was turned - p-locks, slides and LFOs never write the sound, and
| a pattern loaded changes more than Contour - so dx_reset puts the macros
| in the middle and the algorithm on VCE. Then the snapshot is taken again.
| Clobbers d0/d1, a0/a1.
dx_vwatch:
    movel   %d2,%sp@-
    lea.l   dx_snap,%a1
    movel   %d6,%d0
    mulu.w  #66,%d0
    adda.l  %d0,%a1
    lea.l   dx_snv,%a0
    tstb    %a0@(0,%d6:l)
    movea.l %sp@(8),%a0           | (the sound, as the caller pushed it)
    beqs    4f
    movew   %a0@(48),%d0          | Contour changed...
    cmpw    %a1@(28),%d0
    beqs    4f
    moveq   #65,%d2               | ...and nothing else?
1:  cmpil   #28,%d2
    beqs    2f
    cmpil   #29,%d2
    beqs    2f
    moveb   %a0@(20,%d2:l),%d0
    cmpb    %a1@(0,%d2:l),%d0
    bnes    4f
2:  subql   #1,%d2
    bpls    1b
    movel   %d6,%d0
    bsr     dx_reset
    movea.l %sp@(8),%a0
    lea.l   dx_snap,%a1
    movel   %d6,%d0
    mulu.w  #66,%d0
    adda.l  %d0,%a1
4:  moveq   #65,%d2               | the snapshot
5:  moveb   %a0@(20,%d2:l),%d0
    moveb   %d0,%a1@(0,%d2:l)
    subql   #1,%d2
    bpls    5b
    lea.l   dx_snv,%a0
    moveq   #1,%d0
    moveb   %d0,%a0@(0,%d6:l)
    movel   %sp@+,%d2
    rts

| d0 = a DX track: its macros in the middle (Color, Shape, Sweep: 64) and its
| algorithm on VCE (the voice's own), in its sound and in the words the
| audio side reads (dx_tdp; the next trig copies the sound there anyway).
| UI task. Clobbers d0/d1, a0/a1.
    .globl  dx_reset
dx_reset:
    movel   %d0,%sp@-
    jsr     sound_obj
    movel   %sp@+,%d1
    tstl    %d0
    beqs    1f
    movea.l %d0,%a0
    movel   #64<<8,%d0
    movew   %d0,%a0@(42)          | k = 11..13
    movew   %d0,%a0@(44)
    movew   %d0,%a0@(46)
    clrl    %a0@(72)              | k = 26: VCE
1:  cmpil   #6,%d1
    bccs    2f
    lea.l   dx_tdp,%a1
    movel   %a1@(0,%d1:l:4),%d0
    beqs    2f
    movea.l %d0,%a1
    movel   #64<<8,%d0
    movew   %d0,%a1@(22)
    movew   %d0,%a1@(24)
    movew   %d0,%a1@(26)
    clrw    %a1@(52)
2:  rts

| ---- cartridges arriving as SysEx: saved on the drive -----------------------
| The MIDI task hands every complete SysEx message, from USB and from MIDI IN,
| to one handler through the pointer at 0x40112950 (the SysEx entry of the
| dispatch table 0x400803e2 reads; stock: 0x400812f6, which answers the
| universal and Elektron messages and drops the rest). build.py points it
| here: (ptr to F0, length with F0 and F7, port), entered by jmp with the
| return address on top. A DX7 bulk dump (4104 bytes) or one-voice dump (163)
| is copied out - the stock frees or reuses its buffer at once - and a UI
| message writes it as /DX7/<its first voice's name> <4 hex digits>.syx;
| anything else goes on to the stock handler, untouched. One at a time: a
| dump that arrives while the last one is still being written is dropped.
    .globl  dx_sysex
dx_sysex:
    movea.l %sp@(4),%a0
    moveb   %a0@(1),%d0
    cmpib   #0x43,%d0             | Yamaha's
    bnew    dxs_stock
    moveb   %a0@(2),%d0
    andil   #0xf0,%d0
    bnew    dxs_stock
    movel   %sp@(8),%d1
    moveb   %a0@(3),%d0
    cmpib   #0x09,%d0
    bnes    1f
    moveb   %a0@(4),%d0
    cmpib   #0x20,%d0
    bnew    dxs_stock
    tstb    %a0@(5)
    bnew    dxs_stock
    cmpil   #4104,%d1
    beqs    dxs_take
    braw    dxs_stock
1:  tstb    %d0                   | 00 01 1B: one voice
    bnew    dxs_stock
    moveb   %a0@(4),%d0
    cmpib   #0x01,%d0
    bnew    dxs_stock
    moveb   %a0@(5),%d0
    cmpib   #0x1b,%d0
    bnew    dxs_stock
    cmpil   #163,%d1
    bnew    dxs_stock
dxs_take:
    tstl    dx_rxlen
    bnes    dxs_out               | still writing the last one
    lea.l   DX_RXBUF,%a1
    movel   %d1,%d0
    subql   #1,%d0
3:  moveb   %a0@+,%a1@+
    subql   #1,%d0
    bpls    3b
    movel   %d1,dx_rxlen
    lea.l   dx_rxmsg,%a0          | the UI task writes it (as lcc_post posts)
    tstl    %a0@(0x18)
    bnes    dxs_out
    movel   %a0,%sp@-
    pea     1
    jsr     ALLOC
    addql   #4,%sp
    movea.l %sp@+,%a0
    movel   %d0,%a0@(4)
    clrl    %a0@(8)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a0@(0x0c)
    movel   #dx_rx_write,%d1
    movel   %d1,%a0@(0x10)
    clrl    %a0@(0x14)
    moveq   #1,%d0
    movel   %d0,%a0@(0x18)
    moveq   #6,%d0                | type 6: run the callback
    moveb   %d0,%a0@
    movel   %a0,%sp@-
    pea     MQ_MAIN
    jsr     0x40001fba
    addql   #8,%sp
dxs_out:
    rts
dxs_stock:
    jmp     0x400812f6

| (closure, arg), in the UI task: the dump in DX_RXBUF to its file
    .globl  dx_rx_write
dx_rx_write:
    linkw   %fp,#-40
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d3/%a2,%sp@
    clrl    dx_rxmsg+0x18
    bsr     dx_rx_path            | -> a0 = the path
    movel   %a0,%sp@-
    jsr     0x4007c18e
    movel   %sp@,%d0
    movel   %d0,%sp@-
    jsr     0x40095284            | its folder
    addql   #4,%sp
    tstl    %d0
    beqs    1f
    pea     %fp@(-17)
    movel   %d0,%sp@-
    pea     %fp@(-24)
    jsr     0x400f980c            | as a String
    lea.l   %sp@(12),%sp
    pea     %fp@(-24)
    jsr     0x4007c262            | made, if it is not there
    addql   #4,%sp
    pea     %fp@(-24)
    jsr     0x400f7d5c
    addql   #4,%sp
1:  movel   %sp@+,%d3             | the path
    pea     %fp@(-16)
    pea     0x401272db            | "w"
    movel   %d3,%sp@-
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnes    2f
    movel   dx_rxlen,%d2
    movel   %d2,%sp@-
    clrl    %sp@-
    pea     %fp@(-16)
    jsr     0x4007bb76
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    movel   %d2,%sp@-
    pea     DX_RXBUF
    jsr     0x4007bb0a
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    jsr     0x4007e2f6
    addql   #4,%sp
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
2:  clrl    dx_rxlen              | the next one may come
    moveml  %sp@,%d2-%d3/%a2
    unlk    %fp
    rts

| -> a0 = dx_rxpath: "/DX7/" + the first voice's name (letters, digits,
| - and . kept, others _, trailing spaces off; "CART" if none) + " " + 4 hex
| digits of the data's FNV-1a hash + ".syx". Clobbers d0/d1/a1.
dx_rx_path:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    lea.l   DX_RXBUF+6,%a0        | the data
    movel   #4096,%d2
    lea.l   %a0@(118),%a1         | a bulk dump's first voice's name
    movel   dx_rxlen,%d0
    cmpil   #4104,%d0
    beqs    1f
    movel   #155,%d2
    lea.l   %a0@(145),%a1         | one voice's
1:  movel   #0x811c9dc5,%d0       | FNV-1a over the data
    movel   #0x01000193,%d3
    subql   #1,%d2
2:  moveq   #0,%d1
    moveb   %a0@+,%d1
    eorl    %d1,%d0
    mulul   %d3,%d0
    subql   #1,%d2
    bpls    2b
    movel   %d0,%d3               | the hash, kept
    lea.l   dx_rxpath,%a0
    moveb   #47,%a0@+             | /DX7/
    moveb   #68,%a0@+
    moveb   #88,%a0@+
    moveb   #55,%a0@+
    moveb   #47,%a0@+
    movel   %a0,%d2               | where the name starts
    moveq   #9,%d1
3:  moveb   %a1@+,%d0
    cmpib   #48,%d0
    bcss    5f
    cmpib   #58,%d0
    bcss    4f                    | 0..9
    cmpib   #65,%d0
    bcss    5f
    cmpib   #91,%d0
    bcss    4f                    | A..Z
    cmpib   #97,%d0
    bcss    5f
    cmpib   #123,%d0
    bcss    4f                    | a..z
5:  cmpib   #32,%d0
    beqs    4f
    cmpib   #45,%d0
    beqs    4f
    cmpib   #46,%d0
    beqs    4f
    moveq   #95,%d0               | _
4:  moveb   %d0,%a0@+
    subql   #1,%d1
    bpls    3b
6:  cmpl    %a0,%d2               | trailing spaces off
    beqs    7f
    moveb   %a0@(-1),%d0
    cmpib   #32,%d0
    bnes    8f
    subql   #1,%a0
    bras    6b
7:  moveb   #67,%a0@+             | CART
    moveb   #65,%a0@+
    moveb   #82,%a0@+
    moveb   #84,%a0@+
8:  moveb   #32,%a0@+
    moveq   #12,%d2               | 4 hex digits
9:  movel   %d3,%d0
    lsrl    %d2,%d0
    andil   #15,%d0
    addil   #48,%d0
    cmpil   #58,%d0
    bcss    10f
    addql   #7,%d0
10: moveb   %d0,%a0@+
    subql   #4,%d2
    bpls    9b
    moveb   #46,%a0@+             | .syx
    moveb   #115,%a0@+
    moveb   #121,%a0@+
    moveb   #120,%a0@+
    clrb    %a0@
    lea.l   dx_rxpath,%a0
    movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts

| ---- the labels and the values the page shows ------------------------------
| Rows as mi_swap's (apply_names set 7).
    .align 2
    .globl  dx_swap
dx_swap:
    .long 2576, s_dxa, s_dxc, s_dxas, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, s_dxe, s_dxc, s_dxes, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, s_dxf, s_dxc, s_dxfs, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, s_dxv, s_dxc, s_dxvs, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0

| d0 (0..99) -> two-place decimal at a1 (no leading zero), a1 past it.
| Clobbers d1.
dx_num:
    movel   %d0,%sp@-
    moveq   #10,%d1
    cmpl    %d1,%d0
    bcss    1f
    divul   %d1,%d0               | the tens
    movel   %d0,%d1
    addil   #48,%d1
    moveb   %d1,%a1@+
    mulu.w  #10,%d0
    movel   %sp@,%d1
    subl    %d0,%d1
    movel   %d1,%d0
1:  addil   #48,%d0
    moveb   %d0,%a1@+
    addql   #4,%sp
    rts

| (record + 0x14, value, buf) -> sprintf(buf, "%s", dx_sbuf), the string
| at dx_sbuf already made
dx_sfmt:
    movel   %sp@(12),%d1
    movel   %d1,%sp@(4)           | buf
    movel   #0x40124b58,%d1       | "%s"
    movel   %d1,%sp@(8)
    movel   #dx_sbuf,%d1
    movel   %d1,%sp@(12)
    jmp     0x40000e6e            | sprintf

| Color, Shape, Sweep: the macros, 64 the voice as it is: "0", "+1" .. "+63",
| "-1" .. "-64"
    .globl  dx_offmt
dx_offmt:
    lea.l   dx_sbuf,%a1
    movel   %sp@(8),%d0
    bpls    1f
    moveq   #0,%d0
1:  lsrl    #8,%d0
    subil   #64,%d0
    beqs    2f
    bpls    3f
    moveb   #45,%a1@+             | -
    negl    %d0
    bras    2f
3:  moveb   #43,%a1@+             | +
2:  bsr     dx_num
    clrb    %a1@
    braw    dx_sfmt

| d0 = Contour's value, d1 = the voices to pick from (the bank's, or 32) ->
| d0 = the voice, (value >> 8) x d1 / 128. Clobbers d1.
dx_vidx:
    tstl    %d0
    bpls    1f
    moveq   #0,%d0
1:  lsrl    #8,%d0
    DX_MAX  %d0, 127
    mulu.w  %d1,%d0
    lsrl    #7,%d0
    rts

| the selected track's cartridge -> a0 = it (the built-in bank, or the
| track's own), d1 = its voices. Clobbers d0, a1.
dx_selc:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    lea.l   dx_cart,%a0
    moveq   #DX_BANKV,%d1
    cmpil   #6,%d0
    bccs    1f
    lsll    #8,%d0
    addl    %d0,%d0               | x DX_TS (512)
    lea.l   DX_TST,%a1
    tstl    %a1@(56,%d0:l)
    beqs    1f
    lsll    #3,%d0                | x 4096 = 512 x 8
    lea.l   DX_CARTS,%a0
    adda.l  %d0,%a0
    moveq   #32,%d1
1:  rts

| DX's Algorithm (param 0x0a): "VCE" (the voice's own), then "A1" .. "A32"
    .globl  dx_algfmt
dx_algfmt:
    lea.l   dx_sbuf,%a1
    movel   %sp@(8),%d0
    bpls    1f
    moveq   #0,%d0
1:  lsrl    #8,%d0
    DX_MAX  %d0, 127
    mulu.w  #33,%d0
    lsrl    #7,%d0
    bnes    2f
    moveb   #86,%a1@+             | VCE
    moveb   #67,%a1@+
    moveb   #69,%a1@+
    bras    3f
2:  moveb   #65,%a1@+             | A
    bsr     dx_num
3:  clrb    %a1@
    braw    dx_sfmt

| ---- dx_algpop: the Algorithm popup, full screen ---------------------------
| SETTINGS + Shape on a DX track turns Algorithm (param 0x0a): instead of the
| stock value popup (a dial), the whole screen shows the algorithm's diagram,
| as the DX7's chart draws it, and under it its number ("ALGORITHM 24"; on
| VCE, the voice's own: "VCE: ALGORITHM 5", boxed when p-locked). Reached from
| wf_popup, the parameter view's popup (slot 0x90), with its arguments
| (this, ctx, id, value, locked, 1); like the waveform there, it clears the
| screen, draws, and pre-inverts the left 64 x 64 box, which the view's render
| inverts once more. The diagrams are rectangles (dx_algo_gfx.inc, written
| by tools/gen_dx_algo_gfx.py), rows from the top: mirrored here, as the
| screen counts y from the bottom.
    .globl  dx_algpop
dx_algpop:
    linkw   %fp,#0
    lea.l   %sp@(-40),%sp
    moveml  %d2-%d7/%a2-%a5,%sp@
    movea.l %fp@(12),%a4          | ctx
    clrl    %sp@-                 | the screen cleared
    pea     63
    pea     127
    clrl    %sp@-
    clrl    %sp@-
    movel   %a4,%sp@-
    jsr     0x40070dea
    lea.l   %sp@(24),%sp
    movel   %fp@(20),%d0          | the value: 0 VCE, 1..32 (dx_algfmt's sum)
    bpls    1f
    moveq   #0,%d0
1:  lsrl    #8,%d0
    DX_MAX  %d0, 127
    mulu.w  #33,%d0
    lsrl    #7,%d0
    movel   %d0,%d5               | 0: VCE
    movel   %d0,%d6               | the algorithm shown, 1..32
    bnes    2f
    jsr     0x400cf866            | VCE: the selected track's voice's own
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    moveq   #1,%d6
    cmpil   #6,%d0
    bccs    2f
    lsll    #8,%d0
    addl    %d0,%d0               | x DX_TS (512)
    lea.l   DX_TST+64+110,%a0
    mvzb    %a0@(0,%d0:l),%d6     | the voice (packed): its algorithm, 0..31
    moveq   #31,%d0
    andl    %d0,%d6
    addql   #1,%d6
2:  lea.l   dx_agfx_ix,%a0
    movel   %d6,%d0
    subql   #1,%d0
    addl    %d0,%d0
    mvzw    %a0@(0,%d0:l),%d0
    lea.l   dx_agfx,%a2
    adda.l  %d0,%a2
    moveq   #0,%d7
    moveb   %a2@+,%d7             | how many rectangles
3:  subql   #1,%d7
    bmis    5f
    moveq   #0,%d2
    moveb   %a2@+,%d2             | x0 (bit 7: cleared)
    moveq   #0,%d3
    moveb   %a2@+,%d3             | y0, from the top
    moveq   #0,%d4
    moveb   %a2@+,%d4             | x1
    moveq   #0,%d1
    moveb   %a2@+,%d1             | y1, from the top
    moveq   #1,%d0
    bclr    #7,%d2
    beqs    4f
    moveq   #0,%d0
4:  movel   %d0,%sp@-             | (ctx, x0, 63 - y1, x1, 63 - y0, set/clear)
    moveq   #63,%d0
    subl    %d3,%d0
    movel   %d0,%sp@-
    movel   %d4,%sp@-
    moveq   #63,%d0
    subl    %d1,%d0
    movel   %d0,%sp@-
    movel   %d2,%sp@-
    movel   %a4,%sp@-
    jsr     0x40070dea
    lea.l   %sp@(24),%sp
    bras    3b
5:  movel   %d6,%sp@-             | the number, centred under it (rows 54..62)
    pea     dx_agf_n
    tstl    %d5
    bnes    6f
    addql   #4,%sp
    pea     dx_agf_v
6:  moveq   #0x12,%d0             | centred, cleared behind
    tstb    %fp@(27)              | p-locked: boxed, as the stock popup marks it
    beqs    7f
    moveq   #0x1a,%d0
7:  movel   %d0,%sp@-
    pea     1
    pea     64
    pea     0x40ea14cc
    movel   %a4,%sp@-
    jsr     0x40071a04
    lea.l   %sp@(28),%sp
    pea     -1                    | pre-invert the left box, which the render
    pea     63                    | inverts once more
    pea     63
    clrl    %sp@-
    clrl    %sp@-
    movel   %a4,%sp@-
    jsr     0x40070dea
    lea.l   %sp@(24),%sp
    moveml  %sp@,%d2-%d7/%a2-%a5
    unlk    %fp
    rts
dx_agf_n: .asciz "ALGORITHM %d"
dx_agf_v: .asciz "VCE: ALGORITHM %d"
    .align 2

| Contour: the voice's number, 1..63 of the bank or 1..32 of a track's own
| cartridge (the name is the knob screen's, dx_draw)
dx_vfmt:
    movel   %sp@(8),%sp@-
    bsr     dx_selc
    movel   %sp@+,%d0
    bsr     dx_vidx
    lea.l   dx_sbuf,%a1
    addql   #1,%d0
    bsr     dx_num
    clrb    %a1@
    braw    dx_sfmt

| value (Contour's) -> a1 = dx_sbuf holding the selected track's voice name,
| from its cartridge, trailing spaces off; d0 = its length. Clobbers d1/a0.
dx_vname:
    movel   %d0,%sp@-
    bsr     dx_selc
    movel   %sp@+,%d0
    bsr     dx_vidx
    lsll    #7,%d0
    lea.l   %a0@(118,%d0:l),%a0   | its name, ten characters
    lea.l   dx_sbuf,%a1
    moveq   #9,%d1
3:  moveb   %a0@+,%d0
    cmpib   #32,%d0
    bcss    4f                    | (a control byte ends it)
    cmpib   #127,%d0
    bccs    4f
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    3b
4:  cmpa.l  #dx_sbuf,%a1          | trailing spaces off
    beqs    5f
    moveb   %a1@(-1),%d0
    cmpib   #32,%d0
    bnes    5f
    subql   #1,%a1
    bras    4b
5:  clrb    %a1@
    movel   %a1,%d0
    subil   #dx_sbuf,%d0
    lea.l   dx_sbuf,%a1
    rts

| ---- dx_draw: the knob screen while Contour picks the voice ----------------
| Invoked as mi_draw is (functor, value, ctx, 0x60, y, flag), after the page
| has drawn the name on the left and its five pips: the pips cleared, the
| voice's name drawn centred on them (x 96) in the big font if it fits in 56
| pixels, else in the small one (left at x 66 if even that is too wide).
    .globl  dx_draw
dx_draw:
    linkw   %fp,#-60
    moveml  %d2-%d7/%a2-%a5,%sp@
    movel   %fp@(16),%d2          | ctx
    movel   %fp@(24),%d3          | y
    clrl    %sp@-                 | the stock pips cleared
    pea     28
    pea     122
    pea     22
    pea     66
    movel   %d2,%sp@-
    jsr     0x40070efc
    lea.l   %sp@(24),%sp
    movel   %fp@(12),%d0
    bsr     dx_vname              | a1 = the name, d0 = its length
    movel   %d0,%d6
    beqw    dxd_out
    lea.l   dx_nbuf,%a4           | (kept: dx_sbuf is reused by formatters)
    movea.l %a4,%a0
1:  moveb   %a1@+,%a0@+
    bnes    1b
    movel   #FD_BIG,%d7
    bsr     dxd_width
    cmpil   #56,%d0
    bles    2f
    movel   #FD_SMALL,%d7
    bsr     dxd_width
2:  movel   %d0,%d5
    lsrl    #1,%d5
    moveq   #96,%d0
    subl    %d5,%d0
    cmpil   #66,%d0
    bges    3f
    moveq   #66,%d0
3:  movel   %d0,%d5               | x
    movel   %d7,%sp@-
    jsr     0x400720f8
    addql   #4,%sp
    movel   %a4,%sp@-             | (ctx, font, x, y, 0, 0, template, "%s", name)
    pea     0x40124b58
    movel   %a4,%sp@-
    clrl    %sp@-
    clrl    %sp@-
    movel   %d3,%sp@-
    movel   %d5,%sp@-
    movel   %d0,%sp@-
    movel   %d2,%sp@-
    jsr     0x40071c10
    lea.l   %sp@(36),%sp
dxd_out:
    moveml  %fp@(-60),%d2-%d7/%a2-%a5
    unlk    %fp
    rts
| d7 = a font, a4 = the name, d6 = its length -> d0 = its width (as mi_draw
| measures). Uses the frame's String at fp-4.
dxd_width:
    movel   %fp,%d4
    subql   #4,%d4
    pea     %fp@(-5)
    movel   %a4,%sp@-
    movel   %d4,%sp@-
    jsr     0x400f980c
    movel   %d7,%sp@-
    jsr     0x400720f8
    movel   %d4,%sp@-
    movel   %d6,%sp@-
    movel   %d0,%sp@-
    jsr     0x40072102
    lea.l   %sp@(28),%sp
    movel   %d0,%sp@-
    movel   %d4,%sp@-
    jsr     0x400f7d5c
    addql   #4,%sp
    movel   %sp@+,%d0
    rts

s_dxc:  .asciz "DX"
s_dxa:  .asciz "Bright"
s_dxas: .asciz "BRIG"
s_dxe:  .asciz "Env Time"
s_dxes: .asciz "TIME"
s_dxf:  .asciz "Feedback"
s_dxfs: .asciz "FDBK"
s_dxv:  .asciz "Voice"
    .globl  s_dxalg, s_dxalgs
s_dxalg:  .asciz "Algorithm"
s_dxalgs: .asciz "ALGO"
s_dxvs: .asciz "VOIC"
    .align 2
dx_swf0: .long 0x400456c8         | Sweep's stock formatter, while ours is in
dx_sbuf: .space 16
dx_nbuf: .space 16

    .align 2
dx_amsq:   .long 0, 4342338, 7171437, 16777216 | AM sensitivity (msfa's ams_tab), Q24
    .align 2
    .include "dx_tables.inc"
    .include "dx_algo_gfx.inc"
    .align 2
dx_p:      .space 6*16            | the four dials, Q15
dx_vel:    .byte 100,100,100,100,100,100 | each track's note velocity (dx_params)
dx_algo:   .byte 0,0,0,0,0,0     | each track's algorithm, 1..32 (0: the voice's)
dx_snv:    .byte 0,0,0,0,0,0     | dx_snap holds the track's sound (dx_vwatch)
dx_snap:   .space 6*66           | each DX track's 66 parameter bytes, last look
    .align 2
dx_tdp:    .space 6*4            | each track's trackData (dx_params)
    .align 2
dx_hash:   .space 6*4             | the cartridge each track holds (its file's
                                  | hash; 0 the built-in one)
dx_ptick:  .long 0                | dx_probe's block count
dx_rxlen:  .long 0                | a dump waiting in DX_RXBUF (its length)
dx_rxmsg:  .space 0x1c            | the UI-task message that writes it
dx_rxpath: .space 40              | its file's path
