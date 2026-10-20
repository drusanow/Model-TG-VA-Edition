# DX

*Plaits Edition* (branch `plaits`).

DX is a thirteenth machine: one 6-operator FM voice per track, after the
Yamaha DX7, as Dexed's engine (msfa) models it. It plays DX7 voices: the 32
algorithms, six operators with their envelopes, ratios or fixed frequencies,
detune, output levels and keyboard scaling, feedback, the LFO and the pitch
envelope.

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.**

## Using it

Choose **DX** on any track, like any other machine. Pitch and the trig's note
tune it, and Decay, Attack, Filter and Resonance work as on the other
machines. **Turn Decay up** for DX: the track's Amp Decay comes after the
voice's own envelopes and cuts the note short, so a low Decay turns a pad
into a click, or into silence if its attack is slow.

The voice always plays as its cartridge has it: its algorithm, ratios,
feedback and envelopes are the voice's own, as on a DX7 (or Dexed). The
three macros only move it, from the middle, in directions that keep it the
same sound:

| knob | label | does |
|---|---|---|
| **Color** | **BRIG** | Bright: the modulators' output levels, up to 12 dB down or up. Down is rounder and darker, up brighter and more biting. The carriers, and so the loudness, stay. |
| **Shape** | **AENV** | Amp Env: the carriers' attack and release. Down is snappier (up to 32x faster), up slower: an instant attack swells over about a second at 127, and releases ring longer. |
| **Sweep** | **MENV** | Mod Env: the modulators' attack and first decay, the way the tone moves. Down is a quicker bite and a shorter "pluck", up a slower swell (brass, pads). |
| **Contour** | **VOIC** | the voice: 1 to 63 of the built-in bank, or 1 to 32 of a cartridge you loaded. The parameter page shows its number; turning Contour shows its name. |

Each macro shows **0** in the middle (the voice exactly as it is), and
**-64** to **+63** around it. All four p-lock, slide, take LFOs and are
saved, like any machine knob.

The defaults are the three macros in the middle and voice 1 (Tooropulse).

**The built-in bank.** Until you load a cartridge of your own, Contour picks
among 63 built-in voices (the parameter page shows the number, the knob
screen the name):

| voices | from | |
|---|---|---|
| 1-32 | Legowelt's "Magic DX7" cartridge | Tooropulse, Overcast P, Delicho, ... Swissynth |
| 33-52 | a DX9 cartridge | DX9. 1 to DX9.20 |
| 53-63 | this firmware's own | below |

Both cartridges are free patch sets ([carts/](../carts/README.md)). The
firmware's own voices:

| voice | name | what it is |
|---|---|---|
| 53 | INIT VOICE | a sine (the DX7's starting point) |
| 54 | E.PIANO MT | an electric piano: three pairs, a tine on 14:1 |
| 55 | SYN BASS 1 | a bass: a stack of modulators with feedback |
| 56 | GLASS BELL | a bell: inharmonic ratios, long decays |
| 57 | BRASS SWEL | brass: the modulators swell after the carriers |
| 58 | DRAWBARS 1 | an organ: six carriers on drawbar ratios |
| 59 | FM PLUCK 1 | a short pluck |
| 60 | SOFT PAD 1 | a slightly detuned pad, swelling in over ~0.3 s |
| 61 | MARIMBA 1 | a short 4:1 strike over a sine |
| 62 | CLAV 1 | a bright, nasal clav |
| 63 | SINE BASS | a sine bass with a touch of its octave |

The built-in voices are levelled to play about equally loud (within 1.5 dB,
measured), and their attacks are a few milliseconds, not instant.

**How close it is to a DX7.** Every voice of a third-party 32-voice
cartridge was rendered here and through msfa (Dexed's engine, built
unchanged on the host) at four notes and velocities: 122 of the 128 renders
match within 1 dB in level over time and 3 dB in spectrum, and the rest
within 5 dB in one spectrum window (voices with feedback at 7, where the
sound is near noise).

**Your own cartridges.**

1. **Put them on the Cycles.** Send DX7 `.syx` files to it over USB or MIDI
   IN, from anything that sends SysEx: the
   [DX Cartridge Sender](../flasher/dx.html) page (Chrome or Edge, next to
   the web flasher), a SysEx librarian, or Dexed's "send" to a DX7. The
   Cycles saves each dump in its `/DX7/` folder as
   `<first voice's name> <4 hex digits>.syx`: a 32-voice cartridge, or a
   single voice (which becomes voice 1 of a cartridge, INIT VOICE after it).
   Send them one at a time, a second or two apart (the page does).

   **Through Elektron Transfer** (experimental): Transfer only sends audio,
   so the sender page's **Save for Transfer** wraps each cartridge in a
   48 kHz mono WAV whose samples are the cartridge's bytes. Upload it with
   Transfer in SMP mode and load it on a DX track as below: the loader finds
   the cartridge inside the sample file. (Don't play it on a Sampler track:
   it is noise.) Whether Transfer keeps the bytes exactly needs a try on the
   Cycles.
2. **Load one onto a track.** Select a DX track, press **FUNCTION** +
   **PRESET**, open `DX7` and choose a cartridge: "Cartridge loaded". Contour
   now picks its 32 voices. Files from anywhere on the drive work too: a
   bulk dump, a single voice, or a bare 4096-byte bank.
3. **It stays with the pattern.** The pattern remembers the cartridge (its
   sound's name reads `DX7` and the file's id) and loads it again with the
   pattern or the project, as a Sampler track remembers its sample. Another
   pattern can hold another cartridge. Deleting the file leaves those
   patterns on the built-in cartridge.

**Notes and the gate.** Each trig is a new note on the same voice: the
envelopes start their attack from wherever they are, and the operators
restart their phase if the voice has key sync on - unless they still sound,
where the jump would click. No operator's level jumps: it moves at most a
quarter of full scale a block (0 to full in about 2.7 ms), so instant
attacks and releases don't click. The envelopes hold at their sustain level
(L3) while the note is held, and go to R4/L4 when it is released (the
trig's length, from a MIDI keyboard or a sequencer). A gate shorter than
three blocks (2 ms) is taken for a trig's pulse rather than a note length:
the note then holds on, and the track's Decay shapes it, until the next
trig.

## Limits

- **One voice per track.** Chords need more tracks, as on the other
  machines.
- **Velocity** reaches each operator by its own sensitivity (KVS 0..7), as
  on a DX7: hit harder and a sensitive modulator brightens the sound, a
  sensitive carrier gets louder. On top of that the Cycles still scales the
  whole track's volume by velocity, by the track's own velocity setting, as
  for every machine; set that to none for a DX7's response alone.
- **Algorithms 4 and 6:** their feedback goes from OP4 or OP5 back to OP6
  on a DX7. Here, as in msfa, OP6 feeds back on itself instead.
- **The pitch envelope and the LFO's amplitude modulation** are msfa's
  models, which are close to a DX7's but not measured from one.
- **48 kHz:** the voice is computed at 48 kHz, the rate the stock decimator
  brings every machine to (the DX7 ran at 49 kHz).
- **Envelope timing** follows msfa's rate formula, scaled to this machine's
  blocks (within 0.4%).
- **Cartridges** are loaded whole: a pattern holds one cartridge per
  track, and Contour picks among its voices. Editing a voice on the Cycles
  is limited to the three macros.
- **Receiving** keeps one dump at a time: one that arrives while the last
  is still being written is dropped (a second apart is plenty).

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
  keyboard level scaling) x 32, plus the velocity by the operator's
  sensitivity (msfa's ScaleVelocity: ((KVS (veld[vel / 2] - 239) + 7) >>
  3) x 16), and each envelope level ((scaleoutlevel(L)
  / 2) x 64) + that - 4256, at least 16. OL 99 with L 99 is 3840, full
  scale. One OL step is 0.75 dB.
- **Rates:** (R x 41) / 64 + rate scaling, at most 63.
- **Keyboard scaling** is msfa's: the break point, the two depths and the
  four curves (linear, exponential), in groups of three notes.
- **Frequencies:** a ratio (coarse, 0 = 0.5, times 1 + fine / 100, Q16), or
  a fixed frequency: msfa's log2 of 10^(coarse + fine / 100) Hz (Q24), plus
  the increment's log2 for 48 kHz, turned back into an increment through a
  2^x table (`dx_x2`, interpolated): exact from 1 Hz to 9.8 kHz. Detune is
  msfa's, larger on low notes: the ratio times 1 + (D - 7) `dx_dtn[note]`
  (about 1 cent a step in the middle).

**The velocity** is the OS's own per-track note velocity (`0x40fe4cdc`,
velocity << 8), which the audio side writes from the note-on event before
the block renders; `dx_params` copies it every block and `dx_fill` takes it
at the note. Gains have an octave of headroom (Q14, up to 2.0), so a
sensitive operator at OL 99 goes above full scale at high velocity, as in
msfa.

**Each block** (32 samples at 48 kHz):

- The **LFO** steps (msfa's measured speeds, `dx_lfor`, its delay and six
  waves) and gives a pitch and an amplitude modulation (msfa's: the depth
  times the delay times the inverted wave, Q24).
- The **pitch envelope** steps (msfa's `PitchEnv`: levels `pitchenv_tab[L]`
  << 19 octaves, Q24, from L4 at the note; steps `pitchenv_rate[R]` x 525 a
  block; stage 3 after the key lifts). With the transpose and the LFO it
  makes this block's increment.
- **Each operator** (OP6 first, as the voice stores them):
  - its **envelope** steps once (msfa's `Env::getsample`: rising faster the
    lower it is, from 1716 at least; falling linearly; stage 3 waits for the
    key to lift). A stage whose target is the level already (or an attack
    to level 0) first holds for a time set by its rate, as the DX7 does
    (msfa's ACCURATE_ENVELOPE statics, `dx_stat`). The macros offset the
    rates of the stages they move: Amp Env a carrier's attack and release,
    Mod Env a modulator's attack and first decay, by (64 - k) x 20 / 64
    below the middle (up to +20 steps) and (64 - k) x 40 / 63 above it
    (down to -40, but not below rate 16, a 2.7 s attack, unless the
    voice's own rate is).
  - its **level** gets Bright's offset if it is a modulator ((Color - 64)
    x 8, in 1/256 octave: 16 output-level steps either way), and loses
    msfa's amplitude modulation: level x amf[(AM depth x AMS) >> 42]
    (`dx_amf`, `dx_amsq`).
  - its **gain** is 2^((level - 3840) / 256), moves at most a quarter of
    full scale from last block's, and ramps across the 32 samples.
  - it **renders** 32 samples of sin(phase + input), x gain, into its bus or
    the output, as its algorithm's byte says: read nothing, bus one or bus
    two; write or add to bus one, bus two or the output. A full modulator
    moves its carrier's phase by +-2 cycles (an index of 4 pi). Feedback is
    msfa's: the operator's last two outputs, averaged and scaled by 2^(fb -
    8), added to its own phase.
  - An operator whose gain is under 2/32768 at both ends of the block is
    skipped (its phase still turns, and a bus it would start is cleared).
- The **output** is the carriers' sum x 1/4 (one full carrier at -12 dB, so
  several add up before they clip), written twice to 96 kHz.

**The sine** is an 8192-entry table read with the phase's top 13 bits,
sampled at the half steps so the truncation rounds (spurs below -70 dB). The
two buses are in the resident blob (copyback cache); the cartridges and the
tracks' state are in the sample region (`DX_BASE`, 32 KB under the overlay),
cleared at boot.

**The algorithms** are a table of six bytes each (`dx_alg`), msfa's.
`tests/test_dx.py` reads them by msfa's bus rules and checks every one
against the DX7's diagrams: its carriers, which operator modulates which,
and the feedback.

**Cartridges.**
- **Receiving:** the MIDI task hands every complete SysEx message, from
  USB and MIDI IN, to one handler through the pointer at `0x40112950`, the
  SysEx entry of its dispatch table (stock `0x400812f6`, which answers the
  universal and Elektron messages and drops everything else). `build.py`
  points it at `dx_sysex`, which copies a DX7 bulk dump (4104 bytes) or
  one-voice dump (163) out (the stock frees or reuses its buffer at once)
  into `DX_RXBUF`, and posts a UI-task message. Anything else goes on to
  the stock handler, untouched. Both paths assemble a whole message first
  (MIDI IN up to 32,767 bytes, USB in a chain of 152-byte blocks).
- **Saving:** `dx_rx_write`, in the UI task, writes it to
  `/DX7/<name> <hash>.syx` (making the folder if needed) with the stock
  file calls, as LFO CC's files are written. The name is the first voice's
  (letters, digits, `-` and `.` kept), the hash the data's FNV-1a.
- **Loading:** in the preset browser, the stock preset parse fails on such
  a file, as on a sample; `preset_fail_hook` hands a DX track's file to
  `dx_cload`. It finds the file by its hash in the inode table and reads
  its first 16 KiB as the sample loader does (`DX_FBUF`). The first bulk
  dump, one-voice dump (packed from the 155-byte to the 128-byte format) or
  bare 4096-byte bank becomes the track's cartridge (`DX_CARTS`), 7-bit.
- **The pattern:** `dx_nstore` writes "DX7" + the file's hash into the
  sound's name, saved with the kit as the Sampler's "SMP" token is.
  `dx_probe` (from `led_hook`, every 16th block) reads a track's cartridge
  again when its token changes, or goes back to the built-in one; a file it
  cannot read is not tried again until the token changes.

**The labels and values.** `apply_names` set 7 puts up BRIG / AENV / MENV /
VOIC (Bright, Amp Env, Mod Env, Voice). Color's, Shape's and Sweep's values
print the macro's offset, 0 in the middle (`dx_offmt`; Sweep's formatter is
swapped in and back), Contour's
the voice's number (`dx_vfmt`, in place of PLAITS' formatter while DX's
labels are up), and its knob screen (`dx_draw`, as PLAITS' `mi_draw`) clears
the stock pips and draws the selected track's voice name, centred on x 96
in the big font if it fits 56 pixels, else in the small one.

**Machine plumbing,** widened from 12 to 13 machines as for PLAITS:
`MACH_MAX = 12`, the ParameterPageView list (13 longs), the descriptor
lookups (machine+1 7..13 / raw 6..12), the LFO and Amp Decay gates, the
defaults row, the name table and the icon tables (thirteen entries, 364
bytes), the machine page's second line of markers (five).

**CPU** (`tests/test_dx.py`, `VA_TEST_VERBOSE=1`): the worst block, six
operators sounding with feedback, the LFO and amplitude modulation on every
operator, is **~5,600 instructions**, against PLAITS' heaviest engine at
~6,800.

## Verification

Host-verified in `tests/test_dx.py` (Unicorn ColdFire V4e):

- **Algorithms:**
  - The table read by msfa's rules is the DX7's 32 diagrams: carriers,
    direct modulation edges and the feedback operator.
  - The engine follows it for all 32: each operator alone sounds only if it
    is a carrier; each modulator alone with a carrier changes it only if it
    feeds it directly.
  - A feedback operator writing a bus overwrites it (algorithm 2: OP4's
    output does not reach OP1).
- **Against msfa:** a third-party cartridge's 32 voices, rendered here and
  through msfa built on the host, at notes 36, 60, 72 and 84: as in
  "How close it is to a DX7" above. (The comparison is a script outside
  the suite: the cartridge is not ours to ship.)
- **Operators:**
  - INIT VOICE is a sine at the note within 1.5 cents (65, 262 and 1047 Hz),
    nothing else above -60 dB.
  - Ratios 2, 0.5 and 1.5 are in tune; detune 14 is a few cents sharp; a
    fixed 100 Hz stays 100 Hz at any note; fixed 3.16 Hz, 23.4 Hz and
    9.77 kHz are within 2 cents.
  - OL 99 against OL 91: 6.0 dB (0.75 dB a step).
  - **One modulator into one carrier gives Bessel's sidebands** within 8% at
    OL 99, 91 and 75 (indices 4 pi, 2 pi, pi / 2).
  - Feedback brightens step by step; FB6 has a sawtooth's 2nd and 3rd
    harmonics.
- **Macros:** all three at 64 are the voice exactly; Bright and Mod Env
  leave a voice of carriers alone. Bright brightens step by step, and at
  127 is the modulator 16 levels up (within 1 dB); Mod Env down makes a
  slow modulator attack brighter early and a modulator decay shorter, up
  the opposite; Amp Env up turns an instant attack into a pad (over 0.4 s),
  down makes an attack faster.
- **Pitch envelope:** a held level is msfa's pitch (L 62: 12/32 octave up,
  within 3 cents); it starts at L4, lands on L1, and goes back to L4 after
  the release.
- **Gate:** one- and two-block gates hold the note; three or more release
  it.
- **Envelopes:**
  - A faster R1 rises sooner; Amp Env 0 is over 4x faster than 64, 127
    slower.
  - L2 70 under L1 99 is 21 dB down, and holds.
  - The level holds while the gate is held, and falls after it is released.
  - A new note mid-attack starts from where it was.
- **Velocity:** a carrier with KVS 7 follows msfa within 0.3 dB from
  velocity 40 to 127 (+4.5 dB at 127 over 100, near silence at 1); KVS 0
  stays; a sensitive modulator brightens with velocity; the velocity is
  taken at the note, and `dx_params` reads it from the OS's word.
- **LFO:** PMD 99 / PMS 7 swings the pitch; AMD 99 / AMS 3 swings the level
  over 12 dB; PMD 0 leaves it steady.
- **Voices:** the built-in bank is the generator's (the two cartridges from
  `carts/`, then its own voices), Contour reaches all 63 (or the 32 of a
  loaded cartridge) and copies the voice into the track, and different
  voices sound different. The DX9 voices match msfa as closely as the
  Legowelt ones.
- **Values:** the macros print -64..-1, 0, +1..+63; Contour the voice's
  number, and its knob screen the voice's name, from the built-in or the
  track's own cartridge.
- **Cartridges,** on a stand-in drive: a bulk dump with junk around it,
  a single voice (packed to exactly the generator's 128 bytes, INIT VOICE
  after it), a bare bank, 7-bit masking; a file that is no DX7 cartridge,
  a missing one, a cut-short one. The token is written and read back; the
  probe loads a pattern's cartridge, goes back to the built-in one, and
  does not reread. The preset browser loads onto a DX track and says so.
- **Receiving:** bulk and one-voice dumps from MIDI IN and USB are kept
  and posted; identity, Elektron, wrong-channel, short and other-format
  messages go on to the stock handler; one at a time. The file is
  `/DX7/<name> <hash>.syx`, written whole and closed; a blank name is
  CART.
- **Bounds:** random voices, knobs, notes and gates stay within full scale.
- **Plumbing:** `sampler_pre` routes 12 to `dx_params` (and the gate), the
  render calls `dx_fill`, the labels and the three formatters go in for DX
  and back out for other machines, the defaults row, the level x2.
- **Existing suites** pass, updated for thirteen machines.

Needs hardware:

1. **Machine page:** DX is the thirteenth machine, the fifth marker on the
   second line, with its card and picture.
2. **Parameter page:** BRIG / AENV / MENV / VOIC; the macros show 0 in the
   middle and -64..+63, Contour the voice number and its name. No label
   runs out of its box. PLAITS' Contour and Chord's still show theirs.
3. **The built-in voices** play and sound as described; Pitch and notes
   tune them.
4. **Note length:** with Decay up, a pad (SOFT PAD 1) swells in and holds
   until the next trig; with Decay down it is cut short. From a MIDI
   keyboard, a held key holds the sustain and letting go releases it. (The
   gate is the one thing here the emulator cannot show: tell us what you
   hear.)
5. **Macros:** Bright darkens or brightens E.PIANO MT without changing its
   loudness; Amp Env makes FM PLUCK 1 snappier or a pad; Mod Env makes
   BRASS SWEL swell slower or bite quicker. None of them breaks a voice.
6. **Cartridges:** send a cartridge with the DX Cartridge Sender (or a
   librarian), then FUNCTION + PRESET on a DX track: it is in `/DX7/`, it
   loads ("Cartridge loaded"), and Contour shows its voice names. Save the
   project, load another, come back: the cartridge is still there. Another
   pattern can have another. Elektron Transfer still connects and works.
7. **Velocity:** E.PIANO MT and FM PLUCK 1 with soft and hard trigs (or
   velocity p-locks, or a MIDI keyboard): harder is brighter, not only
   louder.
8. **CPU page:** six DX tracks playing E.PIANO MT chords across tracks, with
   no dropouts.

## Credits and licence

DX follows the DX7 engine of **msfa** ("Music Synthesizer for Android",
Raph Levien), as kept in **Dexed** (Pascal Gauthier). Dexed itself is
GPL-3.0 and none of its code is used; its copy of msfa is under the Apache
License 2.0. msfa's algorithms are followed, rewritten in ColdFire assembly,
and some of its numbers are used as tables (`tools/gen_dx_tables.py`): the
algorithm routing, scaleoutlevel's low end, the exponential keyboard
scaling curve, the pitch modulation sensitivities, the velocity curve, the
LFO's measured speeds, the pitch envelope's rates and levels, the amplitude
modulation sensitivities and curve, the detune's formula and the
envelope's measured static times.

msfa is released under the Apache License 2.0:

> Copyright 2012-2013 Google Inc.; Copyright 2016-2025 Pascal Gauthier
> (Dexed's changes to `env.cc` and `dx7note.cc`).
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

The built-in bank's first 52 voices are Legowelt's "Magic DX7" cartridge
and a DX9 cartridge, free patch sets ([carts/](../carts/README.md)); the
rest are this project's own. "DX7" and "Yamaha" are Yamaha
Corporation's. This firmware is not made, supported or endorsed by Yamaha,
by the authors of msfa or Dexed, or by Elektron.
