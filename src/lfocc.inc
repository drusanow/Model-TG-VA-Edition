| ============================================================================
| =========================  LFO -> MIDI CC  =================================
|
| See docs/LFOCC.md. The LFO menu gets a fifth row, MIDI CC: OFF, or a CC
| number 0..127 that the LFO showing (1..4) of the selected track sends its
| modulation to, on that track's MIDI channel. It goes out as well as to the
| LFO's Destination, so Destination NONE makes it a CC-only LFO.
|
| THE NUMBERS are one byte per LFO and track (lcc_num, i = LFO x 6 + track: 0
| OFF, n + 1 for CC n), a setting of the machine like the tracks' MIDI
| channels, not of the pattern: a pattern's kit record has no room left. They
| are kept in a file, LCC_PATH, read once after boot and written (a UI-task
| message, as the slice markers' are) when an LFO menu that changed one
| closes.
|
| THE VALUE. After the four LFOs have run (lfo_run), lcc_calc turns each one
| with a CC into 0..127: the engine's own wave, state +4 (after its fade),
| times the LFO's Depth as the engine scales it, (Depth - 16384) x 2 -
| exactly what it adds to a destination word, where a full Depth sweeps
| +-32768 - mapped as 64 + that / 512, clipped. So Depth 0 sends 64, and a
| full Depth the whole 0..127. LFO 3 and 4 are run while any track sends
| them, even with no Destination.
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
LCC_MAGIC = 0x4C434331            | "LCC1"
LCC_SIZE  = 4 + LCC_N + 4         | the file: magic, the numbers, 4 spare

    .align 2
| lcc_calc(a5 = the parameter block), at the end of lfo_run: lcc_val. Keeps
| d2-d7/a2-a6.
lcc_calc:
    tstl    lcc_mask
    beqw    lcc_x
    lea.l   %sp@(-24),%sp
    moveml  %d2-%d5/%a2-%a3,%sp@
    movel   CUR_KIT,%d0           | LFO 3 and 4's Depth: the playing pattern's
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
    movel   %d4,%d0               | its Depth
    mulu.w  #66,%d0
    lea.l   %a5@(14+2*8,%d0:l),%a0       | LFO 1: k 8
    tstl    %d3
    beqs    lcc_dep
    lea.l   %a5@(14+2*(LFO2_K0+4),%d0:l),%a0 | LFO 2: k 32
    moveq   #1,%d0
    cmpl    %d3,%d0
    beqs    lcc_dep
    movel   %d4,%d0               | LFO 3 / 4: the pattern's block
    mulu.w  #10,%d0
    lea.l   %a3@(8,%d0:l),%a0
    moveq   #2,%d0
    cmpl    %d3,%d0
    beqs    lcc_dep
    lea.l   %a0@(60),%a0
lcc_dep:
    mvsw    %a0@,%d1
    subil   #16384,%d1
    addl    %d1,%d1
    movel   %a2@(4),%d0           | the wave, after its fade
    swap    %d0
    extl    %d0
    mulsl   %d1,%d0               | +-2^30 at full Depth
    asrl    #8,%d0
    asrl    #8,%d0
    asrl    #8,%d0                | / 2^24: +-64
    addil   #64,%d0
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
    moveml  %sp@,%d2-%d5/%a2-%a3
    lea.l   %sp@(24),%sp
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
| value: "OFF" or the number, through gm_val with a one-name list
lcc_value:
    bsr     lcc_i
    lea.l   lcc_s_off,%a0
    tstl    %d0
    bmis    lv_txt
    lea.l   lcc_num,%a0
    moveq   #0,%d1
    moveb   %a0@(0,%d0:l),%d1
    lea.l   lcc_s_off,%a0
    subql   #1,%d1
    bmis    lv_txt
    lea.l   lcc_vbuf,%a0          | the CC number, in decimal
    moveq   #100,%d0
    cmpl    %d0,%d1
    bcss    lv_10
    moveb   #49,%a0@+             | "1" hundred
    subl    %d0,%d1
    bras    lv_t
lv_10:
    moveq   #10,%d0
    cmpl    %d0,%d1
    bcss    lv_u
lv_t:
    moveq   #48,%d0               | the tens
lv_tl:
    cmpil   #10,%d1
    bcss    lv_te
    addql   #1,%d0
    subil   #10,%d1
    bras    lv_tl
lv_te:
    moveb   %d0,%a0@+
lv_u:
    addil   #48,%d1
    moveb   %d1,%a0@+
    clrb    %a0@
    lea.l   lcc_vbuf,%a0
lv_txt:
    movel   %a0,lcc_name
    lea.l   lcc_zero,%a0
    lea.l   lcc_name,%a1
    jmp     gm_val

| turn (closure, item, delta): one step a click, OFF .. 127; then redraw the
| menu, as qm_rot does
lcc_rot:
    movel   %sp@(12),%d0
    beqs    lr_out
    bsr     lcc_i
    tstl    %d0
    bmis    lr_out
    lea.l   lcc_num,%a0
    adda.l  %d0,%a0
    moveq   #0,%d1
    moveb   %a0@,%d1
    tstl    %sp@(12)
    bgts    lr_up
    subql   #1,%d1
    bpls    lr_set
    moveq   #0,%d1
    bras    lr_set
lr_up:
    addql   #1,%d1
    cmpil   #128,%d1
    blss    lr_set
    movel   #128,%d1
lr_set:
    moveb   %d1,%a0@
    moveq   #-1,%d1               | sent again, from its first value
    lea.l   lcc_sent,%a0
    moveb   %d1,%a0@(0,%d0:l)
    lea.l   lcc_val,%a0
    moveb   %d1,%a0@(0,%d0:l)
    moveq   #1,%d0
    movel   %d0,lcc_dirty
    bsr     lcc_remask
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

| lcc_row_add(d0 = the LFO menu, just built): the row added at its end, as
| lm_build adds a list menu's rows. Keeps d2-d7/a2-a6.
lcc_row_add:
    linkw   %fp,#-64
    lea.l   %sp@(-12),%sp
    moveml  %a2-%a4,%sp@
    movea.l %d0,%a2
    lea.l   ALLOC,%a4
    lea.l   %fp@(-64),%a3         | four closures: label, press, value, turn
    pea     0x1
    jsr     %a4@
    addql   #4,%sp
    movel   %d0,%a3@
    clrl    %a3@(4)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a3@(8)
    movel   #lcc_lab,%d1
    movel   %d1,%a3@(12)
    pea     0x4
    jsr     %a4@
    addql   #4,%sp
    movea.l %d0,%a0
    movel   %a2,%a0@
    movel   %d0,%a3@(16)
    clrl    %a3@(20)
    movel   #SLOT2_MANAGER,%d1
    movel   %d1,%a3@(24)
    movel   #new_slot2_invoker,%d1
    movel   %d1,%a3@(28)
    pea     0x1
    jsr     %a4@
    addql   #4,%sp
    movel   %d0,%a3@(32)
    clrl    %a3@(36)
    movel   #GENERIC_MANAGER,%d1
    movel   %d1,%a3@(40)
    movel   #lcc_value,%d1
    movel   %d1,%a3@(44)
    pea     0x4
    jsr     %a4@
    addql   #4,%sp
    movea.l %d0,%a0
    movel   %a2,%a0@
    movel   %d0,%a3@(48)
    clrl    %a3@(52)
    movel   #SLOT2_MANAGER,%d1
    movel   %d1,%a3@(56)
    movel   #lcc_rot,%d1
    movel   %d1,%a3@(60)
    pea     0x54
    jsr     %a4@
    addql   #4,%sp
    tstl    %d0
    beqs    lra_free
    movea.l %d0,%a4
    pea     0x8
    pea     -1
    pea     %a3@(48)
    pea     %a3@(32)
    pea     %a3@(16)
    movel   %a3,%sp@-
    movel   %a4,%sp@-
    jsr     MENUITEM_CTOR
    lea.l   %sp@(28),%sp
    movel   %a4,%sp@-
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
    moveml  %sp@,%a2-%a4
    unlk    %fp
    rts

| ---- the file -------------------------------------------------------------
| lcc_load: the numbers from LCC_PATH, if it is a good one (as mk_read reads
| a slice file). Keeps d2-d7/a2-a6.
lcc_load:
    linkw   %fp,#-56
    movel   %d2,%sp@-
    pea     %fp@(-16)             | fh
    pea     0x40126ce4            | "r"
    pea     lcc_path
    jsr     0x4007bc5e
    lea.l   %sp@(12),%sp
    tstl    %d0
    bnes    ll_out                | no file: every one OFF
    pea     %fp@(-16)
    pea     LCC_SIZE
    pea     %fp@(-48)
    jsr     0x4007baaa
    lea.l   %sp@(12),%sp
    movel   %d0,%d2
    pea     %fp@(-16)
    jsr     0x4007bc1c
    addql   #4,%sp
    moveq   #LCC_SIZE,%d0
    cmpl    %d2,%d0
    bnes    ll_out
    lea.l   %fp@(-48),%a0
    movel   %a0@+,%d0
    cmpil   #LCC_MAGIC,%d0
    bnes    ll_out
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
    linkw   %fp,#-56
    lea.l   %fp@(-48),%a1
    movel   #LCC_MAGIC,%a1@+
    lea.l   lcc_num,%a0
    moveq   #LCC_N-1,%d1
lw_c:
    moveb   %a0@+,%a1@+
    subql   #1,%d1
    bpls    lw_c
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
    pea     %fp@(-48)
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
lcc_vbuf:   .space 4
lcc_path:   .asciz "/lfo_cc.bin"
lcc_s_lab:  .asciz "MIDI CC"
lcc_s_off:  .asciz "OFF"
    .align 2
