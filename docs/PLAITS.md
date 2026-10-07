# PLAITS

*Plaits Edition only* (branch `plaits`). This is a separate firmware build,
so the VA Edition stays as it is.

PLAITS is one machine with eight sound engines and four knobs. It is after
Mutable Instruments **Plaits**, Emilie Gillet's macro-oscillator module. It
sits after the drums on the machine page: the twelfth machine, index 11.

Each engine is a whole-number remake of the algorithm of the Plaits engine it
is named after; Plaits' code itself is not used. The Cycles' processor has no
floating point, and only a fraction of Plaits' memory. Some of the tables are
Plaits' own, computed from Plaits' formulas: the waveshapers, the wavefolder,
the FM ratios and the chords. See [Credits and licence](#credits-and-licence).

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.**

## Using it

Choose **PLAITS** on any track like any other machine. It needs no sample.
- **Pitch** and the trig's note are Plaits' FREQUENCY: note 60 is middle C.
- **Decay**, **Attack**, **Filter** and **Resonance** work as on the VA. The
  amp envelope plays the part of Plaits' low-pass gate.

The four machine knobs are Plaits' controls:

| knob | label | Plaits |
|---|---|---|
| **Color** | (per engine) | HARMONICS |
| **Shape** | (per engine) | TIMBRE |
| **Sweep** | (per engine) | MORPH |
| **Contour** | ENG | the engine, shown by name |

All four p-lock, slide, take LFOs and are saved. So the engine itself can
be p-locked per step, or swept by an LFO.

**Choosing the engine.** Contour's value is shown as the engine's name
(WSHP, FM, GRAN, PD, CHIP, NOIS, PART, STRG), not a number. Each click of
the knob is one engine, however fast it is turned, and it stops at either
end. A p-lock or an LFO on Contour picks engines too: underneath it is still
0..127, 16 values an engine.

The other three knobs take that engine's names for HARMONICS, TIMBRE and
MORPH. Every name is kept to 9 characters and every short label to 4, short
of the stock ones, so none runs past its box (`build.py` checks this).

| Contour | engine | Color | Shape | Sweep |
|---|---|---|---|---|
| WSHP | waveshaping | **SHPE** Waveshape | **FOLD** Fold | **ASYM** Asymmetry (triangle to ramp) |
| FM | 2-op FM | **RATI** Ratio | **INDX** Mod Index | **FDBK** Feedback |
| GRAN | formants (grains) | **FRM2** Formant 2 (its ratio) | **FRMT** Formant | **WDTH** Width (grain shape) |
| PD | phase distortion | **RATI** Ratio | **DIST** Distort | **ASYM** Asymmetry |
| CHIP | chiptune arpeggio | **CHRD** Chord | **ARP** Arpeggio (pattern) | **SYNC** Sync (synced ratio) |
| NOIS | filtered noise | **TYPE** Filt Type (LP - BP - HP) | **CLK** Clock | **RESO** Resonance |
| PART | particles (dust) | **SPRD** Spread | **DENS** Density | **RESO** Resonance |
| STRG | plucked string | **STIF** Stiffness (bridge / dispersion) | **BRIG** Bright | **DAMP** Damping |

The defaults are Color, Shape and Sweep at 64 and the WSHP engine. Each
engine starts from rest whenever it is selected.

### The engines

- **WSHP** (Plaits' waveshaping engine)
  - A triangle (MORP 0) that leans into a ramp (127).
  - It goes through one of Plaits' five waveshapers, blended (SHPE: inverse
    tan, inverse sin, linear, bump, double bump).
  - Then Plaits' wavefolder: FOLD sets how hard.
- **FM**
  - A sine carrier, phase-modulated by a sine at one of Plaits' quantized
    ratios (RATI: 0.5 to 8, with flat spots on the musical ratios).
  - INDX: the index, up to two cycles.
  - FDBK above the middle feeds the modulator back on itself (harsher).
    Below the middle, the carrier bends the modulator's frequency.
- **GRAN** (Plaits' grain engine)
  - Two grainlets: a window at the note's pitch, filled with a sine at a
    formant.
  - FRMT sets the first formant, from note 24 to 108, whatever note is
    played: a vowel-like, talking tone.
  - FRM2 sets the second formant's ratio to it, -24 to +24 semitones. Below
    the middle it also lets the carrier bleed through.
  - WDTH shapes the window.
- **PD** (Plaits 1.2's phase distortion engine)
  - Casio CZ-style: a cosine whose phase is bent by a triangle at RATI's
    ratio, hard-synced to the note.
  - DIST: the amount.
  - ASYM: the triangle's and the phasor's asymmetry.
- **CHIP** (Plaits 1.2's chiptune engine, as Plaits plays it when
  triggered)
  - **Each trig plays the next note** of an arpeggio over one of Plaits'
    eleven chords (CHRD: OCT, 5, sus4, m, m7, m9, m11, 69, M9, M7, M).
  - ARP: Plaits' twelve patterns: up, down, up-down and random, over 1, 2
    or 4 octaves.
  - The voice is a square hard-synced to the note: SYNC sets its ratio,
    0.51x to 1x below the middle, then up to about 5x.
  - Before the first trig it plays the root.
- **NOIS** (Plaits' noise engine)
  - White noise, sampled and held at a clock: CLK, from a few Hz up to
    raw noise.
  - Then a resonant state-variable filter at the note's pitch. TYPE
    sweeps low-pass, band-pass, high-pass; RESO goes from Q 0.5 to 512.
  - Plaits' gain compensation keeps the level even.
- **PART** (Plaits' particle engine)
  - Random impulses (DENS: from a few a second to a crackle).
  - Each impulse rings one of three resonant band-passes. The band-pass is
    retuned at random about the note when struck (SPRD: up to +-48
    semitones).
  - RESO above the middle makes them ring (Q up to ~1000).
  - Then a low-pass at the note. Pitch it low for dark rumbles, high for
    rain.
- **STRG** (Plaits' string engine, one string)
  - A Karplus-Strong string struck on each trig by a short burst of noise.
  - BRIG: the strike's and the string's brightness.
  - DAMP: how long it rings. Above ~95% it rings forever.
  - STIF below 24% curves the bridge: a buzz, sitar-like.
  - STIF above 26% adds dispersion: stiffer, bell-like overtones.
  - Between those it is a clean string.
  - Lowest note: about 23 Hz.

### Differences from Plaits

- **One output.** Plaits' AUX output (the sub, the second variant of each
  engine) is not there.
- **Trigs, not a TRIG input.** CHIP steps its arpeggio and STRG is struck
  on every trig, as on a patched Plaits. GRAN, WSHP, FM, PD and NOIS run
  continuously and the amp envelope shapes them. NOIS's clock does not
  resync on a trig.
- **PART** has three filters to Plaits' six, a one-pole low-pass after
  them, no diffuser below the middle of RESO (Q 1.5 there), and its input
  gain is held to 16.
- **STRG** has a one-pole damping filter, where Plaits has a two-pole. Its
  dispersion is a one-pole allpass, where Plaits uses a long allpass delay
  line.
- **Not included:** Plaits' other engines.
  - **VA**, the bass drum, snare and hi-hat: this firmware has its own.
  - **Wavetable:** its data alone is ~100 KB.
  - **Speech:** data and CPU.
  - **Modal, additive, chords, swarm and six-op FM:** each is more CPU
    than a track can spend.

## How it works

The code is in `src/plaits.inc`, the tables are from
`tools/gen_mi_tables.py` (`src/mi_tables.inc`) and the icons are from
`tools/gen_mi_icons.py`.

**Routing.** PLAITS takes exactly the drums' route ([DRUMS.md](DRUMS.md#how-it-works)):
- `sampler_pre` sends machines 7..11 to `va_pre`. For machine 11,
  `va_pre` stores the four dials (`mi_params`) and marks a new note
  (`dr_trig`).
  - The dials are the Sampler's ids, at trackData words +22..+28.
  - They are kept as the raw words, 0..32512, not 0..127. LFOs and slides
    move them smoothly.
- `sampler_render` calls `mi_fill(track, step, 11)`. It writes the same
  64-sample window, then the same tail.
- `mi_fill` turns the step into the note's phase increment, as the VA does.
  It picks the engine from Contour (`mi_eng`) and clears the track's
  128-byte state when the engine changes. Then it jumps to the engine.

**Rates.** WSHP, FM, GRAN, PD and CHIP run at the render's 96 kHz. NOIS,
PART and STRG run at 48 kHz, the rate the stock decimator brings every
render down to. They write each sample twice. That:
- halves their work;
- doubles their filters' coefficients, so the integer states keep their
  precision at low cutoffs (16-bit-plus fractions, rounded, held within
  range).

**DSP**, all integer:
- **Sines:** a 256-entry table with linear interpolation (better than -80
  dB).
- **Pitches:**
  - Absolute pitches (GRAN's formants, NOIS's clock, PART's density) come
    from a note-to-increment table, interpolated between semitones.
  - Ratios come from a 2^(n/12) table.
- **Filters:** Plaits' (stmlib's) state-variable filter, with g = tan(pi
  f / fs) from a short series.
- **STRG's tuning:**
  - The line is one period long, less the damping filter's phase delay at
    the note, worked out exactly each block (an arctangent).
  - The DC blocker's pole is close enough to 1 that its lead is a few
    cents.
  - It stays within about 6 cents of the note, 65 Hz to 523 Hz, at every
    setting.
- **STRG's delay line** is Pluck's (the Sampler's Karplus-Strong mode),
  8 KiB a track. A track cannot be both at once. Each one zeroes the line
  whenever the other has used it since.

**Level.** The render path peaks at half a stock machine's level. So after
the amp envelope `dr_punch` doubles PLAITS cleanly, held at full scale, as
it makes up the drums (without the drums' soft clip).

**Machine plumbing:** the sites in [DRUMS.md](DRUMS.md#how-it-works), widened
from 10 to 11:
- `MACH_MAX = 11` and `MI_MACH = 11`.
- The ParameterPageView list (12 longs).
- The descriptor hooks and lookups (machine+1 7..12 / raw 6..11).
- The LFO and Amp Decay gates.
- `mc_commit_hook`: defaults, `dr_defaults`' fourth row.
- `apply_names` set 6.
- The name table and icons (twelve entries).
- The machine page's second marker line (four markers).

**Labels.** `apply_names` set 6 fills `mi_swap` from `mi_labs` for one
engine. `mi_watch` keeps that the selected track's engine: it runs from
`led_hook` once per audio block, and relabels when the engine changes.

**Contour by name.** The page prints a value through the formatter at
+0x20 of the parameter's 100-byte UI record (0x40a71754 + id x 100); see
`an_done`.
- While set 6 is up, `mi_fmtswap` makes Contour's (id 0x4a) formatter
  `mi_eng_fmt`. That prints the engine's name the way the stock list
  formatters print theirs: `sprintf(buf, "%s", name)`.
- Every other set puts the stock formatter back. The same id is Chord's
  Contour and the Sampler's Resonance.
- `fine_hook` sends id 0x4a to `mi_eng_step`. On a PLAITS track any turn
  lands on the middle of the next or previous engine.

**Space.** The blob grows to ~110 KB: seven of the filesystem's sixteen
16 KiB cache blocks, one more than the VA Edition. Nine stay the
filesystem's.

**CPU** (`tests/test_plaits.py`, `VA_TEST_VERBOSE=1`): the worst block of
`mi_fill` (instructions), against the VA's ~3,700 and the drums' ~6,000:

| engine | instructions a block |
|---|---|
| WSHP | ~3,850 |
| FM | ~3,880 |
| GRAN | ~5,840 |
| PD | ~5,020 |
| CHIP | ~880 |
| NOIS | ~3,020 |
| PART | ~6,830 |
| STRG | ~3,550 (plus ~2,000 once, zeroing its line) |

An idle PLAITS track costs what an idle stock track costs. 0.25 s after
silence the track is skipped until its next trig.

## Verification

Host-verified in `tests/test_plaits.py` (Unicorn ColdFire V4e).

- **WSHP:**
  - The fundamental is the note.
  - FOLD brightens it step by step.
  - SHPE changes the waveshaper.
  - ASYM adds even harmonics.
- **FM:**
  - Index 0 is a pure sine at the note: nothing else above -60 dB in the
    audio band.
  - INDX brightens it.
  - At ratio 2 every partial is a harmonic.
  - Feedback either way changes the sound.
- **GRAN:**
  - The note is the fundamental.
  - FRMT moves the spectrum's centroid by over 2x.
  - WDTH changes the grain.
  - It has no DC.
- **PD:**
  - Amount 0 is a pure cosine at the note.
  - DIST brightens it.
  - The note stays the fundamental when distorted.
- **CHIP:**
  - The arpeggio steps per trig as Plaits' arpeggiator does (up, down over
    two octaves, up-down, random never repeating).
  - The chord ratios are Plaits'.
  - The square is at the note.
- **NOIS:**
  - TYPE moves the energy from low to high.
  - A resonant band-pass peaks at the note.
  - A slow clock is smoother.
- **PART:**
  - Density raises the energy.
  - No DC.
  - A resonant setting rings at the note.
- **STRG:**
  - In tune within 20 cents (measured 6) at 65, 131, 262 and 523 Hz over
    five settings.
  - DAMP lengthens it, and at the top it rings on.
  - BRIG brightens it.
  - It zeroes its line after Pluck has used it, and marks Pluck to zero it
    again.
- **All:**
  - Output within full scale for random settings, pitches and trigs.
  - A new engine starts from rest.
  - CPU bounded (the table above).
- **Plumbing:**
  - `sampler_pre` routes machine 11 to `mi_params`, a Sampler voice and
    the dials.
  - The render calls `mi_fill` and then the tail.
  - The labels change per engine; every name is 9 characters or fewer and
    every short label 4. They go back to stock for other machines.
  - `mi_watch` relabels as the engine turns.
  - Contour's value prints the engine's name ("%s", the name for each of
    0..32767), and the stock formatter comes back for other machines.
  - Contour steps one engine a click, slow or fast, held at both ends; on
    Chord and the Sampler it is the stock step.
  - The defaults are written.
  - Level x2, clean, held.
  - The LFO and Amp Decay gates take machine 11.
  - The machine list, the names and the twelve icon entries.
- **Existing suites:** the VA, drums, LFO, generator and regression suites
  still pass, updated for twelve machines.

Needs hardware:

1. **Machine page.**
   - PLAITS is the twelfth machine, the fourth marker on the second line.
   - It shows its name, card and picture.
2. **Parameter page.**
   - Contour's value shows the engine's name, and each click steps one
     engine.
   - The other three labels change with it (if they only change on leaving
     and re-entering the page, tell us).
   - No name or label runs out of its box.
   - Chord's Contour and the Sampler's Resonance still show numbers.
3. **Each engine sounds as described.**
   - Pitch and notes tune it.
   - Decay shapes it.
   - The four knobs p-lock and take LFOs.
4. **CHIP:** each trig steps the arpeggio.
5. **STRG:**
   - Each trig plucks it, and it is in tune.
   - Switch a track from a Sampler in Pluck mode to PLAITS STRG and back:
     no noise burst from the other's leftovers.
6. **CPU page:** six PLAITS tracks on PART (the heaviest) with no dropouts.
7. **Saving:** sample loading and project saving still work normally, with
   one less filesystem cache block.

## Credits and licence

PLAITS is after Mutable Instruments Plaits by Emilie Gillet
(https://github.com/pichenettes/eurorack/tree/master/plaits).
- **Its algorithms** are followed engine by engine.
- **Its tables:** `tools/gen_mi_tables.py` computes the waveshaper,
  wavefolder, FM-ratio and chord tables with Plaits' formulas.

Plaits is released under the MIT licence:

> Copyright 2016 Emilie Gillet.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.

"Mutable Instruments" and "Plaits" are Emilie Gillet's. This firmware is
not made, supported or endorsed by Mutable Instruments, or by Elektron.
