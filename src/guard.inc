| ============================================================================
| ===========================  CPU guard  ====================================
|
| See docs/CPU.md. Every block's render time is measured (prof_end): when a
| block takes more than GD_HI percent of the time between blocks, the audio is
| close to missing its deadline - a dropout, and the screen and buttons
| starved. The guard then ends one note early: of the new machines' tracks
| (VA, the VA drums, PLAITS, DX, LXR: trk_mach >= VA_MACH) that are sounding,
| the one whose note started longest ago (gd_age; at least GD_AGE blocks, so
| a fresh hit is never taken) fades out over 64 samples - MUT's fade, gv_gain
| in amp_hook - and is then idle, as a finished note: its whole chain skipped
| (voice_quiet), so it costs nothing until its next trig, which plays as
| always. One note at most every GD_COOL blocks, so the load is measured again
| before another goes. The stock machines and the Sampler are never taken.
|
| Device Config > CPU Guard turns it ON (every boot) or OFF. Device Config >
| System shows how many notes it has ended since the page opened (GUARD n).
|
| Device Config > LXR Rate is here too: LXR at the LXR's own 44.14 kHz (the
| default, as its firmware: lxr.c) or at 48 kHz, without the resampling.
| ============================================================================

GD_HI   = 90                      | percent of the block
GD_AGE  = 24                      | blocks a note sounds before it can go (16 ms)
GD_COOL = 12                      | blocks between two (8 ms)

| gd_block(d0 = this block's load, percent): from prof_end, once a block,
| when the audio is done. Keeps d0, d2-d7/a2-a6.
gd_block:
    lea.l   %sp@(-16),%sp
    moveml  %d0/%d2-%d4,%sp@
    lea.l   gd_age,%a0            | every track's note is a block older
    moveq   #MAX_TRK-1,%d1
gb_age:
    addql   #1,%a0@+
    subql   #1,%d1
    bpls    gb_age
    tstl    gd_cool
    beqs    gb_c0
    subql   #1,gd_cool
    bras    gb_out
gb_c0:
    tstl    gd_on
    beqs    gb_out
    cmpil   #GD_HI,%d0
    blss    gb_out                | under the line: nothing to do
    moveq   #-1,%d2               | the note to end
    moveq   #GD_AGE-1,%d3         | the oldest so far (it must be older)
    moveq   #0,%d4
gb_t:
    lea.l   trk_mach,%a0
    moveq   #VA_MACH,%d0
    cmpl    %a0@(0,%d4:l:4),%d0
    bgts    gb_n                  | a stock machine or the Sampler
    movel   gd_mask,%d0
    btst    %d4,%d0
    bnes    gb_n                  | going already
    lea.l   sv_idle,%a0
    tstb    %a0@(0,%d4:l)
    bnes    gb_n                  | silent
    lea.l   gv_gain,%a0
    tstl    %a0@(0,%d4:l:4)
    beqs    gb_n                  | faded out (muted)
    lea.l   gd_age,%a0
    movel   %a0@(0,%d4:l:4),%d0
    cmpl    %d3,%d0
    blss    gb_n
    movel   %d0,%d3
    movel   %d4,%d2
gb_n:
    addql   #1,%d4
    moveq   #MAX_TRK,%d0
    cmpl    %d4,%d0
    bhis    gb_t
    tstl    %d2
    bmis    gb_out                | none to end
    movel   gd_mask,%d0
    bset    %d2,%d0
    movel   %d0,gd_mask
    addql   #1,gd_count
    moveq   #GD_COOL,%d0
    movel   %d0,gd_cool
gb_out:
    moveml  %sp@,%d0/%d2-%d4
    lea.l   %sp@(16),%sp
    rts

| gd_trig(a0 = the voice, d1 = the track): from voice_quiet. A new trig on a
| track the guard ended: its note plays again, from full level (it was
| silent). Clobbers d0/a1.
gd_trig:
    movel   gd_mask,%d0
    btst    %d1,%d0
    beqs    gt_out
    tstl    %a0@(0x34)
    bnes    gt_go
    tstl    %a0@(0x38)
    beqs    gt_out
gt_go:
    bclr    %d1,%d0
    movel   %d0,gd_mask
    movel   rs_silence,%d0        | (MUT's or a hand mute keep their fade)
    orl     mute_sil,%d0
    btst    %d1,%d0
    bnes    gt_out
    lea.l   gv_gain,%a1
    movel   #32768,%d0
    movel   %d0,%a1@(0,%d1:l:4)
gt_out:
    rts

| gd_text(d0 = notes ended): "GUARD n" in sys_vbuf (up to 99999). Clobbers
| d0/d1/a0/a1.
gd_text:
    lea.l   gd_s_n,%a0
    lea.l   sys_vbuf,%a1
gx_c:
    moveb   %a0@+,%a1@+
    bnes    gx_c
    subql   #1,%a1                | on the 0
    cmpil   #99999,%d0
    blss    gx_n
    movel   #99999,%d0
gx_n:
    movel   %d2,%sp@-
    movel   %d3,%sp@-
    lea.l   gd_pow,%a0            | 10000, 1000, 100, 10, 1
    moveq   #0,%d3                | a digit written yet
gx_d:
    movel   %a0@+,%d1
    movel   %d0,%d2
    divul   %d1,%d2               | this digit
    movel   %d2,%sp@-
    mulul   %d1,%d2
    subl    %d2,%d0
    movel   %sp@+,%d2
    orl     %d2,%d3
    moveq   #1,%d1
    cmpl    %a0@(-4),%d1
    beqs    gx_w                  | the last digit: always
    tstl    %d3
    beqs    gx_d                  | no leading zeros
gx_w:
    addil   #48,%d2
    moveb   %d2,%a1@+
    moveq   #1,%d1
    cmpl    %a0@(-4),%d1
    bnes    gx_d
    clrb    %a1@
    movel   %sp@+,%d3
    movel   %sp@+,%d2
    rts
gd_pow:       .long 10000, 1000, 100, 10, 1

| ---- Device Config > CPU Guard (OFF / ON) and LXR Rate (44.1K / 48K) --------
gd_lab:
    lea.l   gd_s_lab,%a1
    jmp     gm_lab
gd_val:
    lea.l   gd_on,%a0
    lea.l   gd_names,%a1
    jmp     gm_val
gd_rot:
    lea.l   gd_on,%a1
    bras    gd_rot2
lr_lab:
    lea.l   lr_s_lab,%a1
    jmp     gm_lab
lr_val:
    lea.l   lx_r48,%a0
    lea.l   lr_names,%a1
    jmp     gm_val
lr_rot:
    lea.l   lx_r48,%a1
| turn (closure, item, delta), a1 = the setting: right ON (1), left OFF (0),
| then redraw the menu (as qm_rot)
gd_rot2:
    movel   %sp@(12),%d0
    beqs    gr_out
    smi     %d1
    extbl   %d1
    addql   #1,%d1                | right: 1, left: 0
    movel   %d1,%a1@
    movea.l %sp@(4),%a0           | the closure -> its data -> the menu
    movea.l %a0@,%a0
    movel   %a0@,%d0
    beqs    gr_out
    addil   #0x38,%d0             | the menu's view
    movel   %d0,%sp@-
    jsr     0x40076082
    addql   #4,%sp
gr_out:
    rts

    .align 4
    .globl  lx_r48
gd_on:        .long 1             | the guard: ON at every boot
lx_r48:       .long 0             | LXR at 48 kHz (lxr.c): off at every boot
gd_mask:      .long 0             | tracks whose note the guard has ended
gd_cool:      .long 0             | blocks until it may end another
gd_count:     .long 0             | notes ended since the System page opened
gd_age:       .long 0,0,0,0,0,0   | blocks since each track's last trig
gd_row:       .long gd_lab, gd_val, gd_rot, new_slot2_invoker
lr_row:       .long lr_lab, lr_val, lr_rot, new_slot2_invoker
gd_names:     .long gd_s_off, gd_s_on
lr_names:     .long lr_s_lxr, lr_s_48
gd_s_lab:     .asciz "CPU Guard"
gd_s_off:     .asciz "OFF"
gd_s_on:      .asciz "ON"
lr_s_lab:     .asciz "LXR Rate"
lr_s_lxr:     .asciz "44.1K"
lr_s_48:      .asciz "48K"
gd_s_n:       .asciz "GUARD "
gd_s_x:       .asciz "GUARD OFF"
    .align 2
