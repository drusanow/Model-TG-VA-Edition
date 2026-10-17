| ============================================================================
| =========================  LFO -> MIDI CC  =================================
|
| See docs/LFOCC.md. The LFO menu gets two more rows for the LFO showing
| (1..4) of the selected track: MIDI CC, OFF or the CC number 0..127 that the
| LFO sends to, on that track's MIDI channel - turned with the row in edit
| mode and only taken on the press that ends it, so no CC on the way is sent
| anything - and CC Base, the value it sweeps from. It goes out as well as to
| the LFO's Destination, so Destination NONE makes it a CC-only LFO.
|
| THE SETTINGS are two bytes per LFO and track (i = LFO x 6 + track):
| lcc_num (0 OFF, n + 1 for CC n) and lcc_base (0..127, 64 to start), a
| setting of the machine like the tracks' MIDI channels, not of the pattern:
| a pattern's kit record has no room left. They are kept in a file, LCC_PATH,
| read once after boot and written (a UI-task message, as the slice markers'
| are) when an LFO menu that changed one closes.
|
| THE VALUE is Base + Depth x the wave, the wave from 0 to 1: Depth -15 from
| Base 50 sweeps 50 down to 35, and +15 up to 65 (Depth in the menu's own
| steps, +-64). The wave is worked out here from the engine's phase (state
| +0, 0..1382400000 a cycle - Speed, Multiplier, Start Phase and the Trig
| Modes are all in it) and its fade (state +8, 2^30 = full), in the LFO's
| Waveform: TRI rises and falls, SIN likewise smoothly, SQR is 1 then 0, SAW
| falls, EXP falls steeply, RMP rises, RND holds a new random value each
| cycle. Each starts its cycle at 0 but SQR, SAW and EXP, which start at 1.
| LFO 3 and 4 are run while any track sends them, even with no Destination.
|
| THE SENDING is on the UI clock (led_hook, task context, as the quantised
| mutes' CC 94): at most one message a block, round the 24 LFOs, each only
| when its value has changed and LCC_GAP blocks after its last - about 120
| messages a second for one LFO, under a seventh of a MIDI cable's. The
| channel is the track's (0x40012e82 on the mute object, as the stock mute
| CC asks it); a track whose channel is off sends nothing. The send itself
| is the stock 0x40082800(channel, CC, value).
| ============================================================================

LCC_N     = 24                    | 4 LFOs x 6 tracks
LCC_GAP   = 12                    | blocks between two sends of one LFO (8 ms)
LCC_MAGIC1 = 0x4C434331           | "LCC1": the numbers only (older)
LCC_SIZE1 = 4 + LCC_N + 4
LCC_MAGIC = 0x4C434332            | "LCC2": the numbers, then the bases
LCC_SIZE  = 4 + 2*LCC_N + 4
LCC_CYC   = 1382400000            | the engine's phase, a cycle

    .align 2
| lcc_calc(a5 = the parameter block), at the end of lfo_run: lcc_val. Keeps
| d2-d7/a2-a6.
lcc_calc:
    tstl    lcc_mask
    beqw    lcc_x
    lea.l   %sp@(-28),%sp
    moveml  %d2-%d6/%a2-%a3,%sp@
    movel   CUR_KIT,%d0           | LFO 3 and 4: the playing pattern's block
    jsr     l34_blk
    movea.l %a0,%a3
    moveq   #0,%d2                | i
    moveq   #0,%d3                | the LFO
lcc_lfo:
    moveq   #0,%d4                | the track
lcc_trk:
    lea.l   lcc_num,%a0
    tstb    %a0@(0,%d2:l)
    beqw    lcc_next
    lea.l   lcc_st,%a0            | its engine state
    movea.l %a0@(0,%d3:l:4),%a2
    movel   %d4,%d0
    lsll    #5,%d0
    adda.l  %d0,%a2
    movel   %d4,%d0               | a0 = its Waveform word, a1 = its Depth
    mulu.w  #66,%d0
    lea.l   %a5@(14+2*5,%d0:l),%a0       | LFO 1: k 5 and k 8
    movea.w %a0@(6),%a1
    tstl    %d3
    beqs    lcc_got
    lea.l   %a5@(14+2*(LFO2_K0+2),%d0:l),%a0 | LFO 2: k 30 and k 32
    movea.w %a0@(4),%a1
    moveq   #1,%d0
    cmpl    %d3,%d0
    beqs    lcc_got
    movel   %d4,%d0               | LFO 3 / 4: the pattern's block, +4 and +8
    mulu.w  #10,%d0
    lea.l   %a3@(4,%d0:l),%a0
    moveq   #2,%d0
    cmpl    %d3,%d0
    beqs    lcc_g34
    lea.l   %a0@(60),%a0
lcc_g34:
    movea.w %a0@(4),%a1
lcc_got:
    moveq   #0,%d5
    moveb   %a0@,%d5              | the Waveform
    movel   %a2@,%d0              | the phase -> 0..65535
    cmpil   #LCC_CYC-1,%d0
    blss    lcc_ph
    moveq   #0,%d0
lcc_ph:
    movel   #21094,%d1
    divul   %d1,%d0
    movel   %d0,%d6               | p
    movel   #65535,%d1
    moveq   #6,%d0
    cmpl    %d5,%d0
    bcsw    lcc_tri               | out of range: TRI
    lea.l   lcc_wj,%a0
    movea.l %a0@(0,%d5:l:4),%a0
    jmp     %a0@
lcc_tri:                          | 1 - |2p - 1|
    movel   %d6,%d0
    addl    %d0,%d0
    cmpl    %d1,%d0
    blsw    lcc_u
    subl    #131071,%d0
    negl    %d0
    braw    lcc_u
lcc_sin:                          | sin^2(pi p)
    movel   %d6,%d0
    lsrl    #8,%d0
    lsrl    #1,%d0
    lea.l   mi_sin,%a0
    movel   %a0@(0,%d0:l:4),%d0
    swap    %d0
    extl    %d0
    mulsl   %d0,%d0
    lsrl    #8,%d0
    lsrl    #6,%d0
    braw    lcc_u
lcc_sqr:
    movel   %d1,%d0
    cmpil   #32768,%d6
    bcsw    lcc_u
    moveq   #0,%d0
    braw    lcc_u
lcc_saw:
    movel   %d1,%d0
    subl    %d6,%d0
    braw    lcc_u
lcc_exp:                          | (1 - p)^4
    movel   %d1,%d0
    subl    %d6,%d0
    mulul   %d0,%d0
    swap    %d0
    andil   #0xffff,%d0
    mulul   %d0,%d0
    swap    %d0
    andil   #0xffff,%d0
    braw    lcc_u
lcc_rmp:
    movel   %d6,%d0
    braw    lcc_u
lcc_rnd:                          | a new random value each time the phase wraps
    lea.l   lcc_pp,%a0
    moveq   #0,%d0
    movew   %a0@(0,%d2:l:2),%d0
    movew   %d6,%a0@(0,%d2:l:2)
    cmpl    %d0,%d6
    bccs    lcc_rh
    movel   lcc_seed,%d0
    movel   #1103515245,%d1
    mulsl   %d1,%d0
    addil   #12345,%d0
    movel   %d0,lcc_seed
    swap    %d0
    lea.l   lcc_rv,%a0
    movew   %d0,%a0@(0,%d2:l:2)
lcc_rh:
    lea.l   lcc_rv,%a0
    moveq   #0,%d0
    movew   %a0@(0,%d2:l:2),%d0
lcc_u:                            | d0 = the wave, 0..65535; times the fade
    movel   %a2@(8),%d1
    moveq   #14,%d5
    asrl    %d5,%d1               | 0..65536
    bpls    lcc_f
    moveq   #0,%d1
lcc_f:
    mulul   %d1,%d0
    clrw    %d0
    swap    %d0
    movel   %a1,%d1               | x Depth, in its steps: +-64
    subil   #16384,%d1
    mulsl   %d1,%d0
    addil   #0x800000,%d0
    asrl    #8,%d0
    asrl    #8,%d0
    asrl    #8,%d0
    lea.l   lcc_base,%a0          | from the Base
    moveq   #0,%d1
    moveb   %a0@(0,%d2:l),%d1
    addl    %d1,%d0
    bpls    lcc_lo
    moveq   #0,%d0
lcc_lo:
    moveq   #127,%d1
    cmpl    %d1,%d0
    bles    lcc_hi
    movel   %d1,%d0
lcc_hi:
    lea.l   lcc_val,%a0
    moveb   %d0,%a0@(0,%d2:l)
lcc_next:
    addql   #1,%d2
    addql   #1,%d4
    moveq   #6,%d0
    cmpl    %d4,%d0
    bnew    lcc_trk
    addql   #1,%d3
    moveq   #4,%d0
    cmpl    %d3,%d0
    bnew    lcc_lfo
    moveml  %sp@,%d2-%d6/%a2-%a3
    lea.l   %sp@(28),%sp
lcc_x:
    rts

| lcc_remask: lcc_mask = bit l for each LFO l that some track sends. Keeps
| everything but d0/d1/a0.
lcc_remask:
    movel   %d2,%sp@-
    lea.l   lcc_num,%a0
    moveq   #0,%d0                | the mask
    moveq   #0,%d1                | i
lrm_l:
    tstb    %a0@(0,%d1:l)
    beqs    lrm_n
    moveq   #0,%d2
    moveb   %a0@(LCC_N*4,%d1:l),%d2   | lcc_lof (lcc_num + 96): its LFO
    bset    %d2,%d0
lrm_n:
    addql   #1,%d1
    moveq   #LCC_N,%d2
    cmpl    %d1,%d2
    bnes    lrm_l
    movel   %d0,lcc_mask
    movel   %sp@+,%d2
    rts

| lcc_poll, on the UI clock (led_hook): the file the first time, then at most
| one CC. Keeps d2-d7/a2-a6.
lcc_poll:
    tstl    lcc_ld
    bnes    lcp_go
    moveq   #1,%d0
    movel   %d0,lcc_ld
    bsr     lcc_load
lcp_go:
    addql   #1,lcc_tick
    tstl    lcc_mask
    beqw    lcp_x
    lea.l   %sp@(-16),%sp
    moveml  %d2-%d5,%sp@
    movel   lcc_rr,%d2
    moveq   #LCC_N-1,%d3
lcp_l:
    addql   #1,%d2                | the next LFO, round
    moveq   #LCC_N,%d0
    cmpl    %d2,%d0
    bhis    lcp_i
    moveq   #0,%d2
lcp_i:
    lea.l   lcc_num,%a0
    moveq   #0,%d4
    moveb   %a0@(0,%d2:l),%d4     | its CC + 1
    beqs    lcp_n
    lea.l   lcc_val,%a0
    moveq   #0,%d5
    moveb   %a0@(0,%d2:l),%d5
    moveq   #127,%d0
    cmpl    %d5,%d0
    bcss    lcp_n                  | not worked out yet
    lea.l   lcc_sent,%a0
    moveq   #0,%d0
    moveb   %a0@(0,%d2:l),%d0
    cmpl    %d5,%d0
    beqs    lcp_n                  | sent already
    lea.l   lcc_last,%a0
    movel   lcc_tick,%d0
    subl    %a0@(0,%d2:l:4),%d0
    moveq   #LCC_GAP,%d1
    cmpl    %d1,%d0
    bcss    lcp_n                  | too soon
    movel   lcc_tick,%d0
    movel   %d0,%a0@(0,%d2:l:4)
    lea.l   lcc_sent,%a0
    moveb   %d5,%a0@(0,%d2:l)
    movel   %d2,lcc_rr
    jsr     qm_obj                | the track's channel
    lea.l   lcc_tk,%a0
    moveq   #0,%d1
    moveb   %a0@(0,%d2:l),%d1
    movel   %d1,%sp@-
    movel   %d0,%sp@-
    jsr     0x40012e82
    addql   #8,%sp
    moveq   #15,%d1
    cmpl    %d0,%d1
    bcss    lcp_out                | its channel is off
    movel   %d5,%sp@-             | the value
    subql   #1,%d4
    movel   %d4,%sp@-             | the CC
    movel   %d0,%sp@-             | the channel
    jsr     0x40082800
    lea.l   %sp@(12),%sp
    bras    lcp_out
lcp_n:
    subql   #1,%d3
    bplw    lcp_l
lcp_out:
    moveml  %sp@,%d2-%d5
    lea.l   %sp@(16),%sp
lcp_x:
    rts

| ---- the LFO menu's fifth row ----------------------------------------------
| lcc_i -> d0 = this row's i (the LFO showing, the selected track), or -1.
| Clobbers d1/a0/a1.
lcc_i:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    cmpil   #MAX_TRK,%d0
    bccs    li_no
    movel   lfo_page,%d1
    mulu.w  #6,%d1
    addl    %d1,%d0
    rts
li_no:
    moveq   #-1,%d0
    rts

lcc_lab:
    lea.l   lcc_s_lab,%a1
    jmp     gm_lab
lcc_labb:
    lea.l   lcc_s_labb,%a1
    jmp     gm_lab

| lcc_fmt(d1 = 0..127) -> a0 = it in decimal, in lcc_vbuf. Clobbers d0/d1.
lcc_fmt:
    lea.l   lcc_vbuf,%a0
    moveq   #100,%d0
    cmpl    %d0,%d1
    bcss    lf_10
    moveb   #49,%a0@+             | "1" hundred
    subl    %d0,%d1
    bras    lf_t
lf_10:
    moveq   #10,%d0
    cmpl    %d0,%d1
    bcss    lf_u
lf_t:
    moveq   #48,%d0               | the tens
lf_tl:
    cmpil   #10,%d1
    bcss    lf_te
    addql   #1,%d0
    subil   #10,%d1
    bras    lf_tl
lf_te:
    moveb   %d0,%a0@+
lf_u:
    addil   #48,%d1
    moveb   %d1,%a0@+
    clrb    %a0@
    lea.l   lcc_vbuf,%a0
    rts

| MIDI CC's value: the CC in force, or - while the row is being edited - the
| one it will become ("OFF" or the number, through gm_val)
lcc_value:
    bsr     lcc_i
    lea.l   lcc_s_off,%a0
    tstl    %d0
    bmis    lv_txt
    lea.l   lcc_num,%a0
    moveq   #0,%d1
    moveb   %a0@(0,%d0:l),%d1
    tstl    lcc_ed                | being edited: the pending one
    beqs    lv_n
    cmpl    lcc_ei,%d0
    bnes    lv_n
    movel   lcc_pv,%d1
lv_n:
    lea.l   lcc_s_off,%a0
    subql   #1,%d1
    bmis    lv_txt
    bsr     lcc_fmt
lv_txt:
    movel   %a0,lcc_name
    lea.l   lcc_zero,%a0
    lea.l   lcc_name,%a1
    jmp     gm_val
| CC Base's value
lcc_valueb:
    bsr     lcc_i
    lea.l   lcc_s_off,%a0
    tstl    %d0
    bmis    lv_txt
    lea.l   lcc_base,%a0
    moveq   #0,%d1
    moveb   %a0@(0,%d0:l),%d1
    bsr     lcc_fmt
    bras    lv_txt

| lcc_step(d1 = a value, d0 = the top, sp@(16) = the turn) -> d1 one step on,
| clamped to 0..top. Clobbers nothing else.
lcc_step:
    tstl    %sp@(16)
    bgts    ls_up
    subql   #1,%d1
    bpls    ls_out
    moveq   #0,%d1
    rts
ls_up:
    addql   #1,%d1
    cmpl    %d0,%d1
    blss    ls_out
    movel   %d0,%d1
ls_out:
    rts

| MIDI CC's turn (closure, item, delta): only the pending CC moves; the press
| that ends the edit takes it (lcc_press)
lcc_rot:
    movel   %sp@(12),%d0
    beqs    lr_out
    bsr     lcc_i
    tstl    %d0
    bmis    lr_out
    tstl    lcc_ed
    beqs    lr_out
    cmpl    lcc_ei,%d0
    bnes    lr_out
    movel   lcc_pv,%d1
    movel   #128,%d0
    bsr     lcc_step
    movel   %d1,lcc_pv
lr_draw:                          | redraw the menu, as qm_rot does
    movea.l %sp@(4),%a0           | the closure -> its data -> the menu
    movea.l %a0@,%a0
    movel   %a0@,%d0
    beqs    lr_out
    addil   #0x38,%d0
    movel   %d0,%sp@-
    jsr     0x40076082
    addql   #4,%sp
lr_out:
    rts
| CC Base's turn: at once, 0..127
lcc_rotb:
    movel   %sp@(12),%d0
    beqs    lr_out
    bsr     lcc_i
    tstl    %d0
    bmis    lr_out
    lea.l   lcc_base,%a0
    adda.l  %d0,%a0
    moveq   #0,%d1
    moveb   %a0@,%d1
    moveq   #127,%d0
    bsr     lcc_step
    moveb   %d1,%a0@
    moveq   #1,%d1
    movel   %d1,lcc_dirty
    bras    lr_draw

| MIDI CC's press (closure, item): the stock edit toggle (new_slot2_invoker),
| and before it - entering the edit, the pending CC starts from the one in
| force; leaving it, the pending one is taken.
lcc_press:
    movel   %sp@(8),%sp@-
    jsr     0x400732f8            | being edited?
    addql   #4,%sp
    movel   %d0,%sp@-
    bsr     lcc_i
    movel   %sp@+,%d1
    tstl    %d0
    bmis    lp_tog
    tstb    %d1
    beqs    lp_in
    tstl    lcc_ed                | leaving it: the pending CC, if it is ours
    beqs    lp_tog
    cmpl    lcc_ei,%d0
    bnes    lp_tog
    clrl    lcc_ed
    lea.l   lcc_num,%a0
    movel   lcc_pv,%d1
    cmpb    %a0@(0,%d0:l),%d1
    beqs    lp_tog                | unchanged
    moveb   %d1,%a0@(0,%d0:l)
    moveq   #-1,%d1               | sent again, from its first value
    lea.l   lcc_sent,%a0
    moveb   %d1,%a0@(0,%d0:l)
    lea.l   lcc_val,%a0
    moveb   %d1,%a0@(0,%d0:l)
    moveq   #1,%d1
    movel   %d1,lcc_dirty
    bsr     lcc_remask
    bras    lp_tog
lp_in:
    movel   %d0,lcc_ei            | entering it
    lea.l   lcc_num,%a0
    moveq   #0,%d1
    moveb   %a0@(0,%d0:l),%d1
    movel   %d1,lcc_pv
    moveq   #1,%d1
    movel   %d1,lcc_ed
lp_tog:
    jmp     new_slot2_invoker

| lcc_row_add(d0 = the LFO menu, just built): its two rows added at its end,
| as lm_build adds a list menu's rows. Keeps d2-d7/a2-a6.
lcc_row_add:
    linkw   %fp,#-64
    lea.l   %sp@(-20),%sp
    moveml  %d2/%a2-%a5,%sp@
    movea.l %d0,%a2
    lea.l   ALLOC,%a4
    lea.l   lcc_rows,%a5          | {label, press, value, turn} a row
    moveq   #1,%d2
lra_row:
    lea.l   %fp@(-64),%a3         | four closures: label, press, value, turn
    pea     0x1
    jsr     %a4@
    addql   #4,%sp
    movel   %d0,%a3@
    clrl    %a3@(4)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a3@(8)
    movel   %a5@+,%a3@(12)
    pea     0x4
    jsr     %a4@
    addql   #4,%sp
    movea.l %d0,%a0
    movel   %a2,%a0@
    movel   %d0,%a3@(16)
    clrl    %a3@(20)
    movel   #SLOT2_MANAGER,%d1
    movel   %d1,%a3@(24)
    movel   %a5@+,%a3@(28)
    pea     0x1
    jsr     %a4@
    addql   #4,%sp
    movel   %d0,%a3@(32)
    clrl    %a3@(36)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a3@(40)
    movel   %a5@+,%a3@(44)
    pea     0x4
    jsr     %a4@
    addql   #4,%sp
    movea.l %d0,%a0
    movel   %a2,%a0@
    movel   %d0,%a3@(48)
    clrl    %a3@(52)
    movel   #SLOT2_MANAGER,%d1
    movel   %d1,%a3@(56)
    movel   %a5@+,%a3@(60)
    pea     0x54
    jsr     %a4@
    addql   #4,%sp
    tstl    %d0
    beqs    lra_free
    movel   %d0,%sp@-             | the item, kept for the add
    pea     0x8
    pea     -1
    pea     %a3@(48)
    pea     %a3@(32)
    pea     %a3@(16)
    movel   %a3,%sp@-
    movel   %d0,%sp@-
    jsr     MENUITEM_CTOR
    lea.l   %sp@(28),%sp
    movel   %a2,%sp@-
    jsr     MENUVIEW_ADDITEM
    addql   #8,%sp
lra_free:
    pea     %a3@(48)
    jsr     FREE_CLOSURE
    addql   #4,%sp
    pea     %a3@(32)
    jsr     FREE_CLOSURE
    addql   #4,%sp
    pea     %a3@(16)
    jsr     FREE_CLOSURE
    addql   #4,%sp
    movel   %a3,%sp@-
    jsr     FREE_CLOSURE
    addql   #4,%sp
    subql   #1,%d2
    bplw    lra_row
    moveml  %sp@,%d2/%a2-%a5
    unlk    %fp
    rts

| ---- the file -------------------------------------------------------------
| lcc_load: the settings from LCC_PATH, if it is a good one (as mk_read reads
| a slice file): "LCC2", or the older "LCC1" with the numbers only. Keeps
| d2-d7/a2-a6.
lcc_load:
    linkw   %fp,#-80
    movel   %d2,%sp@-
    pea     %fp@(-16)             | fh
    pea     0x40126ce4            | "r"
    pea     lcc_path
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnew    ll_out                | no file: every one OFF
    pea     %fp@(-16)
    pea     LCC_SIZE
    pea     %fp@(-76)
    jsr     0x4007baaa
    lea.l   %sp@(12),%sp
    movel   %d0,%d2
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
    lea.l   %fp@(-76),%a0
    movel   %a0@+,%d0
    moveq   #LCC_SIZE1,%d1
    cmpl    %d1,%d2
    bnes    ll_2
    cmpil   #LCC_MAGIC1,%d0
    beqs    ll_num
    bras    ll_out
ll_2:
    moveq   #LCC_SIZE,%d1
    cmpl    %d1,%d2
    bnes    ll_out
    cmpil   #LCC_MAGIC,%d0
    bnes    ll_out
    lea.l   lcc_base,%a1          | the bases, after the numbers
    lea.l   %a0@(LCC_N),%a0
    moveq   #LCC_N-1,%d1
ll_b:
    moveq   #0,%d0
    moveb   %a0@+,%d0
    cmpil   #127,%d0
    blss    ll_bo
    moveq   #64,%d0
ll_bo:
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    ll_b
    lea.l   %fp@(-72),%a0
ll_num:
    lea.l   lcc_num,%a1
    moveq   #LCC_N-1,%d1
ll_c:
    moveq   #0,%d0
    moveb   %a0@+,%d0
    cmpil   #128,%d0
    blss    ll_ok
    moveq   #0,%d0
ll_ok:
    moveb   %d0,%a1@+
    subql   #1,%d1
    bpls    ll_c
    bsr     lcc_remask
ll_out:
    movel   %sp@+,%d2
    unlk    %fp
    rts

| lcc_save: from the LFO menu's destructor, when a number changed - posts the
| UI-task message that writes the file (as mk_post). Clobbers d0/d1/a0/a1.
lcc_save:
    tstl    lcc_dirty
    beqs    lsv_out
    clrl    lcc_dirty
    lea.l   lcc_msg,%a0
    tstl    %a0@(0x18)
    bnes    lsv_out               | queued already: that one will do
    movel   %a0,%sp@-
    pea     1
    jsr     ALLOC
    addql   #4,%sp
    movea.l %sp@+,%a0
    movel   %d0,%a0@(4)
    clrl    %a0@(8)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a0@(0x0c)
    movel   #lcc_write,%d1
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
lsv_out:
    rts

| lcc_write (closure, arg), in the UI task: LCC_PATH replaced, as mk_write
| writes a slice file (the root needs no folder made).
lcc_write:
    linkw   %fp,#-80
    lea.l   %fp@(-76),%a1
    movel   #LCC_MAGIC,%a1@+
    lea.l   lcc_num,%a0
    moveq   #LCC_N-1,%d1
lw_c:
    moveb   %a0@+,%a1@+
    subql   #1,%d1
    bpls    lw_c
    lea.l   lcc_base,%a0
    moveq   #LCC_N-1,%d1
lw_b:
    moveb   %a0@+,%a1@+
    subql   #1,%d1
    bpls    lw_b
    clrl    %a1@
    pea     lcc_path
    jsr     0x4007c18e
    addql   #4,%sp
    pea     %fp@(-16)
    pea     0x401272db            | "w"
    pea     lcc_path
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnes    lw_out
    pea     LCC_SIZE
    clrl    %sp@-
    pea     %fp@(-16)
    jsr     0x4007bb76
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    pea     LCC_SIZE
    pea     %fp@(-76)
    jsr     0x4007bb0a
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    jsr     0x4007e2f6
    addql   #4,%sp
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
lw_out:
    unlk    %fp
    rts

    .align 4
lcc_st:     .long LFO_STATE, lfo2_state, lfo34_state, lfo34_state+192
lcc_mask:   .long 0               | bit l: LFO l sends a CC on some track
lcc_tick:   .long LCC_GAP         | led_hook's blocks (from LCC_GAP: the first
                                  | send waits for nothing)
lcc_rr:     .long LCC_N-1         | the last LFO looked at
lcc_ld:     .long 0               | the file has been read
lcc_dirty:  .long 0               | a number changed since the file was written
lcc_zero:   .long 0
lcc_name:   .long 0               | the row's text, for gm_val
lcc_ed:     .long 0               | MIDI CC is being edited...
lcc_ei:     .long 0               | ... for this i ...
lcc_pv:     .long 0               | ... and will become this (0 OFF, CC + 1)
lcc_seed:   .long 0x2545F491      | RND's generator
lcc_wj:     .long lcc_tri, lcc_sin, lcc_sqr, lcc_saw, lcc_exp, lcc_rmp, lcc_rnd
lcc_rows:   .long lcc_lab, lcc_press, lcc_value, lcc_rot
            .long lcc_labb, new_slot2_invoker, lcc_valueb, lcc_rotb
lcc_pp:     .space 2*LCC_N        | RND: the phase last block
lcc_rv:     .space 2*LCC_N        | RND: the value held
lcc_last:   .space 4*LCC_N        | lcc_tick at each one's last send
lcc_msg:    .space 0x1c           | the UI-task message that writes the file
lcc_num:    .space LCC_N          | i = LFO x 6 + track: 0 OFF, CC + 1
lcc_val:    .rept LCC_N           | the values to send; 0xff: none yet
            .byte 0xff
            .endr
lcc_sent:   .rept LCC_N           | the values sent; 0xff: none yet
            .byte 0xff
            .endr
lcc_tk:     .byte 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5
lcc_lof:    .byte 0,0,0,0,0,0, 1,1,1,1,1,1, 2,2,2,2,2,2, 3,3,3,3,3,3
lcc_base:   .rept LCC_N           | the values the LFOs sweep from
            .byte 64
            .endr
lcc_vbuf:   .space 4
lcc_path:   .asciz "/lfo_cc.bin"
lcc_s_lab:  .asciz "MIDI CC"
lcc_s_labb: .asciz "CC Base"
lcc_s_off:  .asciz "OFF"
    .align 2
