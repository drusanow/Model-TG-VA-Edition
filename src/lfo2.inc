
| ============================================================================
| ======================  LFO 2: a second LFO per track  =====================
|
| See docs/LFO2.md. Press LFO for the LFO menu as always; press it again and
| the same menu edits LFO 2 ("2" under "LFO"); a third press closes it.
|
| STORAGE. A sound has 33 parameter words (k = 0..32, sound +20 + 2k, mirrored
| to the audio thread 66 bytes a track). LFO 1 is k = 1..8. The five words
| k = 28..32 are used by nothing - no parameter claims them, the stock save
| drops them, the default fill writes zero there - so LFO 2 takes them for
| the five values the LFO menu edits:
|     k 28  Speed       (LFO 1: k 1, id 0x1d - the LFO SPEED knob)
|     k 29  Multiplier  (k 2, 0x1e)
|     k 30  Waveform    (k 5, 0x21)
|     k 31  Destination (k 4, 0x20)
|     k 32  Depth       (k 8, 0x24)
| Fade, Start Phase and Trig Mode (LFO Setup, k 3/6/7) have no word left, so
| LFO 2 shares LFO 1's.
|
| UI. No new parameter ids: while LFO 2 is showing (lfo2_on), slotOf
| (0x4005a4e8, param_table[id].long[1], the one place every UI path turns a
| parameter id into its word) answers 28..32 for those five ids. The stock
| menu, its value drawing, the knob, p-lock writes and reads then all work on
| LFO 2 unchanged. lfo2_on is set by the LFO menu's own key handler (a second
| LFO press) and cleared when the menu is built or destroyed, so nothing
| outside the menu ever sees the remap.
|
| AUDIO. The stock engine 0x40091ab2(params, tempo, trig mask) runs all six
| tracks from fixed state at 0x40fde838 (32 bytes a track, 192 in all) and
| reads its eight parameters at params+14 + 66t + 2k, k = 1..8. lfo_run calls
| it twice: first for LFO 2, with LFO 2's state swapped in and a parameter
| block holding LFO 2's words in LFO 1's places, then for LFO 1 as stock
| does. LFO 2's modulation (state +20, for the destination at state +16) is
| added to the real words, clamped as the engine clamps. Running LFO 2
| first lets it modulate LFO 1's own parameters. Same waves, same tempo
| sync, same trigs - it is the stock LFO.
| ============================================================================

LFO2_K0    = 28                  | LFO 2's first word (Speed); then 29..32
LFO_STATE  = 0x40fde838          | the stock engine's state, 6 x 32 bytes
LFO_ENGINE = 0x40091ab2

    .align 2
| ---- the block (replaces `jsr 0x40091ab2` at 0x400597c6) --------------------
| args: sp@(4) = params (words at +14 + 66t + 2k), sp@(8) = tempo, sp@(12) =
| the LFO trig mask. The caller pops them. Keeps d2-d7/a2-a6.
| LFO 2 runs from the words k28..32, LFO 3 and 4 from lfo34_w (Model-TG
| RAM, no p-locks), each through lfo_one; LFO 3 or 4 with no destination on
| any track is not run at all. Then LFO 1, as stock.
lfo_run:
    lea.l   %sp@(-32),%sp
    moveml  %d2-%d5/%a2-%a5,%sp@
    movea.l %sp@(36),%a5          | params
    movel   %sp@(40),%d4          | tempo
    movel   %sp@(44),%d5          | trig mask
    lea.l   %a5@(14+2*LFO2_K0),%a0
    moveq   #66,%d3
    lea.l   lfo2_state,%a4
    bsr     lfo_one               | LFO 2
    lea.l   lfo34_w,%a0
    moveq   #10,%d3
    lea.l   lfo34_state,%a4
    bsr     lfo_one_if            | LFO 3
    lea.l   lfo34_w+60,%a0
    moveq   #10,%d3
    lea.l   lfo34_state+192,%a4
    bsr     lfo_one_if            | LFO 4
    movel   %d5,%sp@-             | LFO 1, as stock
    movel   %d4,%sp@-
    movel   %a5,%sp@-
    jsr     LFO_ENGINE
    lea.l   %sp@(12),%sp
    moveml  %sp@,%d2-%d5/%a2-%a5
    lea.l   %sp@(32),%sp
    rts

| lfo_one_if: lfo_one, unless no track has a destination (a0 = Speed of
| track 0, d3 = the stride: Destination at a0 + 6 + t x d3).
lfo_one_if:
    movea.l %a0,%a1
    moveq   #5,%d2
loi_l:
    tstw    %a1@(6)
    bnes    lfo_one
    adda.l  %d3,%a1
    subql   #1,%d2
    bpls    loi_l
    rts

| One more LFO through the stock engine. a0 = its five words for track 0
| (Speed, Multiplier, Waveform, Destination, Depth), d3 = the stride to the
| next track's, a4 = its 192 bytes of engine state, a5 = params, d4 = tempo,
| d5 = trig mask. Fade, Start Phase and Trig Mode are LFO 1's (k 3/6/7).
| Clobbers d0-d2, a0-a3.
lfo_one:
    lea.l   %a5@(14),%a2          | a2 = the real words, track 0, k = 0
    lea.l   lfo2_buf+14,%a1       | a1 = the block's words, track 0, k = 0
    movea.l %a0,%a3               | a3 = this LFO's five
    moveq   #5,%d2
lo_blk:
    movew   %a3@,%a1@(2)                  | k1 Speed
    movew   %a3@(2),%a1@(4)               | k2 Multiplier
    movew   %a2@(6),%a1@(6)               | k3 Fade       (shared)
    movew   %a3@(6),%a1@(8)               | k4 Dest
    movew   %a3@(4),%a1@(10)              | k5 Waveform
    movew   %a2@(12),%a1@(12)             | k6 Phase      (shared)
    movew   %a2@(14),%a1@(14)             | k7 Trig mode  (shared)
    movew   %a3@(8),%a1@(16)              | k8 Depth
    lea.l   %a2@(66),%a2
    lea.l   %a1@(66),%a1
    adda.l  %d3,%a3
    subql   #1,%d2
    bpls    lo_blk
    movea.l %a4,%a1               | its state in, the engine, the state back
    bsr     lfo_swap
    movel   %d5,%sp@-
    movel   %d4,%sp@-
    pea     lfo2_buf
    jsr     LFO_ENGINE
    lea.l   %sp@(12),%sp
    movea.l %a4,%a1
    bsr     lfo_swap
    | Its modulation onto the real words: for each track, the engine left the
    | destination at state +16 and the amount at +20 (+20 is only current
    | while the destination is 0..32). None (0) and LFO 1's own destination
    | are skipped - the menus keep the LFOs apart, this keeps p-locks and old
    | projects honest too.
    lea.l   %a5@(14),%a2
    movea.l %a4,%a3
    moveq   #5,%d2
lr_ap:
    movel   %a3@(16),%d0          | this LFO's destination
    beqs    lr_next
    moveq   #32,%d1
    cmpl    %d0,%d1
    bcss    lr_next               | not a word: no destination
    mvsb    %a2@(8),%d1           | LFO 1's destination, k4's high byte as the
    cmpl    %d0,%d1               | engine reads it
    beqs    lr_next
    lea.l   %a2@(0,%d0:l:2),%a0
    mvsw    %a0@,%d1
    addl    %a3@(20),%d1
    bpls    lr_lo
    moveq   #0,%d1
lr_lo:
    cmpil   #32512,%d1
    bles    lr_hi
    movel   #32512,%d1
lr_hi:
    movew   %d1,%a0@
lr_next:
    lea.l   %a2@(66),%a2
    lea.l   %a3@(32),%a3
    subql   #1,%d2
    bpls    lr_ap
    rts

| swap the engine's state with a1's 48 longs. Clobbers d0/d1/a0/a1.
lfo_swap:
    lea.l   LFO_STATE,%a0
    moveq   #47,%d1
lsw_l:
    movel   %a0@,%d0
    movel   %a1@,%a0@+
    movel   %d0,%a1@+
    subql   #1,%d1
    bpls    lsw_l
    rts

| ---- slotOf: 0x4005a4e8 (id) -> param_table[id].long[1] ----------------------
| Entered in place of its first two instructions, `moveq #76,%d1 ; movel
| %sp@(4),%d0`, replayed on the stock path. While LFO 2 is showing, the five
| ids the LFO menu and the SPEED knob use answer LFO 2's words.
lfo_slot:
    movel   %sp@(4),%d0
    moveq   #2,%d1                | LFO 3 / 4 showing: no word for the five
    cmpl    lfo_page,%d1
    bhis    ls_12
    lea.l   lfo2_ids,%a0
    moveq   #4,%d1
ls_n:
    cmpl    %a0@+,%d0
    beqs    ls_none
    subql   #1,%d1
    bpls    ls_n
    bras    ls_stock
ls_none:
    moveq   #-1,%d0
    rts
ls_12:
    tstl    lfo2_on
    beqs    ls_stock
    lea.l   lfo2_ids,%a0
    moveq   #LFO2_K0,%d1
ls_l:
    cmpl    %a0@+,%d0
    beqs    ls_ours
    addql   #1,%d1
    cmpil   #LFO2_K0+5,%d1
    bcss    ls_l
ls_stock:
    moveq   #76,%d1
    jmp     0x4005a4ee
ls_ours:
    movel   %d1,%d0
    rts
    .align 2
lfo2_ids:                         | in LFO 2's word order, k 28..32
    .long   0x1d, 0x1e, 0x21, 0x20, 0x24

| ---- the LFO menu: built (jsr at 0x4001c3dc), destroyed (0x400d979a) ---------
| Every menu starts at LFO 1, and nothing is remapped once it has gone.
lfo_menu_new:
    clrl    lfo2_on
    clrl    lfo_ate
    clrl    lfo_page
    clrl    lfo_view
    moveq   #1,%d0
    movel   %d0,lfo_live
    jmp     0x40025798            | the constructor, with the caller's args
lfo_menu_dtor:
    clrl    lfo2_on
    clrl    lfo_ate
    clrl    lfo_live
    clrl    lfo_page
    clrl    lfo_view
    movel   %a2,%sp@-             | the two instructions the jump replaced
    movel   #0x401016dc,%d0
    jmp     0x400d97a2

| ---- the LFO menu's key handler (View vtable 0x401016dc, slot +8) ------------
| (view, event). Stock (0x40024e86 -> 0x40024e0c): the LFO key's press is
| taken, its release closes the menu - except the release of the press that
| opened it. Here a fresh LFO press moves LFO 1 -> 2 -> 3 -> 4 (redrawn, its
| release swallowed); with LFO 4 showing the stock handling closes it. FUNC +
| LFO (LFO Setup) and every other key are stock. Returns d0 = 1 when the key
| was ours.
lfo_key_hook:
    movel   %sp@(8),%sp@-
    jsr     0x4007240c            | the key code
    addql   #4,%sp
    moveq   #8,%d1                | LFO
    cmpl    %d0,%d1
    bnes    lk_stock
    movel   %sp@(8),%sp@-
    jsr     0x40072490            | FUNC held
    addql   #4,%sp
    tstb    %d0
    bnes    lk_stock
    movel   %sp@(8),%sp@-
    jsr     0x40072470            | 1: a release
    addql   #4,%sp
    tstb    %d0
    beqs    lk_press
    tstl    lfo_ate               | the release of the press that switched
    beqs    lk_stock
    clrl    lfo_ate
    bras    lk_ours
lk_press:
    movea.l %sp@(8),%a0
    btst    #3,%a0@(19)           | auto-repeat: not a fresh press
    bnes    lk_ours
    movel   lfo_page,%d0
    moveq   #3,%d1
    cmpl    %d0,%d1
    beqs    lk_stock              | LFO 4 showing: stock, which closes on release
    addql   #1,%d0
    movel   %d0,lfo_page          | the next LFO
    clrl    lfo2_on
    moveq   #1,%d1
    cmpl    %d0,%d1
    bnes    lk_n2
    movel   %d1,lfo2_on           | LFO 2: its words (slotOf)
lk_n2:
    moveq   #1,%d0
    movel   %d0,lfo_ate
    movel   %sp@(4),%d0
    movel   %d0,lfo_view          | for lfo34_set's redraws
    movel   %d0,%sp@-             | redraw: the values and the number
    jsr     0x40076082
    addql   #4,%sp
lk_ours:
    moveq   #1,%d0
    rts
lk_stock:
    jmp     0x40024e86

| ---- the LFO menu's draw (View vtable 0x401016dc, slot +0x10) ---------------
| (view, ctx). The stock menu draw (0x400426fa), then "2" where a menu's
| second title line goes (MenuView 0x400423e4 draws it at x 32, y 34, the
| title's font, centred).
lfo_render_hook:
    movel   %sp@(8),%sp@-
    movel   %sp@(8),%sp@-
    jsr     0x400426fa
    addql   #8,%sp
    movel   lfo_page,%d0
    beqs    lrh_out
    lea.l   str_lfo2,%a0          | "2" / "3" / "4"
    subql   #1,%d0
    addl    %d0,%d0
    adda.l  %d0,%a0
    movel   %a0,%sp@-
    movel   %sp@(12),%sp@-        | ctx
    bsr     lfo_draw_num
    addql   #8,%sp
lrh_out:
    rts

| lfo_draw_num(ctx, str): str on the title panel's second line. The draw ends
| by inverting the 64x64 title panel, so invert, write, invert back. Keeps
| d2-d7/a2-a6.
lfo_draw_num:
    movel   %d2,%sp@-
    movel   %sp@(8),%d2           | ctx
    bsr     lrh_inv
    movel   %sp@(12),%sp@-        | the text
    pea     0x40124b58            | "%s"
    pea     2                     | centred, as the title
    pea     0x22
    pea     0x20
    pea     0x40ea14d4            | the title's font
    movel   %d2,%sp@-
    jsr     0x40071a04
    lea.l   %sp@(28),%sp
    bsr     lrh_inv
    movel   %sp@+,%d2
    rts
lrh_inv:
    pea     -1                    | invert
    pea     63
    pea     63
    clrl    %sp@-
    clrl    %sp@-
    movel   %d2,%sp@-
    jsr     0x40070dea
    lea.l   %sp@(24),%sp
    rts

| ---- DEST: never another LFO's destination ----------------------------------
| From fine_hook (track-handle slot 0x14, 0x4000c264, "current + delta") for
| id 0x20 while an LFO menu is up: (this, id, current, delta). The stock step
| walks the list of valid destinations; when it lands on one another LFO
| of this track uses (None aside), step on the same way - up to four times,
| one past every other LFO - and if it gets stuck (the end of the list) or
| runs out, stay where it was.
lfo_dest_step:
    tstl    lfo_live
    bnes    lds_go
    jmp     0x4000c264
lds_go:
    lea.l   %sp@(-16),%sp
    moveml  %d2-%d5,%sp@
    movel   %sp@(20),%a0          | this
    bsr     lfo_dests             | lfo_dbuf: the other LFOs' destinations
    movel   %sp@(28),%d2          | c = current
    moveq   #4,%d3
lds_l:
    movel   %sp@(32),%sp@-        | delta
    movel   %d2,%sp@-             | c
    movel   %sp@(32),%sp@-        | id
    movel   %sp@(32),%sp@-        | this
    jsr     0x4000c264
    lea.l   %sp@(16),%sp
    cmpl    %d2,%d0
    beqs    lds_keep              | no further: stay
    movel   %d0,%d2
    movel   %d0,%d1
    asrl    #8,%d1
    bsr     lfo_taken
    beqs    lds_out               | free
    subql   #1,%d3
    bnes    lds_l
lds_keep:
    movel   %sp@(28),%d2
lds_out:
    movel   %d2,%d0
    moveml  %sp@,%d2-%d5
    lea.l   %sp@(16),%sp
    rts

| d1 = a destination -> Z clear when another LFO of the track uses it (from
| lfo_dbuf). None (0) is never taken. Keeps d1; clobbers d0/a0.
lfo_taken:
    andil   #0xff,%d1
    beqs    lt_no
    lea.l   lfo_dbuf,%a0
    moveq   #3,%d0
lt_l:
    cmpb    %a0@+,%d1
    beqs    lt_yes
    subql   #1,%d0
    bpls    lt_l
lt_no:
    moveq   #0,%d0                | Z set
    rts
lt_yes:
    moveq   #1,%d0                | Z clear
    rts

| a0 = a track handle -> lfo_dbuf = the four LFOs' destinations for the
| selected track, the one showing (lfo_page) zeroed: LFO 1's k4 and LFO 2's
| k31 high bytes from the sound (as fine_hook finds it), LFO 3's and 4's
| from lfo34_w. Clobbers d0/d1/a0/a1.
lfo_dests:
    lea.l   lfo_dbuf,%a1
    clrl    %a1@
    movel   %a0@(4),%d0
    beqs    ld_34
    movea.l %d0,%a0
    movea.l %a0@,%a1
    movea.l %a1@(40),%a1          | vtable[40]: the sound
    movel   %d0,%sp@-
    jsr     %a1@
    addql   #4,%sp
    tstl    %d0
    beqs    ld_34
    movea.l %d0,%a0
    lea.l   lfo_dbuf,%a1
    moveb   %a0@(20+2*4),%a1@             | LFO 1
    moveb   %a0@(20+2*(LFO2_K0+3)),%a1@(1) | LFO 2
ld_34:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    cmpil   #MAX_TRK,%d0
    bccs    ld_pg
    mulu.w  #10,%d0
    lea.l   lfo34_w+6,%a0         | the Destination words
    adda.l  %d0,%a0
    lea.l   lfo_dbuf,%a1
    moveb   %a0@,%a1@(2)                  | LFO 3
    moveb   %a0@(60),%a1@(3)              | LFO 4
ld_pg:
    movel   lfo_page,%d0
    lea.l   lfo_dbuf,%a1
    clrb    %a1@(0,%d0:l)         | the one being edited is not "another"
    rts

| ============== LFO 3 and LFO 4: the stock LFO menu, two more pages ==========
| A sound has no words left for them, so their settings live in lfo34_w (per
| track, in RAM: no p-locks, not saved). The stock LFO menu shows them as it
| shows LFO 1 and 2 - same rows, icon, values, DEST list, SPEED knob - because
| every one of its edits goes through the track handle: vtable +28 reads a
| parameter, +32 (0x4000b7d6) adjusts it by reading (+28), stepping (+0x14,
| fine_hook) and storing (0x4000b274 -> +92, 0x4000ba7a). With LFO 3 or 4
| showing, lfo34_get / lfo34_set answer +28 / +92 for the five LFO ids from
| lfo34_w instead of the sound, and ask the menu to redraw (its own redraw
| comes from the sound's observers, which never hear of lfo34_w). slotOf
| answers -1 for those ids meanwhile, and a turn with a step held (the p-lock
| editor, 0x40025e10) is ignored, so nothing can reach LFO 1's words.

| (handle, id) -> d0 = the value (vtable +28, in place of 0x4000ae20)
lfo34_get:
    movel   %sp@(8),%d0
    bsr     l34_ours
    beqs    l34g_stock
    mvsw    %a0@,%d0
    rts
l34g_stock:
    jmp     0x4000ae20

| (handle, id, value, ...) (vtable +92, in place of 0x4000ba7a)
lfo34_set:
    movel   %sp@(8),%d0
    bsr     l34_ours
    beqs    l34s_stock
    movew   %sp@(14),%a0@         | the value's low word
    movel   lfo_view,%d0
    beqs    l34s_out
    movel   %d0,%sp@-
    jsr     0x40076082            | redraw the menu
    addql   #4,%sp
l34s_out:
    moveq   #0,%d0
    rts
l34s_stock:
    jmp     0x4000ba7a

| d0 = a parameter id -> Z clear and a0 = its word in lfo34_w (the LFO
| showing, the selected track) when LFO 3 or 4 shows and it is one of the
| five LFO ids; Z set otherwise. Clobbers d0/d1/a0/a1.
l34_ours:
    moveq   #2,%d1
    cmpl    lfo_page,%d1
    bhis    l34o_no               | LFO 1 or 2
    lea.l   lfo2_ids,%a0          | Speed, Multiplier, Waveform, Dest, Depth:
    moveq   #0,%d1                | lfo34_w's own order
l34o_l:
    cmpl    %a0@+,%d0
    beqs    l34o_yes
    addql   #1,%d1
    cmpil   #5,%d1
    bcss    l34o_l
l34o_no:
    moveq   #0,%d0                | Z set
    rts
l34o_yes:
    movel   %d1,%sp@-
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    addql   #8,%sp
    cmpil   #MAX_TRK,%d0
    bcss    l34o_t
    moveq   #0,%d0
l34o_t:
    mulu.w  #10,%d0
    movel   %sp@+,%d1
    addl    %d1,%d0
    addl    %d1,%d0               | + the word x 2
    lea.l   lfo34_w,%a0
    adda.l  %d0,%a0
    moveq   #3,%d0
    cmpl    lfo_page,%d0
    bnes    l34o_p
    lea.l   %a0@(60),%a0          | LFO 4
l34o_p:
    moveq   #1,%d0                | Z clear
    rts

| the p-lock editor (0x40025e10, a turn with a step held): not for LFO 3/4.
| Replaces `lea %sp@(-16),%sp ; movel %a2,%sp@-`.
lfo34_lock_gate:
    moveq   #2,%d0
    cmpl    lfo_page,%d0
    bhis    llg_go
    rts
llg_go:
    lea.l   %sp@(-16),%sp
    movel   %a2,%sp@-
    jmp     0x40025e16

| ---- LFO 2's defaults, after the stock default fill (0x40061866) -------------
| (sound, ?, group): it writes every word a parameter claims and zero for the
| rest - LFO 2's included. Put LFO 1's own defaults in LFO 2's words. Its
| prologue `lea %sp@(-36),%sp ; moveml %d2-%d3/%a2-%a5,%sp@` is replayed
| with our epilogue pushed as the return address, as snd_save1_hook does.
lfo_init_hook:
    movel   %sp@(12),%sp@-
    movel   %sp@(12),%sp@-
    movel   %sp@(12),%sp@-
    pea     lfo_init_post
    lea.l   %sp@(-36),%sp
    moveml  %d2-%d3/%a2-%a5,%sp@
    jmp     0x4006186e
lfo_init_post:
    movel   %sp@,%d1              | the sound
    beqs    lip_out
    movea.l %d1,%a0
    bsr     lfo2_defaults
lip_out:
    lea.l   %sp@(12),%sp
    rts

| a0 = a sound: LFO 2's words to LFO 1's defaults. Keeps everything but a1.
lfo2_defaults:
    lea.l   lfo2_dflt,%a1
    movew   %a1@+,%a0@(20+2*LFO2_K0)      | Speed
    movew   %a1@+,%a0@(20+2*LFO2_K0+2)    | Multiplier
    movew   %a1@+,%a0@(20+2*LFO2_K0+4)    | Waveform
    movew   %a1@+,%a0@(20+2*LFO2_K0+6)    | Destination
    movew   %a1@,%a0@(20+2*LFO2_K0+8)     | Depth
    rts
    .align 2
lfo2_dflt:                        | param_table's defaults for 0x1d,0x1e,0x21,0x20,0x24
    .word   28672, 1024, 0, 0, 16384

| ---- LFO 2 in the sound record: storage +88 (slots 30/31) --------------------
| The save memsets +28..+91 and stores 23 slots; Model-TG uses slots 24..29
| (+76..+87); +88..+91 is the last free space, so LFO 2 packs into it:
|   bits 31..30 tag (10)   29..22 Speed >> 7   21..17 Multiplier >> 8
|   16..14 Waveform >> 8   13..8 Destination   7..0 Depth >> 7
| Speed and Depth keep half steps (their values are whole steps << 8 unless
| fine-tuned); the rest are exact. A record without the tag (every older
| one) loads LFO 2's defaults.
| lfo2_pack (a1 = sound, a0 = storage) / lfo2_unpack (a0 = storage, a1 =
| sound): keep d0 and the address registers; clobber d1.
lfo2_pack:
    movel   %d2,%sp@-
    movel   %d0,%sp@-
    move.l  #0x80000000,%d2
    mvzw    %a1@(20+2*LFO2_K0),%d0        | Speed
    lsrl    #7,%d0
    andil   #0xff,%d0
    moveq   #22,%d1
    lsll    %d1,%d0
    orl     %d0,%d2
    mvzw    %a1@(20+2*LFO2_K0+2),%d0      | Multiplier
    lsrl    #8,%d0
    andil   #0x1f,%d0
    moveq   #17,%d1
    lsll    %d1,%d0
    orl     %d0,%d2
    mvzw    %a1@(20+2*LFO2_K0+4),%d0      | Waveform
    lsrl    #8,%d0
    andil   #7,%d0
    moveq   #14,%d1
    lsll    %d1,%d0
    orl     %d0,%d2
    mvzw    %a1@(20+2*LFO2_K0+6),%d0      | Destination
    lsrl    #8,%d0
    andil   #0x3f,%d0
    lsll    #8,%d0
    orl     %d0,%d2
    mvzw    %a1@(20+2*LFO2_K0+8),%d0      | Depth
    lsrl    #7,%d0
    andil   #0xff,%d0
    orl     %d0,%d2
    movel   %d2,%a0@(88)
    movel   %sp@+,%d0
    movel   %sp@+,%d2
    rts

lfo2_unpack:
    movel   %d2,%sp@-
    movel   %d0,%sp@-
    movel   %a0@(88),%d2
    movel   %d2,%d0
    moveq   #30,%d1
    lsrl    %d1,%d0
    cmpil   #2,%d0
    beqs    lu_go
    movel   %a0,%sp@-             | no LFO 2 in this record: its defaults
    movea.l %a1,%a0
    movel   %a1,%sp@-
    bsr     lfo2_defaults
    movea.l %sp@+,%a1
    movea.l %sp@+,%a0
    bras    lu_out
lu_go:
    movel   %d2,%d0               | Speed
    moveq   #22,%d1
    lsrl    %d1,%d0
    andil   #0xff,%d0
    lsll    #7,%d0
    movew   %d0,%a1@(20+2*LFO2_K0)
    movel   %d2,%d0               | Multiplier
    moveq   #17,%d1
    lsrl    %d1,%d0
    andil   #0x1f,%d0
    lsll    #8,%d0
    movew   %d0,%a1@(20+2*LFO2_K0+2)
    movel   %d2,%d0               | Waveform
    moveq   #14,%d1
    lsrl    %d1,%d0
    andil   #7,%d0
    lsll    #8,%d0
    movew   %d0,%a1@(20+2*LFO2_K0+4)
    movel   %d2,%d0               | Destination
    lsrl    #8,%d0
    andil   #0x3f,%d0
    lsll    #8,%d0
    movew   %d0,%a1@(20+2*LFO2_K0+6)
    movel   %d2,%d0               | Depth
    andil   #0xff,%d0
    lsll    #7,%d0
    movew   %d0,%a1@(20+2*LFO2_K0+8)
lu_out:
    movel   %sp@+,%d0
    movel   %sp@+,%d2
    rts

| ---- p-locks in saved patterns: words past 22 ------------------------------
| The pattern save (0x4005b960) writes one row per locked word and track:
| the word's slot id, the track, 64 values. It takes the slot from the
| sound save's 23-entry map (0x4010ee4c) with no bound, so a word past 22
| read the next table's entries and was saved as slot 0..9 - LFO 2's Speed
| came back as a lock on LFO 1's Waveform, its Depth on the machine. Words
| 23..25 (Attack, Filter, Resonance) and 28..32 (LFO 2) are given slots
| 24..31 instead, which the loader's row check (slot <= 32) accepts and its
| slot -> word map (0x4005aa1a, slots <= 23) is taught below; 26/27 are not
| parameters and are never locked, but get 0xff, which the loader skips.
| lk_slot_save replaces `moveb %a5@(3,%d4:l),%a3@ ; moveb %d2,%a3@(1)`
| (a5 = the map, d4 = word x 4, a3 = the row, d2 = the track). Keeps all.
lk_slot_save:
    movel   %d4,%sp@-
    lsrl    #2,%d4                | the word
    cmpil   #22,%d4
    bhis    lks_ext
    moveb   %a5@(3,%d4:l:4),%a3@  | the stock map, exactly as before
    bras    lks_tr
lks_ext:
    subil   #23,%d4
    cmpil   #9,%d4
    bhis    lks_bad
    movel   %a0,%sp@-
    lea.l   lk_ext_slot,%a0
    moveb   %a0@(0,%d4:l),%a3@
    movea.l %sp@+,%a0
    bras    lks_tr
lks_bad:
    moveb   #-1,%a3@              | 0xff: the loader skips the row
lks_tr:
    moveb   %d2,%a3@(1)
    movel   %sp@+,%d4
    rts

| lk_slot_load replaces 0x4005aa1a's first three instructions, `movel
| %d2,%sp@- ; moveq #6,%d2 ; movel %sp@(8),%d1`: (track, slot) -> word, or 0.
lk_slot_load:
    movel   %sp@(4),%d0           | track
    moveq   #5,%d1
    cmpl    %d0,%d1
    bcss    lkl_stock             | not a track (6 is the FX row): stock
    movel   %sp@(8),%d0           | slot
    subil   #24,%d0
    moveq   #7,%d1
    cmpl    %d0,%d1
    bcss    lkl_stock             | not one of ours
    lea.l   lk_ext_word,%a0
    moveb   %a0@(0,%d0:l),%d0
    andil   #0xff,%d0
    rts
lkl_stock:
    movel   %d2,%sp@-
    moveq   #6,%d2
    movel   %sp@(8),%d1
    jmp     0x4005aa22

lk_ext_slot:                      | words 23..32 -> slot
    .byte   24, 25, 26, 0xff, 0xff, 27, 28, 29, 30, 31
lk_ext_word:                      | slots 24..31 -> word
    .byte   23, 24, 25, 28, 29, 30, 31, 32
str_lfo2:
    .asciz  "2"                   | 2 bytes apart: "2", "3", "4"
    .asciz  "3"
    .asciz  "4"

| ---- state ------------------------------------------------------------------
    .balign 4
lfo2_on:    .long 0               | the LFO menu shows LFO 2: remap ids
lfo_ate:    .long 0               | we took an LFO press: take its release
lfo_live:   .long 0               | an LFO menu (stock, or LFO 3/4's) is up
lfo_page:   .long 0               | the LFO it shows: 0..3 = LFO 1..4
lfo_view:   .long 0               | the LFO menu's View, for redraws
lfo_dbuf:   .long 0               | lfo_dests: the four destinations
lfo2_state: .space 6*32           | the engine's state for LFO 2
lfo34_state: .space 2*6*32        | ... for LFO 3 and LFO 4
lfo2_buf:   .space 14+6*66+2      | the parameter block lfo_one builds
| LFO 3 and 4's settings: [LFO][track] x {Speed, Multiplier, Waveform,
| Destination, Depth}, words as the sound's (LFO 1's defaults to start)
lfo34_w:    .rept 12
            .word 28672, 1024, 0, 0, 16384
            .endr

    .align 2
