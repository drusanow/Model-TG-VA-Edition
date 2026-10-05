| ============================================================================
| =================  GEN: random / Euclidean trig generator  =================
|
| See docs/GEN.md. SETTINGS + TRACK opens a list menu (the same firmware list
| menu as Resample and the mode options, built by lm_build) for the track that
| is selected. GEN's press replaces that track's trigs, over its length, with
|   RND - each step a trig with probability DNS %
|   EUC - HIT trigs spread as evenly as the steps allow (a Euclidean rhythm),
|         turned ROT steps later
| and, with NOT on, gives every new trig a random note of the scale SCL in
| the key KEY, from OCT for RNG octaves (OCT 4, KEY C = note 60, the track's
| default note). With NOT off the trigs play the track's default note.
| With VEL on each new trig gets a random velocity from VMN to VMX.
|
| Pattern edits go through the stock routines the grid uses, on the track's
| pattern object (0x4000cfcc(0x4000f208(ui), track)), so the pattern's
| observers (grid lights, the audio copy, saving) all hear of them:
|   0x40016402(obj)              its length
|   0x40017b48(obj, step, on)    a trig on (bit 0) or off; clears the
|                                trigless / note-trig overrides as the grid does
|   0x40016642(obj, step, note)  the trig's note, -1 for the track's default
|   0x400166c2(obj, step, vel)   the trig's velocity, -1 for the track's
| P-locks are left as they are. The settings are shared by every track and
| kept until power-off (like Resample's).
| ============================================================================

    .align 2
| ---- SETTINGS + TRACK (from kh_rs, d0 = KEY_TRACK, a0 = the event) ---------
| As SETTINGS + RECORD: the press opens the menu and, like the release, is
| swallowed, so the stock TRACK action never sees it.
kh_gen:
    movel   %a0@(16),%d1
    lea.l   kh_eaten6,%a1
    btst    #0,%d1
    bnes    kg_down
    tstl    %a1@                  | a release: ours only if we took the press
    beqs    kg_out
    clrl    %a1@
    bras    kg_eat
kg_down:
    tstl    %a1@
    bnes    kg_eat                | a later read of a press already taken
    btst    #3,%d1
    bnes    kg_out
    movel   %a0,%sp@-
    movel   %d0,%sp@-
    jsr     gen_menu_open
    movel   %sp@+,%d0
    movea.l %sp@+,%a0
    lea.l   mm_opened,%a1
    tstl    %a1@
    beqs    kg_out
    lea.l   kh_eaten6,%a1
    moveq   #1,%d1
    movel   %d1,%a1@
    movel   %d1,mod_used
    movel   %a0@(16),%d1
    oril    #8,%d1
    movel   %d1,%a0@(16)
kg_eat:
    moveq   #0,%d0                | as kh_eat: this and later reads see no key
kg_out:
    rts

| ---- the menu: as rs_menu_open, for the selected track ----------------------
gen_menu_open:
    linkw   %fp,#-80
    lea.l   %sp@(-28),%sp
    moveml  %d2-%d4/%a2-%a5,%sp@
    lea.l   mm_opened,%a0
    clrl    %a0@
    moveq   #0,%d0
    movew   %sr,%d0
    andil   #0x0700,%d0
    bnes    gmo_done
    tstl    0x40fe4178
    beqs    gmo_done
    jsr     gm_live               | a list menu already up: no second copy
    tstl    %d0
    beqs    gmo_new
    jmp     gm_opened
gmo_new:
    jsr     0x400cf866            | the selected track
    movel   %d0,%sp@-
    jsr     0x4000eb90
    movel   %d0,%sp@-
    jsr     0x40012412
    lea.l   %sp@(8),%sp
    cmpil   #MAX_TRK,%d0
    bccs    gmo_done
    lea.l   gm_track,%a0
    movel   %d0,%a0@
    lea.l   gen_desc,%a0
    lea.l   gm_cur,%a1
    movel   %a0,%a1@
    jmp     lm_build              | builds, presents, and leaves through gm_done
gmo_done:
    jmp     gm_done               | gm_done unwinds this same frame

| ---- rows: labels ----
gen_lab_mod:
    lea.l   gen_s_mod,%a1
    jmp     gm_lab
gen_lab_dns:
    lea.l   gen_s_dns,%a1
    jmp     gm_lab
gen_lab_hit:
    lea.l   gen_s_hit,%a1
    jmp     gm_lab
gen_lab_rot:
    lea.l   gen_s_rot,%a1
    jmp     gm_lab
gen_lab_not:
    lea.l   gen_s_not,%a1
    jmp     gm_lab
gen_lab_key:
    lea.l   gen_s_key,%a1
    jmp     gm_lab
gen_lab_scl:
    lea.l   gen_s_scl,%a1
    jmp     gm_lab
gen_lab_oct:
    lea.l   gen_s_oct,%a1
    jmp     gm_lab
gen_lab_rng:
    lea.l   gen_s_rng,%a1
    jmp     gm_lab
gen_lab_vel:
    lea.l   gen_s_vel,%a1
    jmp     gm_lab
gen_lab_vmn:
    lea.l   gen_s_vmn,%a1
    jmp     gm_lab
gen_lab_vmx:
    lea.l   gen_s_vmx,%a1
    jmp     gm_lab
gen_lab_gen:
    lea.l   gen_s_gen,%a1
    jmp     gm_lab

| ---- rows: values (a0 = &value, a1 = its names, as gm_val takes them) ----
gen_val_mod:
    lea.l   gen_c_mod,%a0
    lea.l   gen_n_mod,%a1
    jmp     gm_val
gen_val_not:
    lea.l   gen_c_not,%a0
    lea.l   gm_n_frz,%a1          | OFF / ON
    jmp     gm_val
gen_val_key:
    lea.l   gen_c_key,%a0
    lea.l   gen_n_key,%a1
    jmp     gm_val
gen_val_scl:
    lea.l   gen_c_scl,%a0
    lea.l   gen_n_scl,%a1
    jmp     gm_val
gen_val_vel:
    lea.l   gen_c_vel,%a0
    lea.l   gm_n_frz,%a1          | OFF / ON
    jmp     gm_val
gen_val_gen:
    lea.l   gen_zero,%a0
    lea.l   gen_n_go,%a1
    jmp     gm_val
gen_val_dns:
    movel   gen_c_dns,%d0
    bras    gen_vnum
gen_val_hit:
    movel   gen_c_hit,%d0
    addql   #1,%d0                | 1..64
    bras    gen_vnum
gen_val_rot:
    movel   gen_c_rot,%d0
    bras    gen_vnum
gen_val_oct:
    movel   gen_c_oct,%d0
    bras    gen_vnum
gen_val_vmn:
    movel   gen_c_vmn,%d0
    addql   #1,%d0                | 1..127
    bras    gen_vnum
gen_val_vmx:
    movel   gen_c_vmx,%d0
    addql   #1,%d0
    bras    gen_vnum
gen_val_rng:
    movel   gen_c_rng,%d0
    addql   #1,%d0                | 1..4
| d0 = a number: shown as text, as REC's row shows its count
gen_vnum:
    lea.l   %sp@(-16),%sp
    moveml  %d0-%d1/%a0-%a1,%sp@
    movel   %d0,gen_args
    pea     gen_args
    pea     gen_f_num
    pea     gen_vbuf
    jsr     0x400005cc            | vsprintf(buf, fmt, args)
    lea.l   %sp@(12),%sp
    moveml  %sp@,%d0-%d1/%a0-%a1
    lea.l   %sp@(16),%sp
    lea.l   gen_zero,%a0
    lea.l   gen_n_num,%a1
    jmp     gm_val

| ---- rows: turns (a1 = &setting, d1 = how many values); clamped ----
gen_rot_mod:
    lea.l   gen_c_mod,%a1
    moveq   #2,%d1
    bras    gen_rot
gen_rot_dns:
    lea.l   gen_c_dns,%a1
    moveq   #101,%d1              | 0..100 %
    bras    gen_rot
gen_rot_hit:
    lea.l   gen_c_hit,%a1
    moveq   #64,%d1
    bras    gen_rot
gen_rot_rot:
    lea.l   gen_c_rot,%a1
    moveq   #64,%d1
    bras    gen_rot
gen_rot_not:
    lea.l   gen_c_not,%a1
    moveq   #2,%d1
    bras    gen_rot
gen_rot_key:
    lea.l   gen_c_key,%a1
    moveq   #12,%d1
    bras    gen_rot
gen_rot_scl:
    lea.l   gen_c_scl,%a1
    moveq   #GEN_NSCL,%d1
    bras    gen_rot
gen_rot_oct:
    lea.l   gen_c_oct,%a1
    moveq   #9,%d1                | 0..8
    bras    gen_rot
gen_rot_vel:
    lea.l   gen_c_vel,%a1
    moveq   #2,%d1
    bras    gen_rot
gen_rot_vmn:
    lea.l   gen_c_vmn,%a1
    moveq   #127,%d1              | velocity 1..127, kept as 0..126
    bras    gen_rot
gen_rot_vmx:
    lea.l   gen_c_vmx,%a1
    moveq   #127,%d1
    bras    gen_rot
gen_rot_rng:
    lea.l   gen_c_rng,%a1
    moveq   #4,%d1
gen_rot:                          | (closure, item, turn): turn = the clicks
    movel   %sp@(12),%d0
    beqs    gnr_out
    addl    %a1@,%d0
    bpls    gnr_hi
    moveq   #0,%d0
gnr_hi:
    cmpl    %d1,%d0
    blts    gnr_set
    movel   %d1,%d0
    subql   #1,%d0
gnr_set:
    movel   %d0,%a1@
gnr_out:
    rts

| ---- GEN's press (closure, item): write the track's trigs -------------------
gen_go:
    lea.l   %sp@(-40),%sp
    moveml  %d2-%d7/%a2-%a5,%sp@
    jsr     0x400cf866            | the UI context
    movel   %d0,%sp@-
    jsr     0x4000f208            | its pattern
    addql   #4,%sp
    movel   gm_track,%sp@-
    movel   %d0,%sp@-
    jsr     0x4000cfcc            | the menu's track in it
    addql   #8,%sp
    tstl    %d0
    beqw    gg_out
    movea.l %d0,%a2
    movel   %a2,%sp@-
    jsr     0x40016402            | its length
    addql   #4,%sp
    movel   %d0,%d3
    beqw    gg_out
    moveq   #64,%d0
    cmpl    %d0,%d3
    blss    gg_len
    movel   %d0,%d3
gg_len:
    bsr     gen_mix
    tstl    gen_c_not
    beqs    gg_nc
    bsr     gen_cands
gg_nc:
    moveq   #0,%d2                | the step
    moveq   #0,%d5                | trigs written
gg_l:
    bsr     gen_hit
    movel   %d0,%d4
    add.l   %d4,%d5
    movel   %d4,%sp@-
    movel   %d2,%sp@-
    movel   %a2,%sp@-
    jsr     0x40017b48            | trig on / off, as the grid does
    lea.l   %sp@(12),%sp
    moveq   #-1,%d0               | the track's default note...
    tstl    %d4
    beqs    gg_note
    tstl    gen_c_not
    beqs    gg_note
    bsr     gen_pick              | ...or one of the scale
gg_note:
    movel   %d0,%sp@-
    movel   %d2,%sp@-
    movel   %a2,%sp@-
    jsr     0x40016642
    lea.l   %sp@(12),%sp
    moveq   #-1,%d0               | the track's default velocity...
    tstl    %d4
    beqs    gg_vel
    tstl    gen_c_vel
    beqs    gg_vel
    bsr     gen_velo              | ...or one between VMN and VMX
gg_vel:
    movel   %d0,%sp@-
    movel   %d2,%sp@-
    movel   %a2,%sp@-
    jsr     0x400166c2            | the trig's velocity, -1 = the track's
    lea.l   %sp@(12),%sp
    addql   #1,%d2
    cmpl    %d3,%d2
    bcss    gg_l
    movel   %d5,gen_args          | "Trigs: n"
    pea     gen_args
    pea     gen_f_done
    pea     gen_pbuf
    jsr     0x400005cc
    lea.l   %sp@(12),%sp
    pea     gen_pbuf
    jsr     show_popup
    addql   #4,%sp
gg_out:
    moveml  %sp@,%d2-%d7/%a2-%a5
    lea.l   %sp@(40),%sp
    rts

| d2 = the step, d3 = the length (1..64) -> d0 = 1 for a trig. Keeps d2-d7.
gen_hit:
    tstl    gen_c_mod
    bnes    gh_euc
    bsr     gen_rand              | RND: a trig DNS times in 100
    swap    %d0
    andil   #0xffff,%d0
    mulu.w  #100,%d0
    clrw    %d0
    swap    %d0                   | 0..99
    cmpl    gen_c_dns,%d0
    bcss    gh_yes
    moveq   #0,%d0
    rts
gh_yes:
    moveq   #1,%d0
    rts
gh_euc:                           | EUC: step j is a trig when (j x k) mod n < k
    lea.l   %sp@(-8),%sp
    moveml  %d4-%d5,%sp@
    movel   gen_c_hit,%d4
    addql   #1,%d4                | k
    cmpl    %d3,%d4
    blss    gh_k
    movel   %d3,%d4               | no more trigs than steps
gh_k:
    movel   gen_c_rot,%d1
gh_r:
    cmpl    %d3,%d1
    bcss    gh_r2
    subl    %d3,%d1               | ROT mod n
    bras    gh_r
gh_r2:
    movel   %d2,%d0
    subl    %d1,%d0               | j = step - ROT, mod n
    bpls    gh_j
    addl    %d3,%d0
gh_j:
    mulsl   %d4,%d0
    movel   %d0,%d5
    divul   %d3,%d0
    mulsl   %d3,%d0
    subl    %d0,%d5               | (j x k) mod n
    moveq   #0,%d0
    cmpl    %d4,%d5
    bges    gh_e
    moveq   #1,%d0
gh_e:
    moveml  %sp@,%d4-%d5
    lea.l   %sp@(8),%sp
    rts

| xorshift32 -> d0. Clobbers d1.
gen_rand:
    movel   gen_seed,%d0
    movel   %d0,%d1
    lsll    #8,%d1
    lsll    #5,%d1
    eorl    %d1,%d0               | ^= x << 13
    movel   %d0,%d1
    swap    %d1
    andil   #0xffff,%d1
    lsrl    #1,%d1
    eorl    %d1,%d0               | ^= x >> 17
    movel   %d0,%d1
    lsll    #5,%d1
    eorl    %d1,%d0               | ^= x << 5
    movel   %d0,gen_seed
    rts

| a fresh seed for every GEN: the audio clock stirred in. Clobbers d0.
gen_mix:
    movel   gen_seed,%d0
    addl    0x8000184c,%d0
    addil   #0x9e3779b9,%d0
    bnes    gm_x_ok
    moveq   #1,%d0
gm_x_ok:
    movel   %d0,gen_seed
    rts

| gen_cand <- every note of the scale from OCT/KEY up to RNG octaves above
| (the top root included), at most 127; gen_ncand <- how many. Keeps d2-d7.
gen_cands:
    lea.l   %sp@(-20),%sp
    moveml  %d2-%d6,%sp@
    movel   gen_c_oct,%d2
    addql   #1,%d2
    mulu.w  #12,%d2
    addl    gen_c_key,%d2         | the lowest note: OCT 4, KEY C = 60
    movel   gen_c_scl,%d0
    lea.l   gen_masks,%a0
    movel   %a0@(0,%d0:l:4),%d3   | the scale: bit d = degree d
    movel   gen_c_rng,%d4
    addql   #1,%d4
    mulu.w  #12,%d4               | the span in semitones
    lea.l   gen_cand,%a1
    moveq   #0,%d5                | how many
    moveq   #0,%d0                | the degree
    moveq   #0,%d1                | semitones above the lowest
gc_l:
    btst    %d0,%d3
    beqs    gc_n
    movel   %d2,%d6
    addl    %d1,%d6
    cmpil   #127,%d6
    bhis    gc_n
    moveb   %d6,%a1@+
    addql   #1,%d5
gc_n:
    addql   #1,%d0
    cmpil   #12,%d0
    bnes    gc_d
    moveq   #0,%d0
gc_d:
    addql   #1,%d1
    cmpl    %d4,%d1
    blss    gc_l
    movel   %d5,gen_ncand
    moveml  %sp@,%d2-%d6
    lea.l   %sp@(20),%sp
    rts

| -> d0 = one of gen_cand, uniformly. Clobbers d1/a0.
gen_pick:
    bsr     gen_rand
    swap    %d0
    andil   #0xffff,%d0
    movel   gen_ncand,%d1
    mulu.w  %d1,%d0
    clrw    %d0
    swap    %d0
    lea.l   gen_cand,%a0
    mvzb    %a0@(0,%d0:l),%d0
    rts

| -> d0 = a velocity from VMN to VMX (either way round), uniformly.
| Clobbers d1.
gen_velo:
    movel   %d2,%sp@-
    bsr     gen_rand
    swap    %d0
    andil   #0xffff,%d0
    movel   gen_c_vmn,%d1         | d1 = the lower, d2 = the upper
    movel   gen_c_vmx,%d2
    cmpl    %d2,%d1
    blss    gv_ord
    movel   %d1,%d2               | VMN above VMX: the other way round
    movel   gen_c_vmx,%d1
gv_ord:
    subl    %d1,%d2
    addql   #1,%d2                | how many velocities
    mulu.w  %d2,%d0
    clrw    %d0
    swap    %d0
    addl    %d1,%d0
    addql   #1,%d0                | kept as 0..126: 1..127
    movel   %sp@+,%d2
    rts

| ---- the menu and its settings -----------------------------------------------
GEN_NSCL = 13
    .align 4
gen_desc:     .long gen_title, gen_vname, 13, gen_items
gen_items:    .long gen_lab_mod, gen_val_mod, gen_rot_mod, new_slot2_invoker
              .long gen_lab_dns, gen_val_dns, gen_rot_dns, new_slot2_invoker
              .long gen_lab_hit, gen_val_hit, gen_rot_hit, new_slot2_invoker
              .long gen_lab_rot, gen_val_rot, gen_rot_rot, new_slot2_invoker
              .long gen_lab_not, gen_val_not, gen_rot_not, new_slot2_invoker
              .long gen_lab_key, gen_val_key, gen_rot_key, new_slot2_invoker
              .long gen_lab_scl, gen_val_scl, gen_rot_scl, new_slot2_invoker
              .long gen_lab_oct, gen_val_oct, gen_rot_oct, new_slot2_invoker
              .long gen_lab_rng, gen_val_rng, gen_rot_rng, new_slot2_invoker
              .long gen_lab_vel, gen_val_vel, gen_rot_vel, new_slot2_invoker
              .long gen_lab_vmn, gen_val_vmn, gen_rot_vmn, new_slot2_invoker
              .long gen_lab_vmx, gen_val_vmx, gen_rot_vmx, new_slot2_invoker
              .long gen_lab_gen, gen_val_gen, rs_rot_none, gen_go
gen_n_mod:    .long gen_s_rnd, gen_s_euc
gen_n_key:    .long gen_k_c, gen_k_cs, gen_k_d, gen_k_ds, gen_k_e, gen_k_f
              .long gen_k_fs, gen_k_g, gen_k_gs, gen_k_a, gen_k_as, gen_k_b
gen_n_scl:    .long gen_sc_maj, gen_sc_min, gen_sc_dor, gen_sc_phr, gen_sc_lyd
              .long gen_sc_mix, gen_sc_loc, gen_sc_hmi, gen_sc_mmi, gen_sc_pma
              .long gen_sc_pmi, gen_sc_blu, gen_sc_chr
gen_masks:    .long 0xab5, 0x5ad, 0x6ad, 0x5ab, 0xad5    | MAJ MIN DOR PHR LYD
              .long 0x6b5, 0x56b, 0x9ad, 0xaad, 0x295    | MIX LOC HMI MMI PMA
              .long 0x4a9, 0x4e9, 0xfff                  | PMI BLU CHR
gen_n_num:    .long gen_vbuf
gen_n_go:     .long gen_s_go
gen_zero:     .long 0
gen_c_mod:    .long 0          | RND
gen_c_dns:    .long 50         | %
gen_c_hit:    .long 3          | 4 trigs
gen_c_rot:    .long 0
gen_c_not:    .long 0          | OFF
gen_c_key:    .long 0          | C
gen_c_scl:    .long 0          | MAJ
gen_c_oct:    .long 4          | from note 60, the track's default
gen_c_rng:    .long 0          | 1 octave
gen_c_vel:    .long 0          | OFF: the track's velocity
gen_c_vmn:    .long 63         | 64 (kept as velocity - 1)
gen_c_vmx:    .long 126        | 127
gen_seed:     .long 0x2545f491
gen_ncand:    .long 0
gen_args:     .long 0,0,0,0
kh_eaten6:    .long 0          | a Track press swallowed by Settings + Track
gen_cand:     .space 52        | the notes NOT picks from (4 octaves + 1)
gen_vbuf:     .space 16
gen_pbuf:     .space 24
gen_title:    .asciz "Generate"
gen_vname:    .asciz "GenerateMenuView"
gen_f_num:    .asciz "%d"
gen_f_done:   .asciz "Trigs: %d"
gen_s_mod:    .asciz "MOD"
gen_s_dns:    .asciz "DNS"
gen_s_hit:    .asciz "HIT"
gen_s_rot:    .asciz "ROT"
gen_s_not:    .asciz "NOT"
gen_s_key:    .asciz "KEY"
gen_s_scl:    .asciz "SCL"
gen_s_oct:    .asciz "OCT"
gen_s_rng:    .asciz "RNG"
gen_s_gen:    .asciz "GEN"
gen_s_vel:    .asciz "VEL"
gen_s_vmn:    .asciz "VMN"
gen_s_vmx:    .asciz "VMX"
gen_s_go:     .asciz "GO"
gen_s_rnd:    .asciz "RND"
gen_s_euc:    .asciz "EUC"
gen_k_c:      .asciz "C"
gen_k_cs:     .asciz "C#"
gen_k_d:      .asciz "D"
gen_k_ds:     .asciz "D#"
gen_k_e:      .asciz "E"
gen_k_f:      .asciz "F"
gen_k_fs:     .asciz "F#"
gen_k_g:      .asciz "G"
gen_k_gs:     .asciz "G#"
gen_k_a:      .asciz "A"
gen_k_as:     .asciz "A#"
gen_k_b:      .asciz "B"
gen_sc_maj:   .asciz "MAJ"
gen_sc_min:   .asciz "MIN"
gen_sc_dor:   .asciz "DOR"
gen_sc_phr:   .asciz "PHR"
gen_sc_lyd:   .asciz "LYD"
gen_sc_mix:   .asciz "MIX"
gen_sc_loc:   .asciz "LOC"
gen_sc_hmi:   .asciz "HMI"
gen_sc_mmi:   .asciz "MMI"
gen_sc_pma:   .asciz "PMA"
gen_sc_pmi:   .asciz "PMI"
gen_sc_blu:   .asciz "BLU"
gen_sc_chr:   .asciz "CHR"
    .align 2
