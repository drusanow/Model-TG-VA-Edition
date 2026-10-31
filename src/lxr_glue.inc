| ============================================================================
| LXR (machine 13): the glue between the voice loop / the page and src/lxr/
| (lxr.c, GPL-3.0-only, drum voices after the Erica Synths LXR-02 firmware;
| built into src/lxr/lxr_gen.s by tools/gen_lxr.py). This file is ours, MIT.
|
| lx_params: va_pre's branch for machine 13 (d2 = track, a2 = trackData):
|   Contour kept for the page (lx_cont), then lxr_params(track, trackData).
| lx_watch: from led_hook once a block, as mi_watch: the four knobs' names
|   follow the selected track's engine group (drums, snare, clap, metal).
| lx_eng_fmt: Contour's value -> the engine's name (mi_fmtswap installs it
|   for set 8), and lx_draw its knob screen: PLAITS' (eng_draw, plaits.inc) -
|   the engine's picture on the left, a pip an engine, the 4-letter name in
|   the big font centred on them. lx_watch also puts the selected track's
|   engine picture on the machine page, as mi_watch does for PLAITS.
| Contour picks the engine as lxr.c's engine_of does: (value >> 8) / 16,
| eight equal steps: KICK TOM PERC SNARE CLAP CL HAT OP HAT CYMBAL.
| ============================================================================
    .globl  lx_params, lx_watch, lx_fillswap, lx_eng_fmt, lx_swap, lx_draw
lx_params:
    mvsw    %a2@(28),%d0          | Contour
    lea.l   lx_cont,%a0
    movel   %d0,%a0@(0,%d2:l:4)
    movel   %a2,%sp@-
    movel   %d2,%sp@-
    jsr     lxr_params
    addql   #8,%sp
    rts

| d0 = Contour's value -> d0 = the engine, 0..7
lx_eidx:
    tstl    %d0
    bpls    1f
    moveq   #0,%d0
1:  lsrl    #8,%d0
    lsrl    #4,%d0
    moveq   #7,%d1
    cmpl    %d1,%d0
    blss    2f
    movel   %d1,%d0
2:  rts

| d0 = an engine -> d0 = its label group: 0 drums, 1 snare, 2 clap, 3 hats,
| 4 cymbal
lx_group:
    cmpil   #3,%d0                | (each branch right after its compare)
    bcss    1f
    beqs    2f
    cmpil   #4,%d0
    beqs    3f
    cmpil   #7,%d0
    beqs    4f
    moveq   #3,%d0                | 5, 6: the hats
    rts
4:  moveq   #4,%d0                | 7: the cymbal
    rts
1:  moveq   #0,%d0                | 0..2: drums
    rts
2:  moveq   #1,%d0                | 3: snare
    rts
3:  moveq   #2,%d0                | 4: clap
    rts

lx_watch:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    cmpil   #MAX_TRK,%d0
    bccs    lw_off
    lea.l   trk_mach,%a0
    moveq   #LX_MACH,%d1
    cmpl    %a0@(0,%d0:l:4),%d1
    bnes    lw_off
    lea.l   lx_cont,%a0
    movel   %a0@(0,%d0:l:4),%d0
    bsr     lx_eidx
    movel   %d0,%sp@-
    bsr     lx_icon               | the machine page: this engine's picture
    movel   %sp@+,%d0
    bsr     lx_group
    cmpl    lx_lab,%d0
    beqs    lw_out
    movel   %d0,lx_lab
    moveq   #8,%d1
    jsr     apply_names
    rts
lw_off:
    moveq   #-1,%d0
    movel   %d0,lx_lab
    bsr     lx_icon               | LXR's own picture back
lw_out:
    rts

| lx_icon(d0 = an engine 0..7, or -1 for LXR's own picture): table B's
| fourteenth entry (+364, its pixels at +16), as mi_icon does PLAITS'.
| Clobbers d1/a0/a1.
lx_icon:
    lea.l   lxr_icon_b_pixels,%a1
    tstl    %d0
    bmis    1f
    lea.l   lx_icons,%a0
    movea.l %a0@(0,%d0:l:4),%a1
1:  movea.l 0x40fe384c,%a0
    movel   0x40fe3850,%d1
    subl    %a0,%d1
    cmpil   #392,%d1
    bnes    2f
    movel   %a1,%a0@(364+16)
2:  rts

| The knob screen while Contour picks the engine: PLAITS' (eng_draw).
lx_draw:
    linkw   %fp,#-60
    moveml  %d2-%d7/%a2-%a5,%sp@
    movel   %fp@(12),%d0
    bsr     lx_eidx
    movel   %d0,%d7
    lea.l   lx_icons,%a0
    movel   %a0,%fp@(-12)
    lea.l   lx_engn,%a0
    movel   %a0,%fp@(-16)
    moveq   #8,%d0
    movel   %d0,%fp@(-20)
    jmp     eng_draw

| lx_swap's names from the group being labelled (lx_lab). Clobbers d0/a0/a1
| only: apply_names still needs d1 (its set) after it.
lx_fillswap:
    movel   lx_lab,%d0
    bpls    1f
    moveq   #0,%d0
1:  lsll    #3,%d0                | 6 longs a group: x 24
    lea.l   lx_labs,%a0
    adda.l  %d0,%a0
    adda.l  %d0,%a0
    adda.l  %d0,%a0
    lea.l   lx_swap,%a1
    movel   %a0@+,%a1@(4)
    movel   %a0@+,%a1@(12)
    movel   %a0@+,%a1@(28+4)
    movel   %a0@+,%a1@(28+12)
    movel   %a0@+,%a1@(56+4)
    movel   %a0@+,%a1@(56+12)
    rts

| (this, value, buf) -> the engine's name, as mi_eng_fmt
lx_eng_fmt:
    movel   %sp@(8),%d0
    bsr     lx_eidx
    lea.l   lx_engn,%a0
    movel   %a0@(0,%d0:l:4),%d0
    movel   %sp@(12),%d1
    movel   %d1,%sp@(4)           | buf
    movel   #0x40124b58,%d1       | "%s"
    movel   %d1,%sp@(8)
    movel   %d0,%sp@(12)          | the name
    jmp     0x40000e6e            | sprintf

    .align 2
| the four knobs' rows, as dx_swap (offset, name, category, short, then the
| stock name, category, short put back); names filled by lx_fillswap
lx_swap:
    .long 2576, s_lxpa, s_lxc, s_lxpas, 0x4012999c, 0x4012996f, 0x401299a7
    .long 3472, s_lxpt, s_lxc, s_lxpts, 0x40129a4d, 0x4012996f, 0x401299b7
    .long 4088, s_lxfm, s_lxc, s_lxfms, 0x40129ab6, 0x4012996f, 0x401299c7
    .long 4144, s_lxe, s_lxc, s_lxes, 0x40129ac2, 0x4012996f, 0x401299d9
    .long -1,0,0,0,0,0,0
| per group: Color, Shape, Sweep - a name and a short label each
lx_labs:
    .long s_lxpa, s_lxpas, s_lxpt, s_lxpts, s_lxfm, s_lxfms       | drums
    .long s_lxnm, s_lxnms, s_lxnt, s_lxnts, s_lxsn, s_lxsns       | snare
    .long s_lxsp, s_lxsps, s_lxto, s_lxtos, s_lxcl, s_lxcls       | clap
    .long s_lxtu, s_lxtus, s_lxno, s_lxnos, s_lxbr, s_lxbrs       | hats
    .long s_lxtu, s_lxtus, s_lxrg, s_lxrgs, s_lxbr, s_lxbrs       | cymbal
lx_engn:
    .long s_lxk, s_lxt, s_lxp, s_lxs, s_lxcp, s_lxch, s_lxoh, s_lxcy
lx_icons:                         | tools/gen_lxr_icons.py
    .long lx_ic_kick, lx_ic_tom, lx_ic_perc, lx_ic_snar
    .long lx_ic_clap, lx_ic_chat, lx_ic_ohat, lx_ic_cymb
lx_cont:   .long 0,0,0,0,0,0      | each track's Contour (lx_params)
lx_lab:    .long -1               | the group labelled (lx_watch)
s_lxc:   .asciz "LXR"
s_lxe:   .asciz "Engine"
s_lxes:  .asciz "ENG"
s_lxpa:  .asciz "Pitch Amt"
s_lxpas: .asciz "PAMT"
s_lxpt:  .asciz "Pitch Dec"
s_lxpts: .asciz "PTIM"
s_lxfm:  .asciz "FM Amount"
s_lxfms: .asciz "FMAM"
s_lxnm:  .asciz "Noise Mix"
s_lxnms: .asciz "NMIX"
s_lxnt:  .asciz "NoiseTone"
s_lxnts: .asciz "NTON"
s_lxsn:  .asciz "Snap"
s_lxsns: .asciz "SNAP"
s_lxsp:  .asciz "Spread"
s_lxsps: .asciz "SPRD"
s_lxto:  .asciz "Tone"
s_lxtos: .asciz "TONE"
s_lxcl:  .asciz "Claps"
s_lxcls: .asciz "CLPS"
s_lxtu:  .asciz "Tune"
s_lxtus: .asciz "TUNE"
s_lxno:  .asciz "Metal"
s_lxnos: .asciz "METL"
s_lxrg:  .asciz "Ring"
s_lxrgs: .asciz "RING"
s_lxbr:  .asciz "Bright"
s_lxbrs: .asciz "BRIT"
| the engines' names: four letters at most, as PLAITS' - they are drawn in
| the big font on the knob screen's right half
s_lxk:   .asciz "KICK"
s_lxt:   .asciz "TOM"
s_lxp:   .asciz "PERC"
s_lxs:   .asciz "SNAR"
s_lxcp:  .asciz "CLAP"
s_lxch:  .asciz "CHAT"
s_lxoh:  .asciz "OHAT"
s_lxcy:  .asciz "CYMB"
    .align 2
lx_ic_kick: .incbin "va_icons/lx_kick_B_34x34.bin"
lx_ic_tom:  .incbin "va_icons/lx_tom_B_34x34.bin"
lx_ic_perc: .incbin "va_icons/lx_perc_B_34x34.bin"
lx_ic_snar: .incbin "va_icons/lx_snar_B_34x34.bin"
lx_ic_clap: .incbin "va_icons/lx_clap_B_34x34.bin"
lx_ic_chat: .incbin "va_icons/lx_chat_B_34x34.bin"
lx_ic_ohat: .incbin "va_icons/lx_ohat_B_34x34.bin"
lx_ic_cymb: .incbin "va_icons/lx_cymb_B_34x34.bin"
