# DX

*Plaits Edition* (branch `plaits`).

DX is a thirteenth machine: one 6-operator FM voice per track, after the
Yamaha DX7, as Dexed's engine (msfa) models it. It plays DX7 voices: the 32
algorithms, six operators with their envelopes, ratios or fixed frequencies,
detune, output levels and keyboard scaling, feedback, the LFO and the pitch
envelope.

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.** Loading your own cartridges is
the next step; this build plays the built-in one.

## Using it

Choose **DX** on any track, like any other machine. Pitch and the trig's note
tune it, and Decay, Attack, Filter and Resonance work as on the other
machines. The four machine knobs:

| knob | label | does |
|---|---|---|
| **Color** | **ALGO** | the algorithm. At 0 (**VCE**) the voice's own; turning up picks **A1** to **A32**, about four steps each. The routing changes at once. |
| **Shape** | **ENV** | every envelope at once: at 64 as the voice has it, turning down makes them faster (plucky), up slower (pad), up to four times either way at the ends. |
| **Sweep** | **FBHM** | below the middle, the feedback, **FB0** to **FB7**. At 64 (**VCE**) the voice's own. Above the middle, the modulators' frequencies go up together, by up to two octaves (**+0** to **+24** semitones): new, often clangy, overtones. |
| **Contour** | **VOIC** | the voice, 1 to 32 of the cartridge. The page shows its name. |

All four p-lock, slide, take LFOs and are saved, like any machine knob. So
the algorithm, the envelope speed and the voice can change from step to step.

The defaults are the voice's own algorithm, envelopes and feedback, and
voice 1.

**The built-in cartridge.** Until you load one of your own, DX plays a
cartridge made for this firmware, so no Yamaha voice is included:

| voice | name | what it is |
|---|---|---|
| 1 | INIT VOICE | a sine (the DX7's starting point) |
| 2 | E.PIANO MT | an electric piano: three pairs, a tine on 14:1 |
| 3 | SYN BASS 1 | a bass: a stack of modulators with feedback |
| 4 | GLASS BELL | a bell: inharmonic ratios, long decays |
| 5 | BRASS SWEL | brass: the modulators swell after the carriers |
| 6 | DRAWBARS 1 | an organ: six carriers on drawbar ratios |
| 7 | FM PLUCK 1 | a short pluck |
| 8 | SOFT PAD 1 | a slow, slightly detuned pad |
| 9-32 | INIT VOICE | |

**Notes and the gate.** Each trig is a new note on the same voice: the
envelopes start their attack from wherever they are (no click), and the
operators restart their phase if the voice has key sync on. The envelopes
hold at their sustain level (L3) while the note is held, and go to R4/L4
when it is released (the trig's length).

## Limits

- **One voice per track.** Chords need more tracks, as on the other
  machines.
- **No velocity:** the voices' key velocity sensitivity is ignored for now.
- **Algorithms 4 and 6:** their feedback goes from OP4 or OP5 back to OP6
  on a DX7. Here, as in msfa, OP6 feeds back on itself instead.
- **The pitch envelope** is a simpler curve than the DX7's (+-4 octaves,
  straight segments), and **the LFO's amplitude modulation** depth is an
  approximation (at most 24 dB). Most voices use neither much.
- **48 kHz:** the voice is computed at 48 kHz, the rate the stock decimator
  brings every machine to (the DX7 ran at 49 kHz).
- **Envelope timing** follows msfa's rate formula, scaled to this machine's
  blocks (within 0.4%).
- **Loading cartridges** is not in this build yet. The plan: send DX7
  `.syx` files to the Cycles, see them in the preset browser, and load one
  with **FUNCTION** + **PRESET** onto a DX track; the pattern then keeps it.

## How it works

The code is in `src/dx.inc` (in the [overlay](INTERNALS.md#how-the-image-is-put-together),
run from the sample region), the tables in `src/dx_tables.inc` from
`tools/gen_dx_tables.py`, the icons from `tools/gen_dx_icons.py`.

**Routing.** As PLAITS: `sampler_pre` sends machine 12 to `va_pre`, which
stores the four dials (`dx_params`) and the gate (`dx_gate`: the render
loop's trigger flag for the track this block). `sampler_render` calls
`dx_fill(track, step, 12)`, which writes the same 64-sample window, then the
same tail. `dr_punch` doubles its level, as PLAITS'.

**The voice** is the DX7's packed 128-byte format (VMEM), copied into the
track's state when Contour picks it (`+64`). On a new note (`dx_noteon`),
`dx_setup` turns it into each operator's numbers for that note, as msfa's
`Dx7Note::init` does:

- **Levels** are in 1/256 octave. The output level is (scaleoutlevel(OL) +
  keyboard level scaling) x 32, and each envelope level ((scaleoutlevel(L)
  / 2) x 64) + that - 4256, at least 16. OL 99 with L 99 is 3840, full
  scale. One OL step is 0.75 dB.
- **Rates:** (R x 41) / 64 + rate scaling, at most 63.
- **Keyboard scaling** is msfa's: the break point, the two depths and the
  four curves (linear, exponential), in groups of three notes.
- **Frequencies:** a ratio (coarse, 0 = 0.5, times 1 + fine / 100, Q16), or
  a fixed frequency (10^(coarse + fine / 100) Hz, as a note). Detune moves
  the ratio about 0.9 cent a step.

**Each block** (32 samples at 48 kHz):

- The **LFO** steps (msfa's speeds, delay and six waves) and gives a pitch
  and an amplitude modulation. The **pitch envelope** steps. With the
  transpose they make this block's increment.
- **Each operator** (OP6 first, as the voice stores them):
  - its **envelope** steps once (msfa's `Env::getsample`: rising faster the
    lower it is, from 1716 at least; falling linearly; stage 3 waits for the
    key to lift). Shape offsets every rate by (64 - Shape) / 4.
  - its **gain** is 2^((level - 3840) / 256), and ramps from last block's
    across the 32 samples.
  - it **renders** 32 samples of sin(phase + input), x gain, into its bus or
    the output, as its algorithm's byte says: read nothing, bus one or bus
    two; write or add to bus one, bus two or the output. A full modulator
    moves its carrier's phase by +-2 cycles (an index of 4 pi). Feedback is
    msfa's: the operator's last two outputs, averaged and scaled by 2^(fb -
    8), added to its own phase.
  - An operator whose gain is under 2/32768 at both ends of the block is
    skipped (its phase still turns, and a bus it would start is cleared).
- The **output** is the carriers' sum x 3/8, written twice to 96 kHz.

**The sine** is an 8192-entry table read with the phase's top 13 bits,
sampled at the half steps so the truncation rounds (spurs below -70 dB). The
two buses are in the resident blob (copyback cache); the cartridges and the
tracks' state are in the sample region (`DX_BASE`, 32 KB under the overlay),
cleared at boot.

**The algorithms** are a table of six bytes each (`dx_alg`), msfa's.
`tests/test_dx.py` reads them by msfa's bus rules and checks every one
against the DX7's diagrams: its carriers, which operator modulates which,
and the feedback.

**The labels and values.** `apply_names` set 7 puts up ALGO / ENV / FBHM /
VOIC. Color's value prints the algorithm (`dx_algfmt`), Sweep's the feedback
or the shift (`dx_swfmt`, Sweep's formatter swapped in and back), Contour's
the selected track's voice name (`dx_vfmt`, in place of PLAITS' formatter
while DX's labels are up).

**Machine plumbing,** widened from 12 to 13 machines as for PLAITS:
`MACH_MAX = 12`, the ParameterPageView list (13 longs), the descriptor
lookups (machine+1 7..13 / raw 6..12), the LFO and Amp Decay gates, the
defaults row, the name table and the icon tables (thirteen entries, 364
bytes), the machine page's second line of markers (five).

**CPU** (`tests/test_dx.py`, `VA_TEST_VERBOSE=1`): the worst block, six
operators sounding with feedback, the LFO and Sweep's shift, is **~5,300
instructions**, against PLAITS' heaviest engine at ~6,800.

## Verification

Host-verified in `tests/test_dx.py` (Unicorn ColdFire V4e):

- **Algorithms:**
  - The table read by msfa's rules is the DX7's 32 diagrams: carriers,
    direct modulation edges and the feedback operator.
  - The engine follows it for all 32 (picked by Color): each operator alone
    sounds only if it is a carrier; each modulator alone with a carrier
    changes it only if it feeds it directly.
  - Color 0 is the voice's own algorithm.
- **Operators:**
  - INIT VOICE is a sine at the note within 1.5 cents (65, 262 and 1047 Hz),
    nothing else above -60 dB.
  - Ratios 2, 0.5 and 1.5 are in tune; detune 14 is a few cents sharp; a
    fixed 100 Hz stays 100 Hz at any note.
  - OL 99 against OL 91: 6.0 dB (0.75 dB a step).
  - **One modulator into one carrier gives Bessel's sidebands** within 8% at
    OL 99, 91 and 75 (indices 4 pi, 2 pi, pi / 2).
  - Feedback brightens step by step; FB6 has a sawtooth's 2nd and 3rd
    harmonics. Sweep 64 keeps the voice's.
  - Sweep at the top moves the modulator up two octaves, the carrier stays.
- **Envelopes:**
  - A faster R1 rises sooner; Shape 0 is over 4x faster than 64, 127 slower.
  - L2 70 under L1 99 is 21 dB down, and holds.
  - The level holds while the gate is held, and falls after it is released.
  - A new note mid-attack starts from where it was.
- **LFO:** PMD 99 / PMS 7 swings the pitch; AMD 99 / AMS 3 swings the level
  over 12 dB; PMD 0 leaves it steady.
- **Voices:** the built-in cartridge is the generator's, Contour picks the
  voice (copied into the track), and different voices sound different.
- **Values:** Color prints VCE, A1..A32; Sweep FB0..FB7, VCE, +0..+24;
  Contour the voice's name, from the built-in or the track's own cartridge.
- **Bounds:** random voices, knobs, notes and gates stay within full scale.
- **Plumbing:** `sampler_pre` routes 12 to `dx_params` (and the gate), the
  render calls `dx_fill`, the labels and the three formatters go in for DX
  and back out for other machines, the defaults row, the level x2.
- **Existing suites** pass, updated for thirteen machines.

Needs hardware:

1. **Machine page:** DX is the thirteenth machine, the fifth marker on the
   second line, with its card and picture.
2. **Parameter page:** ALGO / ENV / FBHM / VOIC; Color shows VCE / A1..A32,
   Sweep FB0..FB7 / VCE / +n, Contour the voice name. No label runs out of
   its box. PLAITS' Contour and Chord's still show theirs.
3. **The built-in voices** play and sound as described; Pitch and notes
   tune them.
4. **Note length:** a long trig holds a voice's sustain (try SOFT PAD 1 or
   DRAWBARS 1), a short one releases it. (If notes never release, or always
   release at once, tell us: the gate is the one thing here the emulator
   cannot show.)
5. **Color** turned while playing changes the routing at once; **Shape**
   makes FM PLUCK 1 snappier or a pad; **Sweep** adds feedback or clang.
6. **CPU page:** six DX tracks playing E.PIANO MT chords across tracks, with
   no dropouts.

## Credits and licence

DX follows the DX7 engine of **msfa** ("Music Synthesizer for Android",
Raph Levien), which **Dexed** (Pascal Gauthier, GPL-3.0) is built on. Dexed's
code is not used. msfa's algorithms are followed, rewritten in ColdFire
assembly, and some of its numbers are used as tables: the algorithm
routing, scaleoutlevel's low end, the exponential keyboard scaling curve and
the pitch modulation sensitivities (`tools/gen_dx_tables.py`).

msfa is released under the Apache License 2.0:

> Copyright 2012 Google Inc.
>
> Licensed under the Apache License, Version 2.0 (the "License"); you may not
> use this file except in compliance with the License. You may obtain a copy
> of the License at http://www.apache.org/licenses/LICENSE-2.0
>
> Unless required by applicable law or agreed to in writing, software
> distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
> WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
> License for the specific language governing permissions and limitations
> under the License.

The built-in voices are this project's own. "DX7" and "Yamaha" are Yamaha
Corporation's. This firmware is not made, supported or endorsed by Yamaha,
by the authors of msfa or Dexed, or by Elektron.
