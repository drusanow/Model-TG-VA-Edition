# LFOs to MIDI CC

*Plaits Edition.*

**Status: verified on the host only. Not yet tested on hardware: back up
first.**

## Using it

1. Press **LFO** to open the LFO menu. Tap **LFO** again to reach LFO 2, 3
   or 4, as usual.
2. Scroll down to **MIDI CC**. Press **DATA**, turn it to the CC number you
   want (0 to 127, or **OFF**), and press **DATA** again. The CC is switched
   on at that second press. Nothing is sent while you scroll, and leaving
   the menu without the second press keeps the old setting.
3. On **CC Base**, turn **DATA** to the value the LFO should sweep from (0 to
   127, 64 to start).

That LFO of the selected track now sends its movement as that CC:

- **Channel:** the track's own MIDI channel, as set in the MIDI settings.
  A track whose channel is OFF sends nothing.
- **Port:** wherever the Cycles sends MIDI, MIDI OUT and USB.
- **Destination:** the CC goes out as well as to the LFO's Destination. Set
  Destination to **None** for an LFO that only moves your external gear.
- **Depth** sweeps one way from the Base, in the menu's own steps (-64 to
  +63):
  - Base 50, Depth -15: between 50 and 35.
  - Base 50, Depth +15: between 50 and 65.
  - Depth 0 holds the Base. The value stops at 0 and 127.
- **Shape:** Speed, Multiplier, Fade, Start Phase and the Trig Modes act on
  it as on the internal destination. Each waveform runs from 0 to the full
  Depth:

  | wave | the sweep |
  |---|---|
  | TRI | out and back, in straight lines |
  | SIN | out and back, smoothly |
  | SQR | full Depth for half a cycle, then the Base |
  | SAW | from full Depth back to the Base |
  | EXP | from full Depth back to the Base, fast at first |
  | RMP | from the Base out to full Depth |
  | RND | a new random amount each cycle, held |

Each of the 24 LFOs (four per track, six tracks) has its own CC number and
Base.

## Limits

- **The settings belong to the pattern**, like the LFO's own settings:
  - Each pattern has its own CC numbers and Bases.
  - They are saved when you save the pattern or the project, as for any
    other change. A pattern you haven't saved keeps them only until power
    off.
  - A pattern's saved record has no room for them, so it holds a short
    fingerprint. The settings themselves go in a small file in the
    `/LFOCC/` folder, named after that fingerprint. Patterns with the same
    settings share one file, so the folder stays small.
  - Copying a pattern copies them. Earlier builds kept one machine-wide
    set in `/lfo_cc.bin`, which this build no longer reads, so set them
    again on the patterns that use them.
- **It sends only when the value changes**, and at most about 120 messages
  a second per LFO. That is smooth for most sweeps, and leaves plenty of
  the MIDI cable for notes. Fast LFOs (tens of Hz) come out stepped.
- **7-bit CC only:** no NRPN, no 14-bit.
- **The menu rows edit the selected track's LFO**, the same one the other
  rows show.

## How it works

The code is in `src/lfocc.inc`.

- **The settings** are 48 bytes per pattern, at +124 of each kit's LFO 3/4
  block. The block is now 172 bytes, and `L34_KITS` is 20 KB in the sample
  region. Index is LFO x 6 + track.
  - **Numbers:** 24 bytes. 0 means OFF, n + 1 means CC n.
  - **Bases:** 24 bytes, 0 to 127, stored ^ 64 so a cleared block reads 64.
  - **The playing pattern's** settings are used everywhere (`lcc_cur`): the
    sending, the value, the menu. `lcc_mask`, the LFOs some track sends, is
    worked out from them every block.
  - The block goes wherever a kit goes: copied by `kit_memcpy`, cleared
    with the kit's FX defaults (`sfx_fxd` → `lcc_kclr`: a new kit, a new
    project).
- **Saving.** The kit record's +700..+707 are free: the FX save writes
  zeros there and the FX load never reads them.
  - `lcc_ksave`, from `kit_save_post`, writes "C", the settings' FNV-1a
    hash and a check byte there. It queues the kit, and a UI-task message
    (`lcc_write`, as the slice files are written) writes the 48 bytes to
    `/LFOCC/` plus the hash in 8 hex digits, making the folder if needed.
  - All OFF and 64 (all zero) writes nothing, so the record keeps the stock
    zeros. An older record, or a bad check, loads all OFF.
  - `lcc_kload`, from `kit_load_post`, clears the kit's settings and notes
    the hash (`lcc_lh`). `lcc_poll` then reads one such file a block on the
    UI clock (`lcc_fread`), never during the load itself. It keeps the file
    only if its size and hash match.
- **The value.** `lfo_run` calls `lcc_calc` once its four LFOs have run.
  For each LFO with a CC:
  - **Phase:** from the engine's state +0 (0 to 1382400000 a cycle, so
    Speed, Multiplier, Start Phase and the Trig Modes are all in it), as
    0 to 65535.
  - **Wave:** shaped here, 0 to 65535, by the LFO's Waveform (table
    above). SIN is sin² of half the phase, from `mi_sin`. EXP is
    (1 - phase)^4. RND draws a new value when the phase wraps.
  - **Fade:** the wave is multiplied by the engine's fade gain, state +8
    (2^30 = full).
  - **Value:** Base + (Depth - 16384) x wave / 2^24, rounded, then clipped
    to 0 to 127. Depth comes from the audio thread's parameter block (k8
    for LFO 1, k32 for LFO 2, so their p-locks count), or from the playing
    pattern's block for LFO 3 and 4.
  - Earlier builds took the engine's own output, state +4, centred on 64.
    That ignored the knob position on the external gear and swung in both
    directions. Running the real stock engine in the emulator showed the
    phase is the dependable part. The multiplier changed the period as it
    should (each step halves it), and the CC now follows it.
  - LFO 3 and 4 are skipped when no track has a Destination for them. They
    now run while any track sends them as a CC.
- **The sending.** `lcc_poll` runs on the UI clock, in task context
  (`led_hook`, once an audio block, where the quantised mutes' CC 94 goes
  out too).
  - It sends at most one message a block, going round the 24 LFOs.
  - Each LFO sends only when its value differs from the last one it sent,
    and only `LCC_GAP` (12) blocks, about 8 ms, after its last message.
  - The channel comes from `0x40012e82(mute object, track)`, as the stock
    mute CC asks it. The message goes through the stock
    `0x40082800(channel, CC, value)`, which checks that MIDI is up and the
    channel is 0 to 15.
  - A CC that changes sends again from its first value.
- **The menu rows.** `lfo_menu_new` calls the LFO menu's constructor, then
  `lcc_row_add`, which appends two MenuItems built as `lm_build` builds a
  list menu's rows.
  - **MIDI CC's press** is `lcc_press`. It reads the stock edit state
    (`0x400732f8`) before the stock toggle (`new_slot2_invoker`).
    - Entering the edit, a pending CC (`lcc_pv`) starts from the one in
      force.
    - Leaving the edit, the pending CC is taken.
    - The turn (`lcc_rot`) moves only the pending CC, and the row shows it
      while the edit is open.
    - Closing the menu or changing LFO drops an unpressed edit.
  - **CC Base** (`lcc_rotb`) changes at once.
- **Space.** To fit, LFO 3/4's per-pattern blocks (`lfo34_kits`, 11.9 KB of
  zeros) moved out of the blob into the sample region (`L34_KITS`, below the
  send effects' lines). The boot clear in `phase1.s` zeroes them, as the
  blob's zeros were.

## Verification

Host verified (`tests/test_lfocc.py`):

- **The value:**
  - For each of the four LFOs and six waves: Base 50, Depth -15 sweeps
    exactly 50 down to 35 over a cycle, matching a Python model at every
    one of 64 phases.
  - Other bases, depths, waves and fades match the model; the value clips
    at 127.
  - RND holds one value through a cycle and draws a new one each cycle.
  - An LFO with no CC is left alone.
  - `lcc_mask` follows the numbers.
- **LFO 3 and 4** run for their CC, with no Destination on any track.
- **Sending:**
  - It waits until a value has been worked out, then sends only on change,
    on the track's channel.
  - It sends once every 12 blocks at most per LFO, and one LFO a block,
    going round them all.
  - Nothing goes out on a track whose channel is off.
  - CC 0 and CC 127 both work.
- **The rows:**
  - MIDI CC: turning outside an edit does nothing. Inside one, the pending
    CC is shown but not in force (nothing sent, mask unchanged); the press
    that ends the edit takes it. It reaches OFF and clamps at 127.
  - Closing the menu mid-edit takes nothing.
  - CC Base: turns at once, 0 to 127, 64 to start.
  - Both rows are added to the LFO menu with their closures, the turn's data
    being the menu, and the constructor's result is kept.
- **Per pattern:**
  - Two patterns hold different CCs and Bases. The menu and the sending
    follow the playing one.
  - Saving writes "C" and the hash at +700, and posts the file's write.
    The file holds exactly the 48 bytes, under its hash's name.
  - Loading into another kit clears it, then the UI clock reads the file
    back into it.
  - All OFF saves nothing (+700..+707 untouched) and posts nothing.
  - Old or odd records load all OFF. So do a missing file and a wrong or
    short one.
  - Copying a kit carries them, and the FX defaults clear them.
- **Against the real stock LFO engine** (run in the emulator from the built
  image): Base 50, Depth -15 gives 35 to 50 for all seven waves, and each
  Multiplier step halves the CC's period.

Needs hardware:

1. The LFO menu shows **MIDI CC** and **CC Base** at the bottom, on LFO 1
   to 4, and the menu still works as before: tap and hold LFO, DEST, and
   the stock Clear LFO.
2. **MIDI CC:** press DATA, scroll to 74, and check that nothing is sent
   while scrolling. Press again: it starts.
3. A synth on MIDI OUT (or a MIDI monitor on USB), on track 1's channel:
   LFO 1 → CC 74, Destination None, Base 50, Depth -15. It moves between
   50 and 35. Multiplier 1 bar against x16: clearly slower.
4. Two LFOs on two tracks at once, plus notes: no stuck or late notes.
5. **Per pattern:** set different CCs on two patterns and switch between
   them. Each sends its own.
6. Save the project, power off and on, and load it: each pattern's CCs and
   bases are back.
7. A track with its MIDI channel OFF sends nothing.
