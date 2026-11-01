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
| `transientGenerator.c`, `transientTables.c` | the twelve sampled attack transients, played from the trigger at their pitch (and the snap and offset modes) |

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

**The LXR's data.** Its wavetables (`lxr_wave.h`), its twelve sampled
transients (`lxr_trans.h`) and its init kit (`lxr_presets.h`) are imported from
its sources by `tools/lxr_import.py`. Erica Synths confirmed the transients are
free or open source and free to use in LXR ports. They are, by the names in its
source: js_expClick, js_click2, 4102__patchen__atik-2-10-stereoatik,
82279__kevoy__acoustic-kick, altemark_rs03 rimshot, neotone_drip2, pj cohen
pedal chic, Shiftr 808 clap, Mattel kick, md 16 17, md 16 36, md 16 81
(transient waves 2 to 13). The
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

## The voice menu

SETTINGS + PRESET on an LXR track opens a page of every setting the voice
uses (`src/lxr/lxr_menu.c`, GPL-3.0-only; glue in `src/lxr_glue.inc`, the page
kind `MM_LXR` of `mm_open` in `src/model_tg.s`). How to use it:
[USER_GUIDE.md](USER_GUIDE.md#the-voice-menu-every-lxr-setting).

**The edits.** Each kit (pattern) holds a 32-byte block per track after its
MIDI CC settings in its LFO 3/4 block (`lfo2.inc`: `LX_OFF` = 172, the block
now 364 bytes): the preset the edits were made on + 1 (0: none), then its 31
knobs in `P_*` order. `lx_params` hands `lxr_params` the playing kit's block
for the track every block; `cook` takes its knobs from there when Contour is
on that preset, else from the preset, and the macros then move them as
before. A change is seen from a sum of the block (`lx_edsum`), so an edit, a
pasted kit or a loaded pattern re-cooks the voice at the next block, without
restarting it. The first edit on a preset copies its knobs in; PUT BACK THE
PRESET clears the block.

**Saved with the pattern.** The block goes wherever the kit's LFO 3/4 block
goes: copied with the kit (`kit_memcpy`), cleared with a track's sound
(`l34_snd_reset`) and with a new kit (`lcc_kclr`), and saved in the kit's
MIDI CC file (`lfocc.inc`): with edits the file is 240 bytes, the 48 of MIDI
CC then the six tracks' blocks, hashed whole; without, it is the 48-byte file
as before, hashed as before. So every older project loads as it did, with no
edits, and a kit with only edits gets a file too.

`tests/test_lxr_menu.py`: the rows of each voice, the values shown, the
screen's layout (everything fits), the edits (the preset copied in, clamped,
put back, another preset's giving way), the sound (the edits on their preset
only, the macros from them, heard while playing, the playing kit's), the file
(240 and 48 bytes, wrong files), the copy and clear, and the page's handlers.

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

With its sampled transients on (the reference holds five: the kick with the
acoustic kick, the tom with js_expClick, the snare with the 808 clap, the closed
hat with the pedal chick, the kick in offset mode): at the LXR's rate the tonal
ones match its code to -46 dB, the hat to -26 dB; at 48 kHz their envelopes and
spectra are checked as above.

Not checked against the LXR (the reference is its init kit as it is): settings
the macros reach, and the knobs the presets leave at the LXR's defaults - the
same code computes them, through the same curves.

**Not ported:** user samples as oscillator waves (silent, as an LXR without
them), the LFOs and the velocity routing (the Cycles has its own), pan.

## CPU

`tests/test_lxr.py` (`VA_TEST_VERBOSE=1`): a sounding voice costs 7,400 to
8,700 instructions a block (the hats the most: three oscillators and the
filter), against a stock voice's ~7,600; its first block after a change of
preset or of the distortion knob ~3,500 more (its distortion curve and tuning
made). A silent LXR track costs nothing. Made cheaper without changing a sample:
the filter's output chosen once a block rather than every sample, the
oscillators' FM test out of their loops, the distortion a branch-free table, the
oscillators' tuning cached (its multiplications only when a note or knob
changes).

## Files

| file | what |
|---|---|
| `src/lxr/lxr.c` | the port (GPL-3.0-only) |
| `src/lxr/lxr_tables.h`, `lxr_gen.s` | written by `tools/gen_lxr.py`: the curves; the ColdFire assembly |
| `src/lxr/lxr_wave.h`, `lxr_trans.h`, `lxr_presets.h` | imported by `tools/lxr_import.py` from the LXR's sources |
| `src/lxr/lxr_menu.c` | the voice menu: its rows, values, edits and screen (GPL-3.0-only) |
| `src/lxr_glue.inc` | the machine's glue: labels, names, pictures, the Contour screen, the voice menu's page (MIT) |
| `tools/lxr_ref/`, `tools/lxr_reference.py` | the reference renderer, and `tests/data/lxr_ref.json` |
