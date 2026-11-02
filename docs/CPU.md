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

`gd_block`, from `prof_end` once a block: over GD_HI (90) percent, of the new
machines' tracks (trk_mach >= VA_MACH) that are sounding (not `sv_idle`, not
faded by a mute), the one whose note started longest ago (`gd_age`, blocks
since its trig, at least GD_AGE = 24) is ended: its bit in `gd_mask`. One at
most every GD_COOL (12) blocks, so the load is measured again first. Ending a
note is MUT's mechanism: `gd_mask` joins `mute_sil` in amp_hook's fade target
(gv_gain, 64 samples to 0) and in `voice_quiet`, which then calls the track
idle - its parameters, fill, filter, effects and gains all skipped - until a
trig (`gd_trig` clears the bit and opens the gain at once, the voice having
been silent). The stock machines and the Sampler are never ended. It cannot
help when the load is the stock voices' or the effects' own.

Device Config > CPU Guard: ON (every boot) / OFF. Device Config > System:
GUARD n, the notes ended since the page opened.

`tests/test_guard.py`: the choice (oldest, never stock or Sampler, silent,
muted or fresh), the pace, OFF, the fade and the idle and the return on a
trig, a hand mute kept, the two rows, the System page; LXR Rate's pitches,
lengths, bounds, switching and cost.

## Still to measure on a Cycles

The emulator cannot say how much of a track's real time is cache misses. The
System page's track shares can: the same pattern with LXR Rate at 44.1K and
48K, and PEAK with the guard OFF and ON.
