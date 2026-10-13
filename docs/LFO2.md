# LFO 2, 3 and 4: more LFOs on every track

*VA Edition only.* Every track gets three more LFOs, each the stock LFO's
twin. LFO 2 is a full LFO: p-lockable and saved with the sound. LFO 3 and
LFO 4 are lighter: no p-locks, saved with the pattern (see
[LFO 3 and LFO 4](#lfo-3-and-lfo-4)).

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.**

## Using it

- Press **LFO** as usual: the LFO menu opens on **LFO 1**.
- Tap **LFO** again: the same menu now edits **LFO 2**, and a **2** appears
  under the word *LFO* on the left of the screen.
- Tap **LFO** a third time for **LFO 3**, a fourth for **LFO 4**, and a
  fifth to close (or leave any other way). The menu always opens on LFO 1.
- **Tap or hold.** The LFO changes when you let go of **LFO**, and only if
  you did nothing else while it was down. Hold **LFO** and turn a knob (the
  stock way to set the destination to that knob's parameter) and the LFO
  showing gets it and stays showing. Pressing another key while **LFO** is
  down counts as a hold too: the stock *Clear LFO* clears the LFO showing,
  and the release then does what it does on stock.
- On LFO 2, the menu and the **LFO SPEED** knob edit LFO 2's Waveform,
  Multiplier, Destination, Depth and Speed. They work exactly as LFO 1's do:
  same waves, tempo sync and destinations. Each value can be p-locked: hold a
  step while LFO 2 is showing.
- **Destinations never collide.** When you scroll DEST, any destination
  another of the track's LFOs already uses is skipped. If there is none free
  further on in the list, DEST stays where it was. *None* can be chosen on
  all of them.
- LFO 2 can target any destination LFO 1 can, including LFO 1's own
  parameters (for example LFO 1's Depth or Speed), since LFO 2 runs first
  each block.
- A new or initialised sound starts LFO 2 with LFO 1's defaults (Destination
  *None*). Sounds and projects from before this version load with LFO 2 off.

### LFO 3 and LFO 4

- Tap **LFO** on LFO 2 for **LFO 3** (a **3** under *LFO*), again for
  **LFO 4**, and once more to close. It is the same LFO menu: the same rows
  (Wav, Mul, Dst, Dep), icon and values, and the **LFO SPEED** knob sets the
  LFO showing, exactly as on LFO 1 and LFO 2.
- **No p-locks.** With LFO 3 or 4 showing, turning a value while holding a
  step does nothing (it never touches LFO 1's or 2's locks).
- **Saved with the pattern.** The sound record has no room left (LFO 2 took
  the last of it), so LFO 3 and 4 belong to the pattern's **kit**, per
  track: each of the 96 patterns has its own, they change when the pattern
  changes (as LFO 1 and 2 do, being in the track sounds), and they are
  saved in the project, in a part of each pattern's kit record the Cycles
  leaves unused. Projects saved before this version load them at LFO 1's
  defaults (destination *None*); projects saved with them still load on the
  stock OS or older Model-TG, which ignore them.
- **Reset like LFO 1.** Clearing a track's sound, clearing a pattern's
  sounds, a new project, and *Clear LFO* (on the LFO showing) put them back
  to LFO 1's defaults. Pattern copy / paste and undo carry them with the
  pattern.
- They are not part of the sound: loading a preset or pasting a copied
  sound does not carry them, and undoing a sound clear does not bring them
  back.
- They share LFO Setup (Trig Mode, Fade, Start Phase) with LFO 1, like LFO 2.
- An LFO 3 or 4 with no destination on any track costs no CPU at all.

### What LFO 2 shares with LFO 1

The Cycles keeps 33 parameter words per sound. Only five are unused, and the
LFO menu edits exactly five values, so those five are LFO 2's. The three
values in **LFO Setup** (**FUNC** + **LFO**: Trig Mode, Fade, Start Phase)
have no room left, so **LFO 2 uses LFO 1's**. Both LFOs retrigger, fade
and start the same way. FUNC + LFO always shows those shared values,
whichever LFO the menu was on.

### Other limits

- **Clear LFO** (the stock command) clears the LFO the menu shows.
  Fade, Start Phase and Trig Mode, shared by all four, are cleared with it.
- LFO 2's Speed and Depth are saved with the sound to half a displayed
  step; anything finer is rounded down when the sound is saved. Pattern
  p-locks keep full precision.
- The RND wave of both LFOs draws from the one random generator, so two RND
  LFOs on one track never produce the same sequence.
- Projects that use LFO 2 load on stock Model-TG or the stock OS with
  LFO 2 ignored.

## How it works

The fork patches the stock sites below, each asserted byte for byte by
`build.py`. The code is in `src/lfo2.inc`.

| what | where (OS 1.13) | change |
|---|---|---|
| words | sound +20 + 2k, k = 28..32 | LFO 2's Speed, Multiplier, Waveform, Destination, Depth. Nothing in the stock OS uses these words. The audio mirror already copies all 33 words (66 bytes) per track |
| audio | `jsr 0x40091ab2` at 0x400597c6 | `lfo_run`: the stock LFO engine runs twice, first for LFO 2, then for LFO 1 |
| UI | slotOf 0x4005a4e8 | while LFO 2 shows, ids 0x1d/0x1e/0x21/0x20/0x24 map to words 28..32 |
| menu | constructor call 0x4001c3dc, destructor 0x400d979a | every menu starts on LFO 1; nothing stays remapped after it closes |
| key | LFO menu View vtable +8 (0x401016e4) | a tap of LFO switches to the next LFO at its release; a hold (the stock menu's knob-while-held flag, view - 56 + 604, or another key) leaves it to stock |
| draw | LFO menu View vtable +0x10 (0x401016ec) | stock draw, then "2" on the second title line |
| DEST | `fine_hook` (track handle slot 0x14) for id 0x20 | skips the other LFO's destination |
| defaults | default fill 0x40061866 | LFO 2, and for a pattern's track sound LFO 3 and 4, get LFO 1's defaults after a sound init |
| LFO 3/4 | handle get/set (vtable 0x400fd134 +28/+92), p-lock editor 0x40025e10 | pages 3 and 4 of the LFO menu (below) |
| kit | kit save 0x4005b2ae, kit load 0x4005d080 | LFO 3/4 into and out of the kit record's unused +646..+699 |
| copies | memcpy 0x4008f1f0 | a whole kit's copy (7094 bytes) carries its LFO 3/4 |
| save/load | `snd_save_post` / `snd_load_post` | packed into the sound record's free long at +88 |
| locks | pattern lock save 0x4005b9c6, load 0x4005aa1a | slot numbers for words 23..32 (see below) |

**Audio.** The stock engine `0x40091ab2(params, tempo, trigmask)` keeps 32
bytes of state per track at 0x40fde838. It reads its eight parameters at
`params + 14 + 66t + 2k` (k = 1..8) and adds its output to the destination
word, clamping to 0..32512. `lfo_run` does the following:

1. Fills a private parameter block, `lfo2_buf`, with LFO 2's words in LFO 1's
   places. Fade, Phase and Trig Mode are copied from LFO 1.
2. Swaps LFO 2's state into the engine's state area, calls the engine on
   that block, then swaps it back.
3. Adds LFO 2's output (state +20) to the real word named by its
   destination (state +16), with the engine's clamp. It skips None, a
   value that is not a word, and LFO 1's current destination. That last
   check covers p-locks and edited projects where the menu's rule was
   bypassed.
4. Calls the engine for LFO 1 with the original arguments, exactly as stock
   does.

So LFO 2 *is* the stock LFO. The host test on the real stock engine checks
that the same settings give LFO 2 the same output as LFO 1. Cost: the
engine for six tracks is ~950 instructions per call, and `lfo_run` is
~2,500, so ~1,550 extra per control block. That is less than half of one VA
track's block.

**Sound record.** The stock save keeps words 0..22. Model-TG uses record
+76..+87, and LFO 2 takes the free long at +88:
`10` tag (bits 31-30), Speed >> 7 (29-22), Multiplier >> 8 (21-17),
Waveform >> 8 (16-14), Destination (13-8), Depth >> 7 (7-0). A record
without the tag loads LFO 2's defaults.

**Pattern p-locks (also a fix for Model-TG).** The pattern save writes
one row per locked word as {slot, track, 64 steps}. It finds the slot
through a 23-entry table indexed by the word, with no bound. For words
23..32 it read past the table and wrote wrong slots. Model-TG's Attack,
Filter and Resonance locks (words 23..25) therefore came back on the wrong
parameters after a save and load. The save now writes the following slots;
the loader maps them back for tracks 0-5:

| word | 23 Attack | 24 Filter | 25 Resonance | 26, 27 (Sampler options) | 28..32 LFO 2 |
|---|---|---|---|---|---|
| slot | 24 | 25 | 26 | not saved (0xff, skipped) | 27..31 |

Stock words keep their stock slots (0..23, slot 18 unused), and the FX row
(track 6) is untouched.

**LFO 3 and 4 per pattern.** All 96 kits sit in RAM at `0x409C9C80 + n x
7094` (track sounds at +28 + 100t; `*0x40a78888` is the playing one).
`lfo34_kits` holds a 120-byte block per kit: LFO 3 then LFO 4, tracks 0-5,
five words each. Audio and the menu use the playing kit's block
(`l34_blk`). The kit's storage record (the per-pattern record in the
project file) is written by `0x4005b2ae`: name, levels, sounds at +32, the
FX block at +632 (of which `0x4005b266` writes +632..+645 and +700..+707)
and the sound pool at +708. Nothing writes or reads +646..+699, so
`kit_save_hook` puts `"LF34"` at +648 and the 12 LFOs packed as LFO 2's at
+652..+699; `kit_load_hook` reads them back, or LFO 1's defaults when the
magic is missing. A whole-kit memcpy - the new project's kits 1-95 copied
from kit 0, pattern copy / paste / undo through their buffers - carries
the block: from a kit or a buffer seen filled (the last four are
remembered) to a kit or a buffer. The blocks take 11.5 KB of Model-TG's
memory; the blob now uses 8 of the filesystem cache's 16 blocks.

## Verification

Host verified (`tests/test_lfo2.py`, Unicorn ColdFire V4e):

- slotOf remaps only the five ids, and only while LFO 2 shows.
- The key sequence LFO → LFO → LFO → LFO → LFO opens LFO 1, switches to LFO
  2, 3 and 4 at each tap's release (each redrawn), then closes through the
  stock handling. A press held while a knob turns (the stock flag set), or
  while another key is pressed, keeps the LFO showing and leaves the
  release to stock. Repeats are ignored, FUNC + LFO stays stock, and the
  number under *LFO* is "2", "3" or "4" as it should be.
- DEST skips every other LFO's destination on the selected track (three in
  a row if need be), for each of the four pages, not another track's, and
  stays put at the end of the list.
- On pages 3 and 4 the track handle's get (+28) and set (+92) answer the
  five LFO ids from the right LFO's words for the selected track, and ask the
  menu to redraw; other ids, and pages 1 and 2, go to the stock methods.
  slotOf answers -1 for those ids there (no lock can land on LFO 1 or 2),
  and the menu's p-lock editor ignores a turn with a step held.
- `lfo_run` skips LFO 3/4 when no track has a destination, otherwise builds
  their block from their own words (sharing LFO 1's Fade/Phase/Mode), swaps
  in their own engine state and applies their output.
- `lfo_run` builds the parameter block correctly, restores LFO 1's state,
  applies LFO 2 with the clamp, and skips None, out-of-range and LFO 1's
  destination.
- Sound pack/unpack round-trips, untagged records load defaults, and the
  default-fill hook writes defaults with a balanced stack.
- Lock rows: every word 0..32 maps to a slot and back, and 26/27 are dropped.
- LFO 3/4 per kit: each of the 96 kits maps to its block and nothing else
  does; the menu reads and writes the playing kit's; a kit saves and loads
  back exactly (to the packing's precision), writing nothing outside
  +648..+699 of the record, and nothing on a failed save or load; a record
  without the magic loads defaults; a scratch kit saves defaults and loads
  into no block; a track's sound reset resets that track's LFO 3 and 4 only;
  memcpy of other sizes is untouched, and a kit's copy carries its block
  kit to kit and through a buffer, but not from an unknown source.
- **On the real stock engine** (with `MODEL_CYCLES_STOCK` set): LFO 2
  matches LFO 1 sample for sample on every wave except RND, which shares the
  RNG. LFO 3 and LFO 4 match LFO 1 too. LFO 1's output under `lfo_run` is
  identical to the stock engine alone.
- `build.py` asserts the stock bytes at every patch site. The existing VA
  and regression suites still pass.

Needs hardware:

1. LFO, LFO, LFO: menu opens, shows "2" under LFO, closes. FUNC + LFO still
   opens LFO Setup. LFO followed by another page key behaves as stock.
2. On LFO 2, set DEST to Pitch and Depth up: pitch moves. Set LFO 1 to
   Decay: both act together, and LFO 1 DEST skips Pitch.
3. LFO 2 Speed knob, Multiplier and all seven waves sound like LFO 1's.
4. P-lock LFO 2 Depth on a step; save, reload: lock still on LFO 2.
5. P-lock Attack, Filter, Resonance; save the pattern, reload: each lock is
   on its own parameter (the Model-TG fix).
6. Save a sound with LFO 2 set, reload it from the pool; init a new sound:
   LFO 2 at defaults.
7. Load a project saved with an older firmware: LFO 2 is off.
8. CPU page with six tracks with all four LFOs running: no dropouts.
9. LFO, LFO, LFO, LFO, LFO: pages 1-4 ("2", "3", "4" under LFO), the same
   rows and icon on each, then closed. LFO SPEED sets the LFO showing.
   On each page, hold LFO and turn a knob: that LFO gets the destination,
   and the page stays when LFO is let go.
10. LFO 3/4 values look like LFO 1's (wave names, multipliers, destination
    names); DST skips the other three LFOs' destinations.
11. LFO 3 on Pitch and LFO 4 on Filter alongside LFO 1 and 2: all four move.
12. Set LFO 3/4 on two patterns differently; switch patterns: each plays its
    own. Save the project, power off, load it: both come back.
13. Clear a track's sound, clear a pattern's sounds, and start a new
    project: LFO 3 and 4 at defaults. Copy a pattern and paste it: its LFO 3
    and 4 come along. Load a project saved by the previous firmware: LFO 3
    and 4 at defaults.
