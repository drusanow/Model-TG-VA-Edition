# LXR

*Plaits Edition.* Machine 13 (the fourteenth): the Erica Synths LXR-02's drum
voices, ported from its firmware (<https://github.com/erica-synths/lxr-desktop-app>,
GPL-3.0-only) to the Model:Cycles. A modified version: not Erica Synths'
firmware, not made, approved or supported by them. Licence and origin:
[src/lxr/NOTICE.md](../src/lxr/NOTICE.md). How to play it:
[USER_GUIDE.md](USER_GUIDE.md#lxr).

## What runs

`src/lxr/lxr.c` is a fixed-point port (the Cycles' ColdFire has no FPU) of the
LXR's four voices, as its firmware computes them:

| LXR source | here |
|---|---|
| `DrumVoice.c` | the drum voice: an oscillator mixed with, or phase-modulated by, a second one; the FM amount follows the pitch envelope; the amp gain ramped across each block |
| `Snare.c` | the snare: a tone plus sample-and-hold noise through its filter; its repeat mode (the LXR's clap) |
| `HiHat.c`, `CymbalVoice.c` | a carrier phase-modulated by two summed oscillators, through the filter (open / closed decays for the hat) |
| `Oscillator.c` | sine, triangle, saw, square (its band-limited tables, one per octave), noise, PWM; its trigger phases |
| `Decay.c`, `SlopeEg2.c`, `snapEg.c` | the pitch EG, the amp EG (attack, decay, slope, repeats), the snap |
| `ResonantFilter.c` | the zero-delay-feedback state-variable filter (LP, HP, BP, unity BP, notch, peak, the naive 2-pole, off) with its input drive and soft clip |
| `distortion.c` | the distortion curve |
| `mixer.c` | the per-voice sample-rate decimator |

**At the LXR's rate, in its blocks.** The voice runs at 44.14 kHz, the LXR's
own rate: its oscillators alias as they do on an LXR (much of a metallic hat's
sound is aliasing), and its filter and decimator see the same rate. Its control
values (the envelopes, the tuning, the gains) step once a 64-sample block, as
the LXR's `calc...Async` does, the blocks starting at the trigger as an LXR's
triggers fall between blocks. Each Cycles block then interpolates its 32 frames
at 48 kHz from those samples (4-point Hermite, two samples - 45 us - late).

**The LXR's knob curves.** A voice's sound is a set of LXR knob values, 0..127,
as an LXR kit stores them (`P_*` in `lxr.c`). Every curve that turns a knob into
a coefficient is the LXR's own formula (`Decay.c`, `SlopeEg2.c`, `MidiParser.c`,
`ResonantFilter.c`, `valueShaper.h`, ...), evaluated by `tools/gen_lxr.py` in
the LXR's float or double arithmetic into `lxr_tables.h`.

**The LXR's data.** Its wavetables (`lxr_wave.h`) and its init kit
(`lxr_presets.h`) are imported from its sources by `tools/lxr_import.py`. The
sine is the stock OS's table in the Cycles' SRAM (see
[DX.md](DX.md)), read as the LXR's: its `sine_table` is in fact a negative
cosine, which `lxr.c` keeps.

**Two details of the LXR's hardware kept.** Its phase modulation converts
`mod x amount` from a float to an unsigned integer; on its ARM core that
conversion saturates, so a negative product is 0 and only the modulator's
positive half moves the phase. And its "sine" starts a quarter turn in, which
with the table being a negative cosine starts the kick at zero.

## The presets

Contour picks one of eight presets: the six voices of the LXR's init kit (the
sound an LXR loads; "from hrtlkits_batch3"), the snare in its repeat mode as a
clap, and the hat triggered open.

| preset | from the init kit |
|---|---|
| KICK, TOM, PERC | drum voices 1, 2, 3 |
| SNAR | the snare |
| CLAP | the snare with: noise only, band-pass, 4 repeats 15 ms apart (attack 11), decay 25 |
| CHAT, OHAT | the hi-hat, closed and open |
| CYMB | the cymbal |

Color, Shape and Sweep move one of the preset's knobs each (two for the hats'
Shape), from its value at 64: two knob steps a step (the hats' and cymbal's
tuning, half a semitone; the clap's count, one per eight).

## How close it is

`tools/lxr_reference.py` builds the LXR's own voice code on a computer
(`tools/lxr_ref/`: its sources, with the hardware calls stubbed and its ARM
float conversion kept) and renders the init kit's voices. `tests/data/lxr_ref.json`
holds what that gives - each voice's envelope, spectrum and pitch - and
`tests/test_lxr.py` (`TReference`) checks the machine's own code, as the Cycles
runs it (Unicorn), against it.

Built at the LXR's rate on a computer, the port matches the LXR's code sample by
sample on the tonal voices (the error under the signal):

| voice | error |
|---|---|
| KICK, TOM, PERC | -45 to -47 dB |
| closed / open hat | -20 / -23 dB (their FM spreads the fixed-point rounding) |
| snare, cymbal | noise: different random values; their spectra within 1 to 2 dB a band |

At 48 kHz, as on the Cycles, against the LXR:

| voice | envelope (10 ms, median) | spectrum, every band holding over 1/1000 of the energy |
|---|---|---|
| KICK, TOM, PERC | within 0.05 dB | within 2 dB; pitch within 0.5% |
| SNAR | 0.2 dB | within 2 dB |
| CHAT, OHAT | 1.5 dB | within 2 dB; their energy is mostly 16..22 kHz, the top of the LXR's band, which 44.14 -> 48 kHz trims |
| CYMB | 0.6 dB | within 2 dB (4.4 dB in its 50..200 Hz band, under 1/2000 of its energy) |

Not checked against the LXR (the reference is its init kit as it is): settings
the macros reach, and the knobs the presets leave at the LXR's defaults - the
same code computes them, through the same curves.

**Not ported:** the sampled attack transients (their source recordings' terms
are unclear: a kit asking for one plays without it, as an LXR whose sample is
missing), user samples as oscillator waves (silent likewise), the LFOs and the
velocity routing (the Cycles has its own), pan.

## CPU

`tests/test_lxr.py` (`VA_TEST_VERBOSE=1`): a sounding voice costs 8,300 to
10,700 instructions a block (the hats the most: three oscillators and the
filter), against a stock voice's ~7,600; its first block after a change of
preset or of the distortion knob ~1,300 more. A silent LXR track costs nothing.

## Files

| file | what |
|---|---|
| `src/lxr/lxr.c` | the port (GPL-3.0-only) |
| `src/lxr/lxr_tables.h`, `lxr_gen.s` | written by `tools/gen_lxr.py`: the curves; the ColdFire assembly |
| `src/lxr/lxr_wave.h`, `lxr_presets.h` | imported by `tools/lxr_import.py` from the LXR's sources |
| `src/lxr_glue.inc` | the machine's glue: labels, names, pictures, the Contour screen (MIT) |
| `tools/lxr_ref/`, `tools/lxr_reference.py` | the reference renderer, and `tests/data/lxr_ref.json` |
