# ACHORD

*Plaits Edition.* Machine 14 (the fifteenth): a paraphonic wavetable chord
machine with keys, scales and progressions. Written for this project
(MIT): `src/achord/achord.c`, glue `src/achord_glue.inc`, tables
`tools/gen_achord.py`, icons `tools/gen_achord_icons.py`. How to play it:
[USER_GUIDE.md](USER_GUIDE.md#achord). Its features follow the chord machines
of today (HiChord's chords by scale degree, the J-6's progressions and
phrases); its sound is wavetables (in the manner of the early digital
wavetable synths, hard sync and two-operator FM), with no filter of its own:
the track's Filter and Resonance, which the LFOs can move. No code of any of
those machines is in it.

## The sound

Paraphonic: four voices, one envelope, the track's filter after it.

- **Wavetables.** Four banks of eight single-cycle waves (TABLE: ANALOG -
  sine, triangle, warm saw, saw, saw + square, square, pulse 25 %, 10 %;
  SYNC - a saw hard-synced at 1, 1.5, 2, 2.6, 3.3, 4.2, 5.5, 7.5 x; DIGITAL -
  a saw with a resonant peak at harmonic 2, 3, 5, 8, 12, 18, 27, 40; FM - two
  operators, (ratio, index) (1, 0.7), (1, 1.6), (1, 3), (2, 1.2), (2, 2.6),
  (3, 1.5), (3, 3.5), (1, 7)). `tools/gen_achord.py` draws each (a waveform of
  4096 samples or its harmonics), takes harmonics 1..64 (sine and cosine
  amplitudes, `ac_spec`, 8 KB) and scales it to RMS 11000 under a peak of
  30000 at every size.
- **Built at run time.** Each wave is kept at five sizes - 256 samples with 64
  harmonics, 128/32, 64/16, 32/8, 16/4, each one sample longer for the wrap
  (501 samples, 32 waves: 32 KB) - in RAM at `ac_wt`, the top 32 KiB of the
  overlay's region (`model_tg.s`: the overlay now stops under it). They are
  summed from `ac_spec` with the OS's own sine (SRAM, 0x8000eee4), 32 samples
  a block by one ACHORD track (another takes over if it stops), 640 blocks
  (~0.43 s of silence) the first time after a boot; nothing in the image.
- **Reading.** Each voice reads the largest size whose top harmonic, x the
  warp's steepest slope, stays under 0.375 of the sample rate (18 kHz; 12 kHz
  at 32 kHz), linearly interpolated, and crossfades to the next wave of the
  bank (Q15). Four loops: with or without the warp, one wave or two.
- **Warp** (Color): phase distortion. The cycle's first k is read at slope
  0.5 / k, the rest at 0.5 / (1 - k); k from 0.5 (none) down to 0.05.
- **Wave** (Shape) + the envelope x **Move** (Sweep): the place in the bank,
  Q8 over 0..7.
- A square sub an octave under the lowest voice (SUB, polyBLEP), then
  **DRIVE** (x 0.5 to x 2.2 into a soft knee) and **GRIT** (the low 3..12
  bits off, each sample held 1..6 samples).
- **Drift.** Each voice sits a few cents from the others (a fixed spread of
  -3, +2, -1, +4 cents x DRIFT / 5) and wanders slowly (a random walk, a step
  a block, within +-1.5 cents x DRIFT).
- **Envelope.** ~2 ms up on each trig (it moves the wave), then down by ENV DECAY (30 ms to 4 s to
  1/e, `ac_dec`), a step a block.
- **Chorus.** One delay line (256 samples, kept twice so reads never wrap),
  read 3.2 ms back, the time moved by a triangle: I 0.51 Hz +-1.6 ms, II 0.86
  Hz +-1.9 ms, I+II 9.8 Hz +-0.35 ms; the line added to the dry sound. Mono,
  as the machine's output.
- **Level.** x 1.8 to 16 bits and a soft limiter above -2 dB; the default
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
default (a triad, chorus I) ~6,500; four voices, the sub and GRIT 5,800
(one wave) to 9,200 (the warp and a crossfade), 10,000 with chorus I+II (a
stock voice ~7,600, LXR 7,900-9,150). Dropping the ladder paid for most of the
wavetables. An idle track costs nothing (the amp stage's idle skip).

## Under load: 32 kHz

When the CPU guard picks it (`gd_eco`, guard.inc), the track renders at 32
kHz: 21 or 22 samples a block (`eco_n`, eco_rs.c, the resampling state DX
uses), the increments x 1.5 (so the voices read smaller waves, under 12
kHz), the chorus's times two thirds as many samples; its envelope, drift
and triangles step once a block as always. The samples are made up to the
block's 32 frames linearly in the same pass as the output stage (at 48 kHz:
the samples two late, so a change of rate carries on in time). It goes down
at once, cleanly, and back up at its next trig. ~6,500 -> ~5,600 instructions
for the default sound, ~8,200 -> ~6,800 with a crossfade and chorus I+II.

## Tests

`tests/test_achord.py`: every FREE type on its root; the key's chords under
SNAP and DEGREES, the sizes, the scales; progressions a trig and with the
pattern (TRIG back to the top, 1 BAR), transposed, minor, USER and its
default; AUTO voice leading against CLOSE and staying near the root, the
fixed voicings, the bass; the notes in the spectrum, the wavetables against
their spectra at every size, built a piece a block in silence, band-limited
high notes, the wave, the warp, the banks, the envelope moving the wave,
DRIVE and GRIT, the chorus, level, bounds with random settings; the
strum's targets; tracks apart; the settings page (rows, values, put back, it
fits, the chord in its title); the settings saved, loaded, an older file,
copied and cleared; the playing kit's settings; the page's labels, Contour's
names, the dispatch, SETTINGS + PRESET; the cost.
