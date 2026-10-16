# LFOs to MIDI CC

*Plaits Edition.*

**Status: verified on the host only. Not yet tested on hardware: back up
first.**

## Using it

1. Press **LFO** to open the LFO menu. Tap **LFO** again to reach LFO 2, 3
   or 4, as usual.
2. Scroll down to the fifth row, **MIDI CC**.
3. Turn **DATA** to choose a CC number from 0 to 127, or **OFF**.

That LFO of the selected track now sends its movement as that CC:

- **Channel:** the track's own MIDI channel, as set in the MIDI settings.
  A track whose channel is OFF sends nothing.
- **Port:** wherever the Cycles sends MIDI, MIDI OUT and USB.
- **Destination:** the CC goes out as well as to the LFO's Destination. Set
  Destination to **None** for an LFO that only moves your external gear.
- **Depth:** sets the sweep, centred on 64.
  - Depth 0 sends a steady 64.
  - Full Depth sweeps the whole 0 to 127.
  - A negative Depth turns the sweep upside down.
- **Shape:** Speed, Multiplier, Waveform, Fade and the trig modes all act on
  the CC just as they act on the internal destination. It is the same LFO.

Each of the 24 LFOs (four per track, six tracks) has its own CC number.

## Limits

- **The CC numbers belong to the machine, not the pattern.** They work like
  the tracks' MIDI channels: one setting for every pattern and project.
  They are kept when you power off, in a small file, `/lfo_cc.bin`, written
  when you close the LFO menu after changing one. The LFO itself (its
  Speed, Depth and the rest) is still saved with the pattern. A pattern's
  record has no room left for the numbers.
- **It sends only when the value changes**, and at most about 120 messages
  a second per LFO. That is smooth for most sweeps, and leaves plenty of
  the MIDI cable for notes. Fast LFOs (tens of Hz) come out stepped.
- **7-bit CC only:** no NRPN, no 14-bit.
- **The menu row edits the selected track's LFO**, the same one the other
  rows show.

## How it works

The code is in `src/lfocc.inc`.

- **The numbers** are `lcc_num`, one byte per LFO and track (index
  LFO x 6 + track). 0 means OFF; n + 1 means CC n. `lcc_mask` has bit l set
  when some track sends LFO l.
  - They are read from `/lfo_cc.bin` the first time the UI clock runs.
    The file holds "LCC1", the 24 bytes and 4 spare. A missing or odd file
    leaves them all OFF, and an out-of-range byte is OFF.
  - The file is written by a UI-task message, posted from the LFO menu's
    destructor when a number changed. The write sequence is the one the
    slice markers' files use.
- **The value.** `lfo_run` calls `lcc_calc` once its four LFOs have run.
  For each LFO with a CC, it reads the engine's own output, state +4 (after
  the fade), and the LFO's Depth, scaled as the engine scales it:
  (Depth - 16384) x 2. Those are exactly the two numbers the engine
  multiplies to modulate a destination word, so the CC follows the
  internal modulation. The CC value is 64 + (output >> 16) x that / 2^24,
  clipped to 0 to 127.
  - Depth comes from k8 (LFO 1) and k32 (LFO 2) of the audio thread's
    parameter block, so their p-locks count. For LFO 3 and 4 it comes from
    the playing pattern's block.
  - LFO 3 and 4 are skipped when no track has a Destination for them. They
    now run while any track sends them as a CC.
  - The stock engine computes its output whatever the Destination, so
    Destination None still gives a moving CC.
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
  - A number that changes sends again from its first value.
- **The menu row.** `lfo_menu_new` now calls the LFO menu's constructor,
  then `lcc_row_add`, which appends one MenuItem built as `lm_build` builds
  a list menu's rows. Its closures are `lcc_lab`, `lcc_value` (OFF or the
  number, through `gm_val`) and `lcc_rot` (one step a click, clamped, then
  a redraw of the menu as `qm_rot` does it).
- **Space.** To fit, LFO 3/4's per-pattern blocks (`lfo34_kits`, 11.9 KB of
  zeros) moved out of the blob into the sample region (`L34_KITS`, below the
  send effects' lines). The boot clear in `phase1.s` zeroes them, as the
  blob's zeros were. The blob is back to ~10 KB spare in its eight cache
  blocks.

## Verification

Host verified (`tests/test_lfocc.py`):

- **The value:**
  - For each of the four LFOs: full positive and negative output at full
    Depth give 127 and 0, and a half output gives 95. Depth 0 gives 64,
    and a negative Depth inverts.
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
- **The row:**
  - It turns OFF, 0 to 127 and clamps, marking the setting changed and the
    LFO to send again.
  - It shows OFF, 0, 7, 10, 74, 99, 100 and 127.
  - It is added to the LFO menu with its four closures, the turn's data
    being the menu, and the constructor's result is kept.
- **The file:**
  - Closing the menu after a change posts the write; closing it otherwise
    posts nothing.
  - The write produces the 32-byte file, and reading it back restores the
    numbers and the mask.
  - A missing file, a wrong magic or an out-of-range byte reads as OFF.

Needs hardware:

1. The LFO menu shows a fifth row, **MIDI CC**, on LFO 1 to 4. **DATA**
   changes it, and the menu still works as before: tap and hold LFO, DEST,
   and the stock Clear LFO.
2. A synth on MIDI OUT (or a MIDI monitor on USB), on track 1's channel:
   LFO 1 → CC 74 with Destination None. The CC sweeps with the LFO's Speed,
   Waveform and Depth. Depth 0 holds it at 64.
3. Two LFOs on two tracks at once, plus notes: no stuck or late notes.
4. Power off and on: the CC numbers are still there.
5. A track with its MIDI channel OFF sends nothing.
6. Boot: LFO 3 and 4 still behave per pattern as before. They now live
   outside the blob.
