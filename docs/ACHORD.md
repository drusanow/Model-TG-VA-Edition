# ACHORD

*Plaits Edition.* Machine 14 (the fifteenth): a paraphonic, analogue-style
chord machine with keys, scales and progressions. Written for this project
(MIT): `src/achord/achord.c`, glue `src/achord_glue.inc`, tables
`tools/gen_achord.py`, icons `tools/gen_achord_icons.py`. How to play it:
[USER_GUIDE.md](USER_GUIDE.md#achord). Its features follow the chord machines
of today (HiChord's chords by scale degree, the J-6's progressions and
phrases) and its sound the string and chord synths of the late seventies; no
code of any of them is in it.

## The sound

Paraphonic, as those machines were: four voices into one filter and one
envelope, which keeps it at about the cost of one stock voice.

- **Oscillators.** Each voice is a saw and a pulse on one phase, both
  band-limited with polyBLEP (`saw`, `sqr`: the residual of the jump, one
  compare a sample to know whether it is near), Q14. Shape mixes them: saw at
  0, saw and square equally at 64, square at 96, then the pulse's width moves
  with a 0.7 Hz triangle, up to +-40 % of the cycle at 127. A square
  sub-oscillator an octave under the lowest voice (SUB).
- **Drift.** Each voice sits a few cents from the others (a fixed spread of
  -3, +2, -1, +4 cents x DRIFT / 5) and wanders slowly (a random walk, a step
  a block, within +-1.5 cents x DRIFT).
- **Filter.** A 4-pole low-pass ladder: four TPT one-poles (G = g / (1 + g),
  `ac_g`, 256 cutoffs from 20 Hz to 18 kHz even in pitch), the resonance fed
  back from the last stage (a sample late, less towards the top so it stays
  stable), the input through a soft knee. Cutoff = Color x 2 + the envelope
  x Sweep (up to 160 steps, ~6 octaves) + the root (half the keyboard's rate).
  Some of the level the resonance takes is given back.
- **Envelope.** ~2 ms up on each trig, then down by ENV DECAY (30 ms to 4 s to
  1/e, `ac_dec`), a step a block.
- **Chorus.** One delay line (256 samples, kept twice so reads never wrap),
  read 3.2 ms back, the time moved by a triangle: I 0.51 Hz +-1.6 ms, II 0.86
  Hz +-1.9 ms, I+II 9.8 Hz +-0.35 ms; the line added to the dry sound. Mono,
  as the machine's output.
- **Level.** x 2.5 to 16 bits and a soft limiter above -2 dB; the default
  triad peaks around -2 dBFS.

## The chords

`ac_chord`: from the note (the track's step, rounded to its note with
`ac_semi`; the cents past it kept as a fine tuning) and the settings.

- **FREE** (SCALE at FREE, PROG off): the note is the root, Contour the type,
  sixteen types (`free_types`).
- **In a key**: the scale (`scales`: major, minor, dorian, mixolydian,
  harmonic minor, phrygian, lydian) and a degree; the chord is stacked from
  the scale's own notes (`key_types`: triad, 7th, 9th, add9, sus2, sus4,
  6th, power), so its quality is the key's (ii is minor, vii diminished, V of
  harmonic minor a dominant 7th). The degree comes from the note: SNAP (the
  note down into the scale) or DEGREES (a semitone a degree, from middle C
  as I).
- **Progressions** (PROG): sixteen of four chords (`progs`, each with the mode
  it is in when SCALE is FREE) or USER, up to eight degrees from the settings.
  ADVANCE TRIG: the next chord a trig, back to the first when a trig falls on
  the pattern's first step (the sequencer's step counter 0x40a78840, which
  holds the next step: model_tg.s / mutesolo.inc). 1, 2, 4 BARS: the chord of
  the step's bar (16, 32, 64 steps a chord). The note transposes the
  progression.
- Intervals are kept in the order they give way: root, third, seventh, the
  colour (9th, 6th, added), fifth - four voices take the first four, and with
  a BASS the root (now below) gives way first.

`ac_voice`: CLOSE (root position within an octave), OPEN (the second note up
an octave), DROP 2 (the second from the top down), WIDE (the intervals as they
stand, the third an octave up), or AUTO: of every inversion of the close
chord, an octave down, as is or up, with its lowest note within 9 semitones of
the root, the one whose notes move least from the last chord's. Then the bass.
STRUM delays each voice's move to its new note by 2 ms a step, low to high.

The chord is made again on a trig, or when the note, Contour or any setting
changes (a sum of the settings' bytes).

## The settings

24 bytes a track in each kit's LFO 3/4 block (`lfo2.inc`: `AC_OFF` = 364, the
block now 508 bytes; `L34_KITS` 0xC000), each value stored + 1 so a cleared
block is the defaults. They go where the kit goes: copied with it, cleared
with a track's sound (`l34_snd_reset`) and a new kit (`lcc_kclr`), and saved
in the kit's MIDI CC file (`lfocc.inc`): 384 bytes now (CC, LXR's edits,
ACHORD's), 48 with none of them, and the 240-byte files written before ACHORD
still load (each file is checked by its hash over its own length).

The page: SETTINGS + PRESET opens mm_open's page of kind MM_LXR (LXR's voice
menu's), whose handlers (`lx_m_*`) pass an ACHORD track's events to `ac_m_*`.
Contour's value names the chord type or size (`ac_cfmt`, installed for label
set 9 by `mi_fmtswap`).

## CPU

`tests/test_achord.py` (`VA_TEST_VERBOSE=1`), instructions a block: the
default (a triad, saw, sub, chorus I) ~5,700; four voices with saw and square
and the chorus ~7,900 (a stock voice ~7,600, LXR 7,900-9,150). An idle track
costs nothing (the amp stage's idle skip, as every machine). The voice loop
renders one voice at a time into the block's sums (fewer registers), the
filter keeps its state in registers, the output stage is folded into the
filter's or the chorus's loop.

## Under load: 32 kHz

When the CPU guard picks it (`gd_eco`, guard.inc), the track renders at 32
kHz: 21 or 22 samples a block (`eco_n`, eco_rs.c, the resampling state DX
uses), the increments x 1.5, the filter's cutoffs from `ac_g32` (held under
14.4 kHz), the chorus's times two thirds as many samples; its envelope, drift
and triangles step once a block as always. The samples are made up to the
block's 32 frames linearly in the same pass as the output stage (at 48 kHz:
the samples two late, so a change of rate carries on in time). It goes down
at once, cleanly, and back up at its next trig. ~6,600 -> ~5,600 instructions
for the default sound, ~8,300 -> ~6,800 for the heaviest.

## Tests

`tests/test_achord.py`: every FREE type on its root; the key's chords under
SNAP and DEGREES, the sizes, the scales; progressions a trig and with the
pattern (TRIG back to the top, 1 BAR), transposed, minor, USER and its
default; AUTO voice leading against CLOSE and staying near the root, the
fixed voicings, the bass; the notes in the spectrum, the filter and its
envelope, Shape and the chorus, level, bounds with random settings; the
strum's targets; tracks apart; the settings page (rows, values, put back, it
fits, the chord in its title); the settings saved, loaded, an older file,
copied and cleared; the playing kit's settings; the page's labels, Contour's
names, the dispatch, SETTINGS + PRESET; the cost.
