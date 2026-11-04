# CPU: the load, the guard and what was made cheaper

How to use the settings: [USER_GUIDE.md](USER_GUIDE.md#cpu-guard-and-lxr-rate).

## Where the time goes

The render is one interrupt a block: 32 samples, ~667 us. Every voice, the
send effects, the LFOs and the output stage run in it; the sequencer, the
screen, the buttons and USB get what is left. A block that takes more than
its time is late - a dropout - and everything outside the audio slows down
with it. `prof_end` (model_tg.s) measures each block's busy time against the
time between block starts (Device Config > System: CPU, PEAK, each track's
share).

The new machines cost about what a stock voice does **in instructions**
(Unicorn counts, `tests/test_*.py` with `VA_TEST_VERBOSE=1`): a stock voice
~7,600 a block, LXR 7,600-8,900, DX ~7,100, PLAITS 880-6,830, the VA drums
5,800-7,200, the send effects 2,500-8,700. The emulator does not count cache
misses, slow divides and multiplies or memory waits, and on the Cycles those
are where ours lose to the stock code: the stock voices keep their buffers
and tables in the processor's internal SRAM (one cycle); ours keep code and
tables in the SDRAM, through the caches, where several heavy machines and a
send effect evict each other's lines.

## The internal SRAM: no room

The SRAM is 64 KiB at 0x80000000. At boot (0x4000045c) the stock OS copies
0x4019b590..0x401a2a50 to 0x80000000 and zeroes up to 0x80008000, then copies
0x401a2a50..0x401aa140 to 0x80008000 and zeroes up to 0x80010000: every byte
is its data or its zeroed state. A scan of the image for addresses into it
(every aligned 32-bit word, and every absolute operand in the code) finds no
run of more than ~1 KiB that nothing names - and arrays are addressed from
their start, so a gap is no proof. Taking any of it could corrupt a stock
voice or effect in a way no test here would see. So nothing of ours moved
there; what was done instead is to make ours touch less memory:

- **LXR's per-sample tables halved**: its filter's soft clip (2,049 entries)
  and tanh (513) are 16-bit (they were 32-bit): 5 KiB instead of 10 of data
  read every sample. Bit-exact. In the emulator it is ~1.7% more
  instructions (the 16-bit loads need more registers); on the Cycles, half
  the data cache lines.
- **LXR's filter coefficients cached**: the two divisions and the
  multiplications made from the cutoff and resonance each render segment are
  made only when the knob moves (`Svf.key`). Bit-exact. The rest of LXR's
  per-block divisions are two in its envelopes' slope curve, a block.

DX already reads the stock SRAM sine (DX.md), as LXR does.

## LXR Rate (Device Config)

`lx_r48` (lxr.c): LXR at 48 kHz instead of the LXR's 44.14 kHz plus the
resampling. The increments from 48 kHz, the filter's cutoffs from tables made
for 48 kHz (`lx_fg48`, `lx_flp2_48`, gen_lxr.py), and every step the
envelopes, the snap, the transient and the decimator take scaled by
44140 / 48000 - the same pitches and times. 6,000-7,300 instructions a block
instead of 7,600-8,900 (~18% less, `tests/test_guard.py` TRate). Kick, tom,
snare, clap and cymbal: pitch within 1.5%, envelopes within 0.6 dB (median).
The hats' sound is mostly the LXR's aliasing at its rate, so at 48 kHz they
come out 1.5-2.5 dB brighter. The default stays the LXR's rate.

## The CPU guard (guard.inc)

`gd_block`, from `prof_end` once a block. It acts when the load stays over
GD_HI (90%) for GD_HOT (4) blocks in a row - a single busy block, a few notes
starting together, is not an overload - and then one step at most every
GD_COOL (12) blocks, so the load is measured again first:

1. **32 kHz** (`gd_eco`, a bit a track): of the DX and LXR tracks that are
   sounding, the one that costs the most (`prof_trk`). LXR renders at 32 kHz
   (`RT_32` in lxr.c: increments from 32 kHz, the filter's 32 kHz tables
   `lx_fg32`/`lx_flp2_32` with the cutoff held under 14.4 kHz, the
   envelopes', the transient's and the decimator's steps x 44140 / 32000);
   DX renders 21 or 22 samples a block instead of 32 (`eco_n`, the
   increments x 1.5; its envelopes, LFO and pitch EG step once a block as
   always). Both are made up to 48 kHz by linear interpolation (the
   cheapest: for DX much of what 32 kHz saves would go into a 4-point
   interpolation). Every rate goes through the same resampling - at 48 kHz a
   copy two samples late, as LXR always was - so a change of rate carries on
   from the same place in the sound: no click (`tests/test_guard.py`
   TRate32: the largest step between two samples around a change is no
   larger than in the second around it). Back at 48 kHz the frames run up to
   1.6% faster for a block or two until they fall on the samples again.
   Costs (Unicorn): LXR 7,600-8,900 -> 5,500-6,300 instructions a block
   (-28%), DX ~7,300 -> ~6,400 (-14%). A track returns to 48 kHz when it
   falls silent or is no longer DX or LXR, and one track every GD_CALM (1,500
   blocks, 1 s) of load under GD_LO (70%).
2. **Ending a note**, only when no DX or LXR track is left to slow: of the
   new machines' tracks (trk_mach >= VA_MACH) that are sounding (not
   `sv_idle`, not faded by a mute), the one whose note started longest ago
   (`gd_age`, at least GD_AGE = 24 blocks): its bit in `gd_mask`. MUT's
   mechanism: `gd_mask` joins `mute_sil` in amp_hook's fade target (gv_gain,
   at GD_FADE = 64 a sample: 512 samples, 10.7 ms - MUT's own fade stays 64
   samples; the steps no longer overshoot) and in `voice_quiet`, which then
   calls the track idle - its parameters, fill, filter, effects and gains all
   skipped - until a trig (`gd_trig` clears the bit and opens the gain at
   once, the voice having been silent).

The first version ended notes on any single block over 90%, with a 1.3 ms
fade: on held DX chords, drones and cymbals that clicked, and often, as one
busy block is normal when notes start together.

The stock machines and the Sampler are never touched. It cannot help when
the load is the stock voices' or the effects' own. DX pays ~220 instructions
a block at 48 kHz for being ready to change rate (the copy two samples late).

Device Config > CPU Guard: ON (every boot) / OFF (every track back to 48
kHz). Device Config > System: 32K n (tracks at 32 kHz now) on CPU's line,
GUARD n (notes ended since the page opened) on PEAK's.

`tests/test_guard.py`: the streak, the order (the costliest DX or LXR slowed,
then the oldest note ended), the pace, the return to 48 kHz, OFF; the slow
fade without a jump, the idle and the return on a trig, MUT's fade kept; the
two rows, the System page; LXR Rate; 32 kHz's pitch, length, level, cost,
and no click either way, for LXR's presets and DX voices (a sine, FM with
feedback, a fixed-frequency operator).

## Still to measure on a Cycles

The emulator cannot say how much of a track's real time is cache misses. The
System page's track shares can: the same pattern with LXR Rate at 44.1K and
48K, and PEAK with the guard OFF and ON.
