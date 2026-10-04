# The VA machine

Model-TG VA Edition adds one machine to [Model-TG](https://github.com/TinyGregAudio/Model-TG):
**VA**, machine index **7**, a two-oscillator analogue-style synth. Everything
Model-TG has, the Sampler (machine 6) included, is unchanged. This page is
for anyone reading or extending the code; players want the
[user guide](USER_GUIDE.md#the-va-machine).

- Source: [`src/va_synth.inc`](../src/va_synth.inc) (the engine), small
  hooks in [`src/model_tg.s`](../src/model_tg.s) and [`src/phase1.s`](../src/phase1.s),
  the machine-count patches in [`build.py`](../build.py).
- Generated: [`src/va_tables.inc`](../src/va_tables.inc) by
  [`tools/gen_va_tables.py`](../tools/gen_va_tables.py), [`src/va_icons/`](../src/va_icons)
  by [`tools/gen_va_icons.py`](../tools/gen_va_icons.py).
- Tests: [`tests/`](../tests) (host only, see [Testing](#testing)).

Addresses are OS 1.13's. **Status: host/static verified only.** Nothing here
has run on a Model:Cycles yet; see [What needs hardware testing](#what-needs-hardware-testing).

## Contents

- [Architecture](#architecture)
- [The existing signal path it reuses](#the-existing-signal-path-it-reuses)
- [Oscillators](#oscillators)
- [Parameters](#parameters)
- [Machine index 7, and every place that knows the count](#machine-index-7-and-every-place-that-knows-the-count)
- [CPU](#cpu)
- [Memory](#memory)
- [Build and install](#build-and-install)
- [Testing](#testing)
- [What needs hardware testing](#what-needs-hardware-testing)
- [Known limitations](#known-limitations)

## Architecture

```
note / trig / pad / MIDI ──> stock note-on ──> voice+232 (pitch)
                                                   │
sampler_pre (every block, every track)             │
  machine = trackData+18 ──> trk_mach[track]       │
  machine 7 ──> va_pre: dials -> va_w1/w2/rat/mix  │
                trigger edge -> wv_trig            │
                voice@(0) = 6  (stock dispatch sees a Sampler voice)
                                                   │
sampler_dispatch (d4 = 6) ──> 0x400aa7b8 (per-voice params) ──> sampler_render
                                                   │
sampler_render: 0x400a9120 (envelope, pitch) ──> idle check ──> trk_mach = 7?
                                                   │ yes: sv_va
   step = voice+232 -> Q16 ──> va_fill: OSC 1 ─┐
                                        OSC 2 ─┴─ MIX ──> sampler_buf+32 (64 samples, 96 kHz)
                                                   │
sv_nofilt (the Sampler's own tail, unchanged):
   0x400a9f58 decimator 2x -> outBuf ──> 0x400a9252 / 0x400a9430 Amp Decay
   ──> amp_hook: 0x400a967a envelope, Attack, gflt_run (the common Filter /
       Resonance, as on stock machines), resampling tap, MUT / mute gain
   ──> sil_note (idle tracking) ──> the stock mixer
```

The VA replaces only the waveform generation. Pitch, note handling,
envelopes, Attack, Filter, Resonance, the LFOs, slides, idle detection,
level, pan, sends and the mixer are the existing Model-TG and stock stages.

### Why the audio dispatch sees 6

Stock's voice loop reads the machine for its voice reset (`0x400a7dcc`:
`cmpl %a4@,%d4`, then `jsr 0x400a7ab8`) and for the render dispatch
(`0x400a7df4`: bound, then the six-entry function tables `0x40118610` /
`0x40118628`). The Sampler already bypasses the stock machine read by
writing `voice@(0) = 6` itself (`sp_nt_x`). `va_pre` does exactly the same,
so **no stock audio code ever sees an index above 6**: the dispatch bound
stays as Model-TG set it, no table grows, and `sampler_dispatch` sends both
6 and 7 to `sampler_render`. Inside, the VA is told apart by `trk_mach`, the
copy of `trackData+18` that `sampler_pre` publishes every block (Model-TG's
existing mechanism; `amp_hook` and `voice_quiet` already key on it).

One consequence: switching a track between Sampler and VA does not run the
stock voice reset (it does from any stock machine). Nothing in the VA
depends on it: its phases restart on every note.

## The existing signal path it reuses

The Stage 1 map, as found in Model-TG's source:

| part | where | what it is |
|---|---|---|
| wavetable renderer | `wave_fill` / `wv_voice` (`model_tg.s`) | the Sampler's Wave mode: frames of the loaded sample played as an oscillator |
| inputs | `wave_fill(track, stepQ16, pcmBase, count)` | step from `voice+232`; `wv_shift`, `wv_fa`, `wv_fb`, `wv_m8`, `wv_det` set per block by `sw_wave` in `sampler_pre` from trackData +22..+28 |
| outputs | `gran_acc` (64 longs) -> `wf_dst` = `sampler_buf+32` | 64 samples at 96 kHz, s16 << 15 |
| phase | `wv_ph`: 3 x Q32 per track | centre, upper, lower unison voices |
| pitch | `sv_havestep`: `((v>>4) * 93957) >> 12` | `voice+232` -> Q16 step, 1.0 = note 60 |
| increment | `step * 357.2031`, halved | Q32 cycles per 96 kHz sample (261.63 Hz at 1.0) |
| detune | `wv_det` = `wdet_tab[dial]`, ratio - 1 in Q16 | +-delta added to the centre's increment |
| trigger | `sp_wave_trig` sets `wv_trig`; `wave_fill` restarts at 0, 1/3, 2/3 | on the `a5@ && !a3@` edge of the voice loop |
| 32/64-sample path | fill at 2x into `sampler_buf+32`, decimator `0x400a9f58` -> 32 into outBuf | `sampler_buf+0..31` is the decimator's history |
| amp stage | `amp_hook` (all six stock call sites of `0x400a967a` retargeted to it) | envelope, Attack ramp, `gflt_run`, resampling tap, gains |
| filter | Sampler: `ladder_run` on the 2x buffer, from +26/+28. Stock machines: `gflt_coef` (in `sampler_pre`) + `gflt_run` (in `amp_hook`) from the common +48/+50 | the same 4-pole ladder |
| machine dispatch | `sampler_pre` (0x400a7da8) / `sampler_dispatch` (0x400a7e02) | see above |
| descriptors | 0x40a71540 + 76 x (machine+1), seven slots; `descr_hook` (0x4004df5c), `descr_b_hook` (0x4004df76) | the Sampler's is private, `sampler_descr` |
| page list | `page_machine_list`, copied at 0x4001bbd0 | machine+1 for each machine with a page |
| labels | `apply_names` swaps param_table's name pointers at runtime | the ids are global, so labels follow the selected track |
| LFO destinations | `paramIdFor` 0x4005a692 (group = the raw machine, from 0x40014042); `sampler_lfo_gate`, `sampler_amp_gate`, `sampler_com_gate` | groups past 5 are answered by the gates |
| commit | `mc_commit_hook` at 0x4001413e | a machine's page defaults on a change |

`wv_ph` and `wv_trig` are reused as they are: `wv_ph[track*3 + 0]` is OSC 1,
`[1]` OSC 2, `[2]` unused (a future sub oscillator). A track is one machine
at a time, so the Wave mode and the VA never share them in the same block.
The pitch, the increment, the 2x rate, the accumulator (`gran_acc`) and the
output scaling are `wave_fill`'s.

## Oscillators

Per oscillator, per 96 kHz sample, from a Q32 phase `p` and increment `inc`:

| wave | value (s16) | band-limiting |
|---|---|---|
| saw | `(p + 2^31) >> 16` signed: rising, jumping down at `p = 0` | PolyBLEP at the jump |
| square | +32767 for the first half cycle, -32767 the second | PolyBLEP at both jumps |
| triangle | `32768 - 2 |saw(p + 1/4)|`: 0 at `p = 0`, rising | none needed (its harmonics fall 12 dB an octave) |

PolyBLEP smooths a jump over the sample either side of it with a
two-segment polynomial. For `u` = the phase's distance from the jump
(`u < inc`), the correction is `(1 - u/inc)^2` of the jump's half-height,
added after the jump and subtracted before it (signs per jump direction).
`u/inc` costs one divide per oscillator per block: `R = 2^31 / (inc >> 8)`,
then `((u >> 8) * R) >> 16` is `u/inc` in Q15, and the product stays below
2^31 because `u < inc`. Away from the jumps a sample costs two (saw) or
four (square) unsigned compares. The host test shows it removes over 90% of
the aliased energy of a naive saw at ~3 kHz.

Mixing: OSC 1 is weighted `256 - MIX`, OSC 2 `MIX` (0..256), summed in
`gran_acc`, `>> 8`, clamped to +-32767, `<< 15` into `sampler_buf+32` - the
same window and scale the Sampler fills. An oscillator whose weight is 0 is
not computed; its phase just advances 64 increments.

OSC 2's increment is OSC 1's times the DTUN ratio (Q16, 0.5..2.0), done as
`(inc >> 15) * r + ((inc & 0x7fff) * r >> 15)` with `r` in Q15 so neither
product passes 32 bits.

Analogue character, all per block, not per sample:

- **Note start.** A new note restarts OSC 1 at half a cycle - the saw's and
  triangle's zero crossing and the square's edge midpoint, so no note starts
  on the saw's jump - and OSC 2 within +-1/16 cycle of it, a little
  differently each note. Unison oscillators therefore never cancel, and
  never start exactly alike.
- **Drift.** Each oscillator's increment is scaled by `1 + d/65536`, where
  `d` is a random walk of one step a block (an LCG's top bits), bounded to
  +-40: about +-1 cent, wandering over roughly a second.
- No saturation stage: not needed for the level (the mix cannot clip) and
  not worth its per-sample multiplies in a first version.

Everything is integer: `mulsl`/`mulul`, shifts, one `divul` per oscillator
per block. No floating point, no allocation, no tables in the sample loop.

## Parameters

The VA's page uses **the Sampler's own four parameter ids**, so it needs no
new parameter objects (Model-TG's history explains why new ones cannot be
made safely: a parameter is constructed and registered at boot):

| knob | id (stock name) | trackData | slot | VA label | value |
|---|---|---|---|---|---|
| 1 Pitch | 0x2a | - | conventional | (stock) | |
| 2 Decay | 0x4b (Chord Amp Decay) | +36 | conventional | (stock) | |
| 3 Color | 0x2e (Kick Color) | +22 | 0x10 | OSC1 / Osc1 Wave | 0-42 saw, 43-85 square, 86-127 tri |
| 4 Shape | 0x3e (Perc Shape) | +24 | 0x11 | OSC2 / Osc2 Wave | the same |
| 5 Sweep | 0x49 (Chord Sweep) | +26 | 0x12 | DTUN / Osc2 Tune | `va_det_tab`, 64 = unison |
| 6 Contour | 0x4a (Chord Contour) | +28 | 0x13 | MIX / Osc Mix | 0 OSC 1 .. 64 even .. 127 OSC 2 |

With **PRESET** held, `va_descr_atk` puts Attack (0x0d) at Decay's place
and the common Filter (0x0c) and Resonance (0x0b) at Sweep's and Contour's -
exactly what `dh_pass` gives every stock machine, because the VA filters
like one (`gflt_run`, from trackData +48/+50), not with the Sampler's
ladder.

- **The descriptor** is `sampler_descr` itself: `descr_hook` answers index 8
  (machine+1) and `descr_b_hook` raw machine 7 with it, or with
  `va_descr_atk` while the modifier is held. Until the audio thread has built
  them (`descr_done`), both fall back to the Kick alias in
  `table_lookup_a_fixed` / `table_lookup_b_fixed`, which now also cover 8 / 7.
- **Labels**: `apply_names` with `d1 = 2` installs `va_swap` (Osc1 Wave,
  Osc2 Wave, Osc2 Tune, Osc Mix; OSC1, OSC2, DTUN, MIX; category VA). The
  stock names come back the moment another machine's page is drawn. The
  Sampler's two-decimal Start/End display is not installed for the VA:
  its dials draw as plain 0..127.
- **P-locks, slides, LFOs, saving** come with the ids: they are the words
  the Sampler's Start/End/Filter/Res already p-lock, slide and save through.
  `va_params` reads the words every block, so a lock, a slide step or an LFO
  takes effect on the next block without touching the render.
- **LFO destinations**: group 7 reaches `sampler_lfo_gate` (the same four
  ids as group 6), `sampler_amp_gate` (0x4b) and `sampler_com_gate` (the
  stock common list, so Attack, Filter and Resonance are offered - the
  Sampler hides the latter two because it has its own).
- **Defaults**: `mc_commit_hook` writes `va_defaults` (saw, saw, DTUN 68 =
  +7.9 cents, MIX 64) over sound +42..+48 when a track becomes a VA. The
  Sampler's packed-options word (+72) is left alone.

## Machine index 7, and every place that knows the count

`0..5` stock, `6` Sampler, `7` VA - checked in the stock code: the machine
byte is `sound+38` (`0x40014042` reads it), mirrored to `trackData+18`, and
the machine page and descriptor lookups use machine+1.

Every site found, by searching the stock disassembly for the six-machine
bound (`moveq #5`), the table bases and the machine accessor's ten callers:

| site | what it is | Model-TG | VA Edition |
|---|---|---|---|
| 0x400147a4 | `setMachine` 0x4001477e guard | 5 -> 6 | **-> 7** |
| 0x400148aa, 0x400148b2 | 0x4001488a: clamp of current + delta | 5 -> 6 | **-> 7** |
| 0x4010e5e6 | param_table[0x29] (machine selector) max | 5 -> 6 | **-> 7** |
| 0x4001bbd0/e2/ee | ParameterPageView's list: alloc, copy, end | 24 -> 28 | **-> 32** (`page_machine_list` 1..8) |
| 0x400a25e0 | machine page: name and icons for 0..N | 5 -> 6 | **-> 7** |
| 0x400a2612 | machine page: name table | `sampler_name_table` | **+ "VA"** |
| 0x40fe32cc / 0x40fe384c | icon vectors (48x33, 34x34), 0x1c per entry | 7 entries | **8** (`build_sampler_icons`) |
| 0x400a26a2 | machine page markers: first x | 80 | **76** |
| 0x400a26e8 | machine page markers: count | 6 -> 7 | **-> 8** |
| 0x4005a79c | machine -> LFO group | 5 -> 6 | **-> 7** (consistency; its only caller bounds by 5 first, see below) |
| 0x4004df5c | descriptor, machine+1 | `descr_hook` (7) | **+ 8** |
| 0x4004df76 | descriptor, raw machine | `descr_b_hook` (6) | **+ 7** |
| 0x4005a6a6 | paramIdFor, machine params | `sampler_lfo_gate` (6) | **+ 7** |
| 0x4005a6b6 | paramIdFor, Amp Decay (6x32 table, no bound) | `sampler_amp_gate` (6) | **+ 7** |
| 0x4005a6d0 | paramIdFor, common params | `sampler_com_gate` (6 hides 24/25) | 7: stock list |
| 0x4001413e | machine commit | `mc_commit_hook` (6) | **+ 7** |
| 0x400a7df4 | audio dispatch bound | 5 -> 6 | **unchanged** (VA handed over as 6) |
| 0x400a7dba | audio: stock machine read, clamps to 5 | bypassed | bypassed |
| 0x400097f0 | parameter object, clamps to 5 (Chord) | as is | as is (the VA uses Chord's object, as the Sampler) |
| 0x4001b696 | another 34x34 icon draw, clamps to 5 | as is | as is |
| 0x400a4096 | clamps to 5 (after reading the machine) | as is | as is |
| 0x40061902 | 0x400618f2's own bound before 0x4005a79c | as is (6 -> -1) | as is (7 -> -1) |

Why the markers moved: the loop at 0x400a26b2 draws each as a rectangle
`x-4..x` at `x = 80 + 7i`. Eight from 80 would put the last at 125..129,
past the 128-pixel screen; from 76 the last is 121..125.

Model-TG's own per-machine checks (`cmpil #6`) are Sampler features - sample
loading, modes, options, slices, take gain, the 3 s idle hold, fine
Start/End, the waveform popup, restore skip - and stay Sampler-only. The VA
gets stock-machine behaviour from each of them (0.25 s idle, stock mute,
stock snapshot restore).

Each stock byte is asserted by `build.py` before it is patched, as before.
No table was grown in place and nothing new is written into the stock area
except those bytes: the name table, icons, descriptors and lists are ours.

## CPU

Measured on the host (`tests/measure_cpu.py`): instructions of **our** code
per 32-sample block, one track. The stock stages (decimator, Amp Decay,
amp envelope, per-voice parameters) are not counted; they are the same for
the VA as for a Sampler track.

| what | instructions / block |
|---|---|
| `va_fill`, saw + saw, MIX 64 | ~2,960-3,010 |
| `va_fill`, square + square, MIX 64 (worst case) | ~3,790-3,870 |
| `va_fill`, triangle + triangle | ~2,760 |
| `va_fill`, one oscillator (MIX 0 or 127) | ~1,990 |
| `wave_fill` (the Wave mode it is built from), no Detune / Detune | ~3,270 / ~7,900 |
| whole track path, VA saw + saw | ~3,690 |
| whole track path, Sampler Wave mode | ~3,950 |

So a VA track costs about what a Sampler track in Wave mode costs, and less
than Wave with Detune. Six VA tracks with two squares each (12 oscillators,
the worst useful case) are about 6 x 4,600 instructions of our code a
block. Instructions are not cycles (`mulsl` and `divul` take several), so
the real share must be read on hardware: **Device Config > System**, second
page, shows each track's share of the block.

Idle VA tracks cost what idle stock tracks cost: once a note's output has
been silent for 0.25 s, the track is skipped until its next trig.

## Memory

- Blob: 82,288 -> 85,072 B (+2,784 B: code, the 512 B tuning table, two
  icons of 384 and 272 B, labels, state). Still 6 of the 16 filesystem cache
  blocks; 13,232 B spare before a seventh would be needed.
- State: 6 tracks x (2 waveforms, ratio, mix, 2 drift walks) + one seed,
  about 170 B, all static in the blob. The phases and trigger flags are
  `wv_ph` / `wv_trig`, already there.
- Heap: the icon vectors are rebuilt once with 8 entries instead of 7
  (2 x 224 B instead of 2 x 196 B, from the firmware's allocator at boot).
- Nothing is allocated in the audio path; the sample region is not used.

## Build and install

Unchanged from Model-TG: see [BUILD.md](BUILD.md).

```sh
python3 build.py --stock path/to/model-cycles_OS1.13.syx --out Model-TG-VA.syx
```

The build assembles `src/` (with `va_synth.inc` included by `model_tg.s`),
links the one blob at 0x401ab750, asserts every stock byte it patches,
applies the patches above, runs its self-checks and repacks your stock
`.syx`. Install the result with Elektron Transfer like an official OS update.
The web flasher (`flasher/`) carries this edition's patch.

## Testing

```sh
pip install unicorn numpy
python3 -m unittest discover -s tests -v
MODEL_CYCLES_STOCK=path/to/model-cycles_OS1.13.syx python3 -m unittest discover -s tests -v
python3 tests/measure_cpu.py
```

The tests run **the assembled blob** on Unicorn's ColdFire V4e model, with
the stock OS replaced by `rts` stubs (`tests/va_emu.py`):

- `va_params` and `va_fill` are bit-exact with a Python model over every
  waveform pair, MIX extremes, pitches from very low to above C8, triggers,
  and 60 random blocks across six tracks; `va_fill` writes nothing outside
  its own state.
- The pitch is right (note 60 = 261.6 Hz), the output never exceeds
  +-32767 << 15, and PolyBLEP cuts aliasing by more than 10x.
- `sampler_pre` routes 7 to `va_pre` (handing the dispatch 6), 6 to the
  Sampler, 0..5 to stock, and sets the trigger only on a new note.
- `sampler_dispatch` takes a VA track through `va_fill`, the decimator,
  Amp Decay, `amp_hook`, the stock envelope and `gflt_run` exactly once a
  block, and not through the Sampler's ladder or Wave mode.
- Six VA tracks rendered block after block with random p-lock-like
  parameter jumps and retriggers each match the model, and nothing outside
  the VA's and the render's per-block state changes.
- The gates, both descriptor lookups (indices 0..9), the label swap, the
  descriptor build and `mc_commit_hook` give the right answers for 7 and
  the old ones for 0..6.
- **Regression** (`tests/test_regression.py`): upstream Model-TG
  (`70b39dd`, v1.1.0) and this tree run the same inputs side by side - the
  six stock machines and all seven Sampler modes, 12 blocks each, with
  notes. The exit taken, every stock routine called, the voice, trackData,
  outBuf and the Sampler's window are identical.
- With `MODEL_CYCLES_STOCK`: a full build from the stock OS, every
  machine-count byte checked in the image, and the `.syx` unpacked again to
  the same section.

## What needs hardware testing

Nothing above is HARDWARE VERIFIED. To check on a Model:Cycles:

1. Boot; every stock machine and the Sampler play as before (all modes).
2. The machine page scrolls to VA: name, icon, eight markers, none off
   screen; scroll back and forth across all eight; no freeze.
3. A VA track plays from sequencer trigs, pads and MIDI notes, at the right
   pitch, and Amp Decay, Attack, Filter and Resonance (PRESET + Decay /
   Sweep / Contour) work.
4. The page shows OSC1, OSC2, DTUN, MIX (the LFO destination list too), and
   the stock machines' pages their own names again.
5. P-lock each of the four; a slide trig across them; an LFO on each.
6. Save, power off with a VA track selected, power on: it loads and plays.
7. Switch a playing track between VA, Sampler and stock machines.
8. Six VA tracks of square + square: System page CPU and PEAK; no dropouts.
9. Resample a VA track; Retrig page and master FX on it.

## Known limitations

- The waveform dials show 0..127, not names: the parameter value renderer
  is shared stock code; a custom renderer is possible (`fine_draw` is one)
  but was left out of this first version.
- One other stock view (0x4001b696) draws the machine's small icon with its
  own clamp to 5, so it shows Chord's icon for the VA (and for the Sampler,
  as in Model-TG).
- Projects with VA tracks need this edition. Model-TG v1.1.0 and the stock
  OS do not know machine 7: their audio path clamps it to Chord, and their
  pages have no entry for it.
- The VA has no stereo, no Sampler options page and no own filter: it uses
  the stock machines' Filter/Resonance.
- `wv_ph[2]` is reserved (sub oscillator, unison) but unused.
- Scale Lock's slice-track exception reads `loop_mode`, which a track keeps
  when it stops being a Sampler (a Model-TG behaviour, also on stock
  machines): a track switched from a Slice-mode Sampler is not scale-locked
  until its mode is changed back. Unchanged here.
