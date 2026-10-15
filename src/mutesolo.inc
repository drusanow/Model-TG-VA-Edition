| ============================================================================
| ==================  Quantised mute and track solo  =========================
|
| See docs/MUTESOLO.md. A mute (FUNC + pad, or latched mute mode) and a solo
| (SETTINGS + pad) wait for the next bar, or for the pattern to come round,
| before they take effect; Device Config > MUTE Q chooses OFF, 1 BAR or PTN.
|
| THE STATE. The stock mute is a mask in the playing pattern (+20 of the
| object manager + 48 returns through vtable +40), read by 0x4001263c(obj,
| track) and set by 0x4001381c(obj, track, on, cc), which also tells the
| sequencer and sends CC 94. Nothing here writes the mask: a change waits in
| qm_want / qm_dirty (the state wanted for the tracks that are to change)
| and is then made through 0x4001381c, exactly as a stock pad would make it.
|
| THE MOMENT. The sequencer tick (0x40054c1e) builds a step's trigs and then
| advances its step counter 0x40a78840, which therefore holds the NEXT step
| (0 after the last). So while it reads a multiple of 16 the bar's first step
| has not been built yet, and while it reads 0 neither has the pattern's: a
| change made then lands exactly on that step. qm_poll runs on the UI clock
| (led_hook, once a block at most, and tick_hook) and applies the waiting
| changes as soon as the counter is on such a step - with OFF, or the
| sequencer stopped (0x40a7883c != 1), at once.
|
| THE PADS. Mute mode already queues its pads and applies them when it closes
| (mq_apply, model_tg.s); each queued pad now goes through qm_flip. The mute
| mode's pads show the state to come (mq_muted asks qm_fut). SETTINGS + a pad
| on the main page solos that track (qm_solo); the same chord on the soloed
| track brings back the mutes from before the solo.
| ============================================================================

QM_PLAY = 0x40a7883c              | 1 while the sequencer plays
QM_STEP = 0x40a78840              | its next step: wraps to 0 after the last

    .align 2
| qm_obj -> d0 = the mute object (manager + 48). Clobbers d1/a0/a1.
qm_obj:
    jsr     0x400cf866
    movel   %d0,%sp@-
    jsr     0x4000eb90
    addql   #4,%sp
    rts

| qm_cur -> d0 = the six tracks' mutes now (bit t = track t). Clobbers d1/a0/a1.
qm_cur:
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d4,%sp@
    bsr     qm_obj
    movel   %d0,%d4
    moveq   #0,%d3
    moveq   #0,%d2
qc_l:
    movel   %d2,%sp@-
    movel   %d4,%sp@-
    jsr     0x4001263c            | muted?
    addql   #8,%sp
    tstb    %d0
    beqs    qc_n
    bset    %d2,%d3
qc_n:
    addql   #1,%d2
    moveq   #6,%d0
    cmpl    %d2,%d0
    bnes    qc_l
    movel   %d3,%d0
    moveml  %sp@,%d2-%d4
    lea.l   %sp@(12),%sp
    rts

| qm_fut -> d0 = the mutes once the waiting changes are made. Clobbers d1/a0/a1.
qm_fut:
    bsr     qm_cur
    movel   qm_dirty,%d1
    notl    %d1
    andl    %d1,%d0
    movel   qm_want,%d1
    andl    qm_dirty,%d1
    orl     %d1,%d0
    rts

| qm_flip(d0 = track): a mute pad - that track's mute flips, at the boundary.
| A mute by hand ends a solo's memory. Keeps d2-d7/a2-a6.
qm_flip:
    clrl    qm_saved_ok
    movel   %d2,%sp@-
    movel   %d0,%d2
    bsr     qm_fut
    bchg    %d2,%d0
    bsr     qm_target
    movel   %sp@+,%d2
    rts

| qm_target(d0 = the mutes wanted): the tracks that differ from now wait for
| the boundary (or change at once: qm_poll). Keeps d2-d7/a2-a6.
qm_target:
    movel   %d2,%sp@-
    movel   %d0,%d2
    andil   #0x3f,%d2
    bsr     qm_cur
    eorl    %d2,%d0
    movel   %d0,qm_dirty
    movel   %d2,qm_want
    movel   %sp@+,%d2
    bra     qm_poll

| qm_solo(d0 = track): mute every other track; on the track already soloed,
| the mutes from before the solo (the first, when solos follow each other). Shows "SOLO n" / "SOLO OFF". Keeps d2-d7/a2-a6.
qm_solo:
    lea.l   %sp@(-8),%sp
    moveml  %d2-%d3,%sp@
    movel   %d0,%d2
    bsr     qm_fut
    movel   %d0,%d3               | the mutes to come
    moveq   #0x3f,%d1
    bclr    %d2,%d1               | this track alone
    cmpl    %d1,%d3
    bnes    qs_on
    tstl    qm_saved_ok           | soloed already: back to before
    beqs    qs_all
    movel   qm_saved,%d0
    bras    qs_off
qs_all:
    moveq   #0,%d0                | nothing saved: every track on
qs_off:
    clrl    qm_saved_ok
    bsr     qm_target
    pea     qm_s_off
    bras    qs_show
qs_on:
    tstl    qm_saved_ok           | from one solo to another: keep the mutes
    bnes    qs_keep               | from before the first
    movel   %d3,qm_saved
    moveq   #1,%d0
    movel   %d0,qm_saved_ok
qs_keep:
    movel   %d1,%d0
    bsr     qm_target
    moveq   #49,%d0               | '1' + track
    addl    %d2,%d0
    lea.l   qm_s_on,%a0
    moveb   %d0,%a0@(5)
    pea     qm_s_on
qs_show:
    jsr     show_popup
    addql   #4,%sp
    moveml  %sp@,%d2-%d3
    lea.l   %sp@(8),%sp
    rts

| qm_poll: on the UI clock (and after every request). Makes the waiting
| changes when the sequencer is on the boundary, or at once with MUTE Q off
| or the sequencer stopped. Keeps d2-d7/a2-a6.
qm_poll:
    movel   qm_dirty,%d0
    beqs    qp_out
    movel   qm_mode,%d1
    beqs    qm_apply              | OFF
    moveq   #1,%d0
    cmpl    QM_PLAY,%d0
    bnes    qm_apply              | stopped: now
    movel   QM_STEP,%d0           | the next step
    subql   #1,%d1
    bnes    qp_pat
    andil   #15,%d0               | 1 BAR: a bar's first step
qp_pat:
    tstl    %d0                   | PTN: the pattern's first step
    beqs    qm_apply
qp_out:
    rts

| qm_apply: every waiting change, through the stock setter (CC and all).
qm_apply:
    lea.l   %sp@(-16),%sp
    moveml  %d2-%d4/%a2,%sp@
    movel   qm_dirty,%d3
    clrl    qm_dirty
    bsr     qm_obj
    movel   %d0,%d4
    moveq   #0,%d2
qa_l:
    btst    %d2,%d3
    beqs    qa_n
    movel   qm_want,%d0
    btst    %d2,%d0
    sne     %d0
    andil   #1,%d0
    pea     1                     | with the CC, as the stock pad
    movel   %d0,%sp@-
    movel   %d2,%sp@-
    movel   %d4,%sp@-
    jsr     0x4001381c
    lea.l   %sp@(16),%sp
qa_n:
    addql   #1,%d2
    moveq   #6,%d0
    cmpl    %d2,%d0
    bnes    qa_l
    moveml  %sp@,%d2-%d4/%a2
    lea.l   %sp@(16),%sp
    rts

| ---- SETTINGS + a pad: solo ---------------------------------------------
| The main page's pad handler (its PadHandler vtable entry 0x401002b0, the
| thunk 0x4001d3d4 to 0x4001d180) is (view + 16, event): event +16 is 1 for
| a press, +20 the pad (1..6 = tracks 0..5). With SETTINGS (key 13) held, a
| press solos its track and nothing else sees the pad; the chord counts as
| one (mod_used), so SETTINGS' release opens no Config menu.
qm_pad:
    pea     13
    jsr     0x4007faf4            | SETTINGS held?
    addql   #4,%sp
    tstl    %d0
    beqs    qpd_stock
    movea.l %sp@(8),%a0
    moveq   #1,%d0
    cmpl    %a0@(16),%d0
    bnes    qpd_eat               | its release: nobody else had the press
    movel   %a0@(20),%d0
    subql   #1,%d0
    moveq   #5,%d1
    cmpl    %d0,%d1
    bcss    qpd_eat
    moveq   #1,%d1
    movel   %d1,mod_used
    bsr     qm_solo
qpd_eat:
    moveq   #1,%d0
    rts
qpd_stock:
    jmp     0x4001d3d4

| ---- Device Config > MUTE Q -------------------------------------------------
qm_lab:
    lea.l   qm_s_lab,%a1
    jmp     gm_lab
qm_val:
    lea.l   qm_mode,%a0
    lea.l   qm_names,%a1
    jmp     gm_val
| turn (closure, item, delta): one step a click, OFF .. PTN, then redraw the
| menu (as id_rot).
qm_rot:
    movel   %sp@(12),%d0
    beqs    qr_out
    movel   qm_mode,%d1
    tstl    %d0
    bgts    qr_up
    subql   #1,%d1
    bpls    qr_set
    moveq   #0,%d1
    bras    qr_set
qr_up:
    addql   #1,%d1
    moveq   #2,%d0
    cmpl    %d1,%d0
    bges    qr_set
    moveq   #2,%d1
qr_set:
    movel   %d1,qm_mode
    movea.l %sp@(4),%a0           | the closure -> its data -> the menu
    movea.l %a0@,%a0
    movel   %a0@,%d0
    beqs    qr_out
    addil   #0x38,%d0             | the menu's view
    movel   %d0,%sp@-
    jsr     0x40076082
    addql   #4,%sp
qr_out:
    rts

    .align 4
qm_mode:      .long 1          | 0 OFF, 1 1 BAR, 2 PTN; every boot starts at 1 BAR
qm_want:      .long 0          | the mutes wanted, for the tracks in qm_dirty
qm_dirty:     .long 0          | tracks whose mute changes at the boundary
qm_saved:     .long 0          | the mutes before a solo
qm_saved_ok:  .long 0
qm_row:       .long qm_lab, qm_val, qm_rot, new_slot2_invoker
qm_names:     .long qm_s_n0, qm_s_n1, qm_s_n2
qm_s_lab:     .asciz "Mute Q"
qm_s_n0:      .asciz "OFF"
qm_s_n1:      .asciz "1BAR"
qm_s_n2:      .asciz "PTN"
qm_s_on:      .asciz "SOLO 1"
qm_s_off:     .asciz "SOLO OFF"
    .align 2
