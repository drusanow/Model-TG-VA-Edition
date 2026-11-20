| ============================================================================
| ACHORD (machine 14): the glue between the voice loop / the pages and
| src/achord/achord.c (built into src/lxr/lxr_gen.s with lxr.c). MIT.
|
| ac_params_g: va_pre's branch for machine 14 (d2 = track, a2 = trackData):
|   ac_params(track, trackData, settings), the settings being the track's
|   block in the playing kit (lfo2.inc: AC_OFF, AC_SETB bytes a track).
| ac_cfmt: Contour's value -> the chord's name (mi_fmtswap installs it for
|   set 9): its type (FREE) or its size (in a key), from the selected track's
|   settings.
| ac_m_*: SETTINGS + PRESET on an ACHORD track: mm_open's page (the kind LXR's
|   voice menu uses: lx_m_* hand an ACHORD track's events here).
| ============================================================================
    .globl  ac_params_g, ac_cfmt, ac_swap, ac_m_mine, ac_m_draw, ac_m_turn, ac_m_press

ac_params_g:
    movel   %d2,%d0               | the settings, the playing kit's
    bsr     ac_setp
    movel   %a0,%sp@-
    movel   %a2,%sp@-
    movel   %d2,%sp@-
    jsr     ac_params
    lea.l   %sp@(12),%sp
    rts

| ac_setp(d0 = a track) -> a0 = its ACHORD settings in the playing kit's
| block. Clobbers d0/d1.
ac_setp:
    movel   %d0,%sp@-
    movel   CUR_KIT,%d0
    jsr     l34_blk
    movel   %sp@+,%d0
    mulu.w  #AC_SETB,%d0
    lea.l   %a0@(AC_OFF),%a0
    adda.l  %d0,%a0
    rts

| ac_m_mine -> d0 = 1 (flags set) if mm_track plays ACHORD. Clobbers a0.
ac_m_mine:
    movel   mm_track,%d0
    lea.l   trk_mach,%a0
    movel   %a0@(0,%d0:l:4),%d0
    subil   #AC_MACH,%d0
    seq     %d0
    extbl   %d0
    negl    %d0
    tstl    %d0
    rts

| ac_m_draw (d2 = ctx): the whole screen. Keeps d2-d7/a2-a6.
ac_m_draw:
    movel   mm_track,%d0
    bsr     ac_setp
    movel   mm_track,%sp@-
    movel   %a0,%sp@-
    movel   %d2,%sp@-
    jsr     ac_menu_draw
    lea.l   %sp@(12),%sp
    rts

| ac_m_turn (d2 = the encoder event) -> d0 = 1 to redraw. Keeps d2-d7/a2-a6.
ac_m_turn:
    movel   %d3,%sp@-
    movea.l %d2,%a0
    movel   %a0@(12),%d3          | the knob: 1 DATA
    movel   %d3,%sp@-
    jsr     ac_menu_accel
    addql   #4,%sp
    movel   %d0,%sp@-
    pea     0x1
    movel   %d2,%sp@-
    jsr     0x4006f73a            | clicks
    lea.l   %sp@(12),%sp
    tstl    %d0
    beqs    1f
    movel   %d0,%sp@-             | (set, knob, clicks)
    movel   %d3,%sp@-
    movel   mm_track,%d0
    bsr     ac_setp
    movel   %a0,%sp@-
    jsr     ac_menu_turn
    lea.l   %sp@(12),%sp
1:  movel   %sp@+,%d3
    rts

| ac_m_press: DATA's press -> d0 = 1 to redraw. Keeps d2-d7/a2-a6.
ac_m_press:
    movel   mm_track,%d0
    bsr     ac_setp
    movel   %a0,%sp@-
    jsr     ac_menu_press
    addql   #4,%sp
    rts

| (this, value, buf) -> sprintf(buf, "%s", the chord's name), as lx_eng_fmt
ac_cfmt:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    suba.l  %a0,%a0
    cmpil   #MAX_TRK,%d0
    bccs    1f
    bsr     ac_setp
1:  movel   %a0,%sp@-
    movel   %sp@(12),%sp@-        | the value
    jsr     ac_cname
    addql   #8,%sp
    movel   %sp@(12),%d1
    movel   %d1,%sp@(4)           | buf
    movel   #0x40124b58,%d1       | "%s"
    movel   %d1,%sp@(8)
    movel   %d0,%sp@(12)          | the name
    jmp     0x40000e6e            | sprintf

    .align 2
| the four knobs' rows, as lx_swap (offset, name, category, short, then the
| stock name, category, short put back)
ac_swap:
    .long 2576, s_acb, s_acc, s_acbs, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, s_acw, s_acc, s_acws, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, s_acf, s_acc, s_acfs, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, s_ach, s_acc, s_achs, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
s_acc:   .asciz "ACHORD"
s_acb:   .asciz "Bright"
s_acbs:  .asciz "BRIT"
s_acw:   .asciz "Wave"
s_acws:  .asciz "WAVE"
s_acf:   .asciz "Env Amt"
s_acfs:  .asciz "FENV"
s_ach:   .asciz "Chord"
s_achs:  .asciz "CHRD"
    .align 2
