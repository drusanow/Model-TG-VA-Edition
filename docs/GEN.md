# GEN: random and Euclidean trigs

*VA Edition only.* A trig generator for the selected track, with optional
random notes in a key.

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.**

## Using it

Hold **SETTINGS** and press **TRACK**. The *Generate* menu opens for the
track that is selected. As in the other Model-TG menus, turn **DATA** to move
between rows, press it to edit a row, and turn to change the value. Select
**GEN** and press **DATA** to write the trigs; a popup says how many were
written. You can press GEN again for a new result. **SETTINGS** or return
closes the menu.

| row | values | what it does |
|---|---|---|
| **MOD** | RND, EUC | RND: each step gets a trig at random. EUC: a Euclidean rhythm |
| **DNS** | 0-100 | RND only: the chance, in %, that a step gets a trig |
| **HIT** | 1-64 | EUC only: how many trigs, spread as evenly as the steps allow |
| **ROT** | 0-63 | EUC only: shifts the rhythm this many steps later |
| **NOT** | OFF, ON | ON: each new trig gets a random note from the scale below |
| **KEY** | C … B | the scale's root |
| **SCL** | MAJ MIN DOR PHR LYD MIX LOC HMI MMI PMA PMI BLU CHR | the scale |
| **OCT** | 0-8 | the lowest octave. **OCT 4 with KEY C starts at note 60, the track's default note** (the pitch the track plays with no note set) |
| **RNG** | 1-4 | how many octaves the notes span, the top root included |
| **GEN** | GO | press: generate |

Scales: major, natural minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
harmonic minor, melodic minor, major pentatonic, minor pentatonic, blues,
chromatic.

Examples:
- *EUC, HIT 3* on a 8-step track gives the tresillo `x..x..x.`.
- *EUC, HIT 5* on 16 steps gives `x..x..x..x..x...`. *ROT 2* moves it two
  steps later.
- *RND, DNS 25, NOT ON, KEY A, SCL PMI, OCT 3, RNG 2* gives a sparse minor
  pentatonic line from A3 up two octaves.

### What GEN changes

- **It replaces the track's trigs** over the track's length (all steps if the
  pattern is longer than one page). Steps past the length are untouched.
  There is no undo: save the pattern first if you may want it back.
- New trigs are plain note trigs, as when you add one on the grid.
- With **NOT ON**, each trig gets a trig note. With **NOT OFF**, and on every
  step left empty, the trig note is reset to the track's default note.
- **P-locks are not touched.** A lock on a step that ends up with a trig
  plays again on that step.
- Velocity and length stay at the track's defaults.
- The settings are shared by every track and kept until power-off. They are
  not saved with the project.
- Each GEN gives a new result, even with the same settings.

## How it works

The code is in `src/gen.inc`.

- **The chord.** `key_hook` already sees every key with SETTINGS as the
  chords' modifier. SETTINGS + TRACK (key 2) goes to `kh_gen`, which opens the
  menu the way SETTINGS + RECORD opens Resample. The press and its release
  are swallowed, so TRACK's own action does not run. TRACK alone, and
  TRACK + trig key (track select), are untouched.
- **The menu** is the firmware's own list menu, built by `lm_build` from a
  descriptor (`gen_desc`) of ten rows {label, value, turn, press}, like
  Resample's. GEN's press is `gen_go`.
- **The pattern.** `gen_go` takes the menu's track pattern object
  (`0x4000cfcc(0x4000f208(ui), track)`), reads its length (`0x40016402`), and
  for each step calls the grid's own routines:
  - `0x40017b48(obj, step, on)`: the trig on or off. It also clears the
    trigless and note-trig overrides, exactly as adding a trig on the grid
    does.
  - `0x40016642(obj, step, note)`: the trig note. -1 means the track's
    default (+712, 60 on a new pattern).

  These routines notify the pattern's observers, so the grid lights, the
  sequencer and saving all see the change as an ordinary edit.
- **RND:** a step gets a trig when a random number 0..99 is below DNS. The
  generator is xorshift32, re-seeded from the audio clock at each press.
- **EUC:** with k trigs over n steps, step j (rotated by ROT) gets a trig
  when `(j × k) mod n < k`. That gives k trigs with gaps that differ by at
  most one step: a Euclidean rhythm.
- **Notes:** the candidates are every scale note from `12 × (OCT + 1) + KEY`
  up to RNG octaves higher, top root included, capped at 127. Each trig picks
  one at random.

Cost: nothing while idle. A press runs once, in the UI task.

## Verification

Host verified (`tests/test_gen.py`, Unicorn ColdFire V4e):

- **EUC:** every length 1-16, 24, 32, 48 and 64 with every hit count, at
  random rotations, matches the formula. Each result has exactly k trigs
  and gaps that differ by at most one. Known rhythms check out, including
  the tresillo and 5-in-16. More hits than steps fills every step.
- **RND:** DNS 0 and 100 are exact, and 10/50/90 come within 4% over 2,000
  draws. The seed changes on every press.
- **Notes:** the candidate notes are right for all 13 scales across several
  keys, octaves and ranges. OCT 4 + C starts at 60. Picks cover every
  candidate, roughly evenly.
- **GEN's press:** it uses the menu's track and visits each step once in
  order. Trigs, notes and -1 resets are as expected. It does nothing with no
  track or zero length, and the stack is balanced.
- **Menu:** turns clamp to each row's range, the descriptor is complete, and
  SETTINGS + TRACK reaches the opener. The press and release are swallowed
  when it opens; TRACK alone is untouched.
- **On the real stock routines** (with `MODEL_CYCLES_STOCK` set): GEN, run
  through the stock trig and note setters on a stand-in pattern object,
  sets bit 0 on exactly the Euclidean steps. It clears the note-trig
  override, writes scale notes or -1, and leaves the steps past the length
  untouched.

Needs hardware:

1. SETTINGS + TRACK opens *Generate*. SETTINGS / Return close it. TRACK
   alone and TRACK + pad still select tracks.
2. EUC, HIT 4 on a 16-step track: GEN lights steps 1, 5, 9, 13 and they play.
3. RND at DNS 25 / 75: sparse and dense patterns. Each GEN is different.
4. NOT ON, KEY C, SCL MAJ, OCT 4, RNG 1: notes stay within C major from
   the track's normal pitch up an octave. Try another key and octave.
5. With the sequencer running, GEN takes effect on the next pass.
6. Save and reload the project: the generated trigs and notes are kept.
7. Track lengths other than 16 (per-track scale): only that many steps
   are written.
