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
| THE SETTINGS are two bytes per LFO and track (i = LFO x 6 + track), per
| pattern: 48 bytes at LCC_OFF of each kit's LFO 3/4 block (lfo2.inc) - the
| numbers (0 OFF, n + 1 for CC n), then the bases (0..127, stored ^ 64 so a
| cleared block reads 64). The playing pattern's are used (lcc_cur). Saved
| with the kit: its record has no room for 48 bytes, so a hash of them goes
| at +700 and the bytes in a small file named by it (below).
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
LCC_OFF   = 124                   | in a kit's LFO 3/4 block (lfo2.inc): the
                                  | numbers (0 OFF, CC + 1), then the bases
                                  | (stored ^ 64, so a cleared block is 64)
LCC_SZ    = 2*LCC_N               | ... 48 bytes
LCC_REC   = 700                   | in the kit's storage record: "C", the
                                  | settings' hash (4 bytes), a check byte
LCC_TAG   = 0x43
LCC_CYC   = 1382400000            | the engine's phase, a cycle

    .align 2
| lcc_calc(a5 = the parameter block), at the end of lfo_run: lcc_val. Keeps
| d2-d7/a2-a6.
lcc_calc:
    bsr     lcc_cur               | the playing pattern's settings: its mask
    bsr     lcc_remask
    tstl    lcc_mask
    beqw    lcc_x
    lea.l   %sp@(-28),%sp
    moveml  %d2-%d6/%a2-%a3,%sp@
    movel   CUR_KIT,%d0           | LFO 3 and 4, and the settings: the playing
    jsr     l34_blk               | pattern's block
    movea.l %a0,%a3
    moveq   #0,%d2                | i
    moveq   #0,%d3                | the LFO
lcc_lfo:
    moveq   #0,%d4                | the track
lcc_trk:
    tstb    %a3@(LCC_OFF,%d2:l)
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
    lea.l   %a3@(LCC_OFF+LCC_N),%a0   | from the Base
    moveq   #0,%d1
    moveb   %a0@(0,%d2:l),%d1
    eoril   #64,%d1
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

| lcc_cur -> a0 = the playing pattern's settings (its kit's block + LCC_OFF:
| the numbers, then the bases ^ 64). Keeps everything else.
lcc_cur:
    lea.l   %sp@(-8),%sp
    moveml  %d0-%d1,%sp@
    movel   CUR_KIT,%d0
    jsr     l34_blk
    lea.l   %a0@(LCC_OFF),%a0
    moveml  %sp@,%d0-%d1
    lea.l   %sp@(8),%sp
    rts

| lcc_remask(a0 = the settings): lcc_mask = bit l for each LFO l that some
| track sends. Keeps everything but d0/d1.
lcc_remask:
    movel   %d2,%sp@-
    movel   %a1,%sp@-
    lea.l   lcc_lof,%a1
    moveq   #0,%d0                | the mask
    moveq   #0,%d1                | i
lrm_l:
    tstb    %a0@(0,%d1:l)
    beqs    lrm_n
    moveq   #0,%d2
    moveb   %a1@(0,%d1:l),%d2     | its LFO
    bset    %d2,%d0
lrm_n:
    addql   #1,%d1
    moveq   #LCC_N,%d2
    cmpl    %d1,%d2
    bnes    lrm_l
    movel   %d0,lcc_mask
    movea.l %sp@+,%a1
    movel   %sp@+,%d2
    rts

| lcc_poll, on the UI clock (led_hook): the file the first time, then at most
| one CC. Keeps d2-d7/a2-a6.
lcc_poll:
    tstl    lcc_lpn               | patterns loaded with settings to read
    beqs    lcp_go
    bsr     lcc_fread
lcp_go:
    addql   #1,lcc_tick
    bsr     lcc_cur
    bsr     lcc_remask
    tstl    lcc_mask
    beqw    lcp_x
    lea.l   %sp@(-20),%sp
    moveml  %d2-%d5/%a2,%sp@
    movea.l %a0,%a2               | the playing pattern's settings
    movel   lcc_rr,%d2
    moveq   #LCC_N-1,%d3
lcp_l:
    addql   #1,%d2                | the next LFO, round
    moveq   #LCC_N,%d0
    cmpl    %d2,%d0
    bhis    lcp_i
    moveq   #0,%d2
lcp_i:
    moveq   #0,%d4
    moveb   %a2@(0,%d2:l),%d4     | its CC + 1
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
    moveml  %sp@,%d2-%d5/%a2
    lea.l   %sp@(20),%sp
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
    bsr     lcc_cur
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
    bsr     lcc_cur
    moveq   #0,%d1
    moveb   %a0@(LCC_N,%d0:l),%d1
    eoril   #64,%d1
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
    bsr     lcc_cur
    lea.l   %a0@(LCC_N),%a0
    adda.l  %d0,%a0
    moveq   #0,%d1
    moveb   %a0@,%d1
    eoril   #64,%d1
    moveq   #127,%d0
    bsr     lcc_step
    eoril   #64,%d1
    moveb   %d1,%a0@
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
    bsr     lcc_cur
    movel   lcc_pv,%d1
    cmpb    %a0@(0,%d0:l),%d1
    beqs    lp_tog                | unchanged
    moveb   %d1,%a0@(0,%d0:l)
    moveq   #-1,%d1               | sent again, from its first value
    lea.l   lcc_sent,%a0
    moveb   %d1,%a0@(0,%d0:l)
    lea.l   lcc_val,%a0
    moveb   %d1,%a0@(0,%d0:l)
    bsr     lcc_cur
    bsr     lcc_remask
    bras    lp_tog
lp_in:
    movel   %d0,lcc_ei            | entering it
    bsr     lcc_cur
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

| ---- saved with the pattern -------------------------------------------------
| A kit's storage record has no room for 48 bytes, but +700..+707 are free
| (the FX save writes zeros there, the FX load never reads them). There go
| "C", the FNV-1a hash of the kit's settings and a check byte; the settings
| themselves go in a file named by the hash, LCC_DIR + 8 hex digits - the
| same settings, the same small file, whichever patterns and projects have
| them. Settings all OFF and 64 (all zero, as stored) write nothing: an
| older record (zeros there) loads them.
|   saving   lcc_ksave, from kit_save_post: the tag, and the kit queued for its
|            file, written by a UI-task message once the save is done
|            (lcc_write, as the slice markers' files are written)
|   loading  lcc_kload, from kit_load_post: the kit's settings cleared, and
|            its hash noted (lcc_lh); lcc_poll reads one such file a block
|            on the UI clock (lcc_fread) - none during the load itself

| lcc_area(d0 = a kit's index) -> a0 = its settings. Clobbers d1.
lcc_area:
    movel   #L34_BLK,%d1
    mulul   %d0,%d1
    lea.l   lfo34_kits+LCC_OFF,%a0
    adda.l  %d1,%a0
    rts

| lcc_hash(a0 = settings) -> d0 = their FNV-1a hash, 0 when all zero (1 for
| a real 0). Clobbers d1/a1.
lcc_hash:
    movel   %d2,%sp@-
    movea.l %a0,%a1
    movel   #0x811c9dc5,%d0
    moveq   #0,%d2                | any set?
    moveq   #LCC_SZ-1,%d1
lhh_l:
    movel   %d1,%sp@-
    moveq   #0,%d1
    moveb   %a1@+,%d1
    orl     %d1,%d2
    eorl    %d1,%d0
    movel   #16777619,%d1
    mulsl   %d1,%d0
    movel   %sp@+,%d1
    subql   #1,%d1
    bpls    lhh_l
    tstl    %d2
    bnes    lhh_set
    moveq   #0,%d0
    bras    lhh_out
lhh_set:
    tstl    %d0
    bnes    lhh_out
    moveq   #1,%d0
lhh_out:
    movel   %sp@+,%d2
    rts

| lcc_ksave(d0 = the kit's index, or -1; a2 = the storage record). Keeps
| d2-d7/a2-a6.
lcc_ksave:
    tstl    %d0
    bmis    lks_out
    movel   %d0,%sp@-
    bsr     lcc_area
    bsr     lcc_hash
    movel   %sp@+,%d1
    tstl    %d0
    beqs    lks_out               | nothing set: the stock zeros
    lea.l   lcc_wq,%a0            | its file, once the save is done
    adda.l  %d1,%a0
    moveq   #1,%d1
    moveb   %d1,%a0@
    lea.l   %a2@(LCC_REC),%a0
    moveq   #LCC_TAG,%d1
    moveb   %d1,%a0@+
    movel   %d2,%sp@-
    moveq   #24,%d1               | the hash, high byte first
lks_b:
    movel   %d0,%d2
    lsrl    %d1,%d2
    moveb   %d2,%a0@+
    subql   #8,%d1
    bpls    lks_b
    movel   %sp@+,%d2
    bsr     lcc_chk
    moveb   %d1,%a0@
    bsr     lcc_post
lks_out:
    rts

| lcc_chk(d0 = a hash) -> d1 = its check byte. Keeps d0.
lcc_chk:
    movel   %d0,%d1
    movel   %d2,%sp@-
    movel   %d0,%d2
    lsrl    #8,%d2
    eorl    %d2,%d1
    lsrl    #8,%d2
    eorl    %d2,%d1
    lsrl    #8,%d2
    eorl    %d2,%d1
    movel   %sp@+,%d2
    eoril   #0x5a,%d1
    andil   #0xff,%d1
    rts

| lcc_kload(d0 = the kit's index, a2 = the storage record): its settings
| cleared, and their hash noted for lcc_fread. Keeps d2-d7/a2-a6.
lcc_kload:
    movel   %d2,%sp@-
    movel   %d0,%d2
    bsr     lcc_area
    moveq   #LCC_SZ/4-1,%d1
lkl_c:
    clrl    %a0@+
    subql   #1,%d1
    bpls    lkl_c
    lea.l   lcc_lh,%a1
    lea.l   %a1@(0,%d2:l:4),%a1
    clrl    %a1@
    lea.l   %a2@(LCC_REC),%a0
    moveq   #LCC_TAG,%d0
    cmpb    %a0@+,%d0
    bnes    lkl_out
    moveq   #0,%d0
    moveq   #3,%d1
lkl_h:
    lsll    #8,%d0
    moveb   %a0@+,%d0
    subql   #1,%d1
    bpls    lkl_h
    movel   %d0,%sp@-
    bsr     lcc_chk
    movel   %sp@+,%d0
    moveq   #0,%d2
    moveb   %a0@,%d2
    cmpl    %d1,%d2
    bnes    lkl_out
    tstl    %d0
    beqs    lkl_out
    movel   %d0,%a1@
    moveq   #1,%d0
    movel   %d0,lcc_lpn
lkl_out:
    movel   %sp@+,%d2
    rts

| lcc_kclr(d0 = the kit's index, or -1): its settings back to OFF and 64 (a
| new kit). Keeps d2-d7/a2-a6.
lcc_kclr:
    tstl    %d0
    bmis    lkc_out
    lea.l   lcc_lh,%a0
    clrl    %a0@(0,%d0:l:4)
    bsr     lcc_area
    moveq   #LCC_SZ/4-1,%d1
lkc_c:
    clrl    %a0@+
    subql   #1,%d1
    bpls    lkc_c
lkc_out:
    rts

| lcc_path(d0 = a hash) -> a0 = LCC_DIR + its 8 hex digits, in lcc_pbuf.
| Clobbers d0/d1/a1.
lcc_path:
    lea.l   lcc_pbuf,%a0
    lea.l   lcc_dir,%a1
lp_d:
    moveb   %a1@+,%a0@+
    bnes    lp_d
    subql   #1,%a0
    movel   %d2,%sp@-
    moveq   #28,%d1
lp_x:
    movel   %d0,%d2
    lsrl    %d1,%d2
    andil   #15,%d2
    addil   #48,%d2
    cmpil   #58,%d2
    bcss    lp_dig
    addql   #7,%d2                | A..F
lp_dig:
    moveb   %d2,%a0@+
    subql   #4,%d1
    bpls    lp_x
    movel   %sp@+,%d2
    clrb    %a0@
    lea.l   lcc_pbuf,%a0
    rts

| lcc_fread, on the UI clock while lcc_lpn: the first kit with a hash noted -
| its file, if it is there and right, into its settings. Keeps d2-d7/a2-a6.
lcc_fread:
    linkw   %fp,#-72
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d3/%a2,%sp@
    lea.l   lcc_lh,%a2
    moveq   #0,%d2
lfr_f:
    movel   %a2@(0,%d2:l:4),%d3
    bnes    lfr_go
    addql   #1,%d2
    cmpil   #NPAT,%d2
    bcss    lfr_f
    clrl    lcc_lpn               | none left
    bras    lfr_out
lfr_go:
    clrl    %a2@(0,%d2:l:4)
    movel   %d3,%d0
    bsr     lcc_path
    pea     %fp@(-16)             | fh
    pea     0x40126ce4            | "r"
    movel   %a0,%sp@-
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnes    lfr_out               | not there: OFF and 64
    pea     %fp@(-16)
    pea     LCC_SZ
    pea     %fp@(-64)
    jsr     0x4007baaa
    lea.l   %sp@(12),%sp
    movel   %d0,%sp@-
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
    movel   %sp@+,%d0
    moveq   #LCC_SZ,%d1
    cmpl    %d0,%d1
    bnes    lfr_out
    lea.l   %fp@(-64),%a0         | the right file?
    bsr     lcc_hash
    cmpl    %d3,%d0
    bnes    lfr_out
    movel   %d2,%d0
    bsr     lcc_area
    lea.l   %fp@(-64),%a1
    moveq   #LCC_SZ/4-1,%d1
lfr_c:
    movel   %a1@+,%a0@+
    subql   #1,%d1
    bpls    lfr_c
lfr_out:
    moveml  %sp@,%d2-%d3/%a2
    unlk    %fp
    rts

| lcc_post: the UI-task message that writes the queued kits' files (as
| mk_post). Clobbers d0/d1/a0/a1.
lcc_post:
    lea.l   lcc_msg,%a0
    tstl    %a0@(0x18)
    bnes    lpo_out               | queued already: that one will do
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
lpo_out:
    rts

| lcc_write (closure, arg), in the UI task: each queued kit's file, named by
| its settings' hash now, written (as mk_write writes a slice file, its
| folder made if it is not there).
lcc_write:
    linkw   %fp,#-40
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d3/%a2,%sp@
    moveq   #0,%d2
lw_k:
    lea.l   lcc_wq,%a0
    tstb    %a0@(0,%d2:l)
    beqw    lw_n
    clrb    %a0@(0,%d2:l)
    movel   %d2,%d0
    bsr     lcc_area
    movea.l %a0,%a2
    bsr     lcc_hash
    tstl    %d0
    beqw    lw_n
    bsr     lcc_path
    movel   %a0,%sp@-
    jsr     0x4007c18e
    movel   %sp@,%d0
    movel   %d0,%sp@-
    jsr     0x40095284            | its folder
    addql   #4,%sp
    tstl    %d0
    beqs    lw_open
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
lw_open:
    movel   %sp@+,%d3             | the path
    pea     %fp@(-16)
    pea     0x401272db            | "w"
    movel   %d3,%sp@-
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnes    lw_n
    pea     LCC_SZ
    clrl    %sp@-
    pea     %fp@(-16)
    jsr     0x4007bb76
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    pea     LCC_SZ
    movel   %a2,%sp@-
    jsr     0x4007bb0a
    lea.l   %sp@(12),%sp
    pea     %fp@(-16)
    jsr     0x4007e2f6
    addql   #4,%sp
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
lw_n:
    addql   #1,%d2
    cmpil   #NPAT,%d2
    bcsw    lw_k
    moveml  %sp@,%d2-%d3/%a2
    unlk    %fp
    rts

    .align 4
lcc_st:     .long LFO_STATE, lfo2_state, lfo34_state, lfo34_state+192
lcc_mask:   .long 0               | bit l: LFO l sends a CC on some track
lcc_tick:   .long LCC_GAP         | led_hook's blocks (from LCC_GAP: the first
                                  | send waits for nothing)
lcc_rr:     .long LCC_N-1         | the last LFO looked at
lcc_lpn:    .long 0               | some kit's settings are still to read
lcc_lh:     .space 4*NPAT         | each kit's settings' hash, still to read
lcc_wq:     .space NPAT           | kits whose settings' file is to write
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
lcc_val:    .rept LCC_N           | the values to send; 0xff: none yet
            .byte 0xff
            .endr
lcc_sent:   .rept LCC_N           | the values sent; 0xff: none yet
            .byte 0xff
            .endr
lcc_tk:     .byte 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5, 0,1,2,3,4,5
lcc_lof:    .byte 0,0,0,0,0,0, 1,1,1,1,1,1, 2,2,2,2,2,2, 3,3,3,3,3,3
lcc_vbuf:   .space 4
lcc_dir:    .asciz "/LFOCC/"
lcc_pbuf:   .space 16
lcc_s_lab:  .asciz "MIDI CC"
lcc_s_labb: .asciz "CC Base"
lcc_s_off:  .asciz "OFF"
    .align 2
