
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
lfo_run:
    lea.l   %sp@(-32),%sp
    moveml  %d2-%d5/%a2-%a5,%sp@
    movea.l %sp@(36),%a2
    lea.l   %a2@(14),%a2          | a2 = the real words, track 0, k = 0
    | 1. LFO 2's parameter block: k 1..8 as the engine reads them
    lea.l   lfo2_buf+14,%a3       | a3 = the block's words, track 0, k = 0
    moveq   #5,%d2
lr_blk:
    movew   %a2@(2*LFO2_K0),%a3@(2)       | k1 Speed      <- k28
    movew   %a2@(2*LFO2_K0+2),%a3@(4)     | k2 Multiplier <- k29
    movew   %a2@(6),%a3@(6)               | k3 Fade       (shared)
    movew   %a2@(2*LFO2_K0+6),%a3@(8)     | k4 Dest       <- k31
    movew   %a2@(2*LFO2_K0+4),%a3@(10)    | k5 Waveform   <- k30
    movew   %a2@(12),%a3@(12)             | k6 Phase      (shared)
    movew   %a2@(14),%a3@(14)             | k7 Trig mode  (shared)
    movew   %a2@(2*LFO2_K0+8),%a3@(16)    | k8 Depth      <- k32
    lea.l   %a2@(66),%a2
    lea.l   %a3@(66),%a3
    subql   #1,%d2
    bpls    lr_blk
    | 2. LFO 2's state in, the engine, the state back
    bsr     lfo_swap
    movel   %sp@(44),%sp@-        | trig mask
    movel   %sp@(44),%sp@-        | tempo
    pea     lfo2_buf              | the block
    jsr     LFO_ENGINE
    lea.l   %sp@(12),%sp
    bsr     lfo_swap
    | 3. LFO 2's modulation onto the real words: for each track, the engine
    | left the destination at state +16 and the amount at +20 (+20 is only
    | current while the destination is 0..32). None (0) and LFO 1's own
    | destination are skipped - the menus keep the two apart, this keeps
    | p-locks and old projects honest too.
    movea.l %sp@(36),%a2
    lea.l   %a2@(14),%a2
    lea.l   lfo2_state,%a3
    moveq   #5,%d2
lr_ap:
    movel   %a3@(16),%d0          | LFO 2's destination
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
    | 4. LFO 1, as stock
    movel   %sp@(44),%sp@-
    movel   %sp@(44),%sp@-
    movel   %sp@(44),%sp@-
    jsr     LFO_ENGINE
    lea.l   %sp@(12),%sp
    moveml  %sp@,%d2-%d5/%a2-%a5
    lea.l   %sp@(32),%sp
    rts

| swap the engine's state with LFO 2's: 48 longs. Clobbers d0/d1/a0/a1.
lfo_swap:
    lea.l   LFO_STATE,%a0
    lea.l   lfo2_state,%a1
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
    moveq   #1,%d0
    movel   %d0,lfo_live
    jmp     0x40025798            | the constructor, with the caller's args
lfo_menu_dtor:
    clrl    lfo2_on
    clrl    lfo_ate
    clrl    lfo_live
    movel   %a2,%sp@-             | the two instructions the jump replaced
    movel   #0x401016dc,%d0
    jmp     0x400d97a2

| ---- the LFO menu's key handler (View vtable 0x401016dc, slot +8) ------------
| (view, event). Stock (0x40024e86 -> 0x40024e0c): the LFO key's press is
| taken, its release closes the menu - except the release of the press that
| opened it. Here, while LFO 1 shows, a fresh LFO press switches to LFO 2 and
| its release is swallowed; with LFO 2 showing the stock handling closes it.
| FUNC + LFO (LFO Setup) and every other key are stock. Returns d0 = 1 when
| the key was ours.
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
    tstl    lfo2_on
    bnes    lk_stock              | LFO 2 showing: stock, which closes on release
    movea.l %sp@(8),%a0
    btst    #3,%a0@(19)           | auto-repeat: not a fresh press
    bnes    lk_ours
    moveq   #1,%d0
    movel   %d0,lfo2_on
    movel   %d0,lfo_ate
    movel   %sp@(4),%sp@-         | redraw: the values and the "2"
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
| title's font, centred). The draw ends by inverting the 64x64 title panel,
| so invert, write, invert back.
lfo_render_hook:
    movel   %sp@(8),%sp@-
    movel   %sp@(8),%sp@-
    jsr     0x400426fa
    addql   #8,%sp
    tstl    lfo2_on
    beqs    lrh_out
    movel   %d2,%sp@-
    movel   %sp@(12),%d2          | ctx
    bsr     lrh_inv
    pea     str_lfo2
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
lrh_out:
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

| ---- DEST: never the other LFO's destination --------------------------------
| From fine_hook (track-handle slot 0x14, 0x4000c264, "current + delta") for
| id 0x20 while an LFO menu is up: (this, id, current, delta). The stock step
| walks the list of valid destinations; when it lands on the other LFO's
| (not None), take one more step the same way, and if that is it again (the
| end of the list) stay where it was.
lfo_dest_step:
    tstl    lfo_live
    bnes    lds_go
    jmp     0x4000c264
lds_go:
    lea.l   %sp@(-12),%sp
    moveml  %d2-%d4,%sp@
    movel   %sp@(16),%a0          | this
    bsr     lfo_other_dest
    movel   %d0,%d4               | d4 = the other LFO's destination, 0 = none
    movel   %sp@(28),%sp@-        | delta
    movel   %sp@(28),%sp@-        | current
    movel   %sp@(28),%sp@-        | id
    movel   %sp@(28),%sp@-        | this
    jsr     0x4000c264
    lea.l   %sp@(16),%sp
    movel   %d0,%d2
    tstl    %d4
    beqs    lds_out
    movel   %d2,%d1
    asrl    #8,%d1
    cmpl    %d4,%d1
    bnes    lds_out
    movel   %sp@(28),%sp@-        | once more, from there
    movel   %d2,%sp@-
    movel   %sp@(28),%sp@-
    movel   %sp@(28),%sp@-
    jsr     0x4000c264
    lea.l   %sp@(16),%sp
    movel   %d0,%d2
    asrl    #8,%d0
    cmpl    %d4,%d0
    bnes    lds_out
    movel   %sp@(24),%d2          | nowhere else to go: unchanged
lds_out:
    movel   %d2,%d0
    moveml  %sp@,%d2-%d4
    lea.l   %sp@(12),%sp
    rts

| a0 = a track handle -> d0 = the destination of the LFO NOT showing (LFO 1's
| k4 while LFO 2 shows, else LFO 2's k31), 0 for none or no sound. As
| fine_hook finds the sound. Clobbers d1/a0/a1.
lfo_other_dest:
    movel   %a0@(4),%d0
    beqs    lod_none
    movea.l %d0,%a0
    movea.l %a0@,%a1
    movea.l %a1@(40),%a1          | vtable[40]: the sound
    movel   %d0,%sp@-
    jsr     %a1@
    addql   #4,%sp
    tstl    %d0
    beqs    lod_none
    movea.l %d0,%a0
    moveq   #0,%d0
    tstl    lfo2_on
    beqs    lod_2
    moveb   %a0@(20+2*4),%d0      | LFO 1's destination
    rts
lod_2:
    moveb   %a0@(20+2*(LFO2_K0+3)),%d0 | LFO 2's
    rts
lod_none:
    moveq   #0,%d0
    rts

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
    .asciz  "2"

| ---- state ------------------------------------------------------------------
    .balign 4
lfo2_on:    .long 0               | the LFO menu shows LFO 2: remap ids
lfo_ate:    .long 0               | we took an LFO press: take its release
lfo_live:   .long 0               | an LFO menu exists
lfo2_state: .space 6*32           | the engine's state for LFO 2
lfo2_buf:   .space 14+6*66+2      | LFO 2's parameter block
    .align 2
