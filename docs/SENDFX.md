# Alternative send effects

*Plaits Edition.*

**Status: verified on the host only. Not yet tested on hardware: back up
first.**

## Using it

Hold **SETTINGS** and press **PAGE** to open the **Send FX** menu. It has
four lines: **DELAY FX** with the delay's effect under it, then **REVERB FX**
with the reverb's. Turn **DATA** on either line of a pair to change that
effect, and press **SETTINGS** or Return to close the menu.

- **Delay** puts one of these on the delay send:
  - **DELAY**: the stock delay.
  - **PING**: a ping-pong delay. Echoes start on the left, then bounce
    between the sides. It follows the tempo the same way the stock delay
    does.
  - **TAPE**: a tape echo. Each repeat gets darker, the time wobbles a
    little, and the repeats saturate as they build up.
  - **CHORUS**: chorus, then flanger.
- **Reverb** puts one of these on the reverb send:
  - **REVERB**: the stock reverb.
  - **PHASER**: a six-stage phaser.
  - **SPRING**: a spring-style reverb, with short, metallic, bouncy
    reflections.
  - **RING**: a ring modulator.
  - **DIST**: a distortion.
  - **CLOUD**: a light granular cloud, after Mutable Instruments Clouds. Four
    grains replay snatches of the last 0.7 s, two leaning left and two
    right, each starting again elsewhere as it ends.

The sends, the FX page's knobs and the delay's feed into the reverb all
still work. Only the effect on each send changes, and the FX page's four
knobs now drive it:

| effect | knob 1 | knob 2 |
|---|---|---|
| PING | Delay Time: echo time, synced to the tempo as stock | Delay Feedback: how many repeats |
| TAPE | Delay Time: echo time, synced to the tempo as stock | Delay Feedback: repeats, a little past self-oscillation |
| CHORUS | Delay Time: LFO speed, 0.05 to 10 Hz | Delay Feedback: up to half, chorus depth; above half, flanger, with more feedback the higher it goes |
| PHASER | Reverb Size: sweep speed, 0.05 to 10 Hz | Reverb Tone: resonance |
| SPRING | Reverb Size: decay | Reverb Tone: dark to bright |
| RING | Reverb Size: carrier frequency, 20 Hz to 4 kHz | Reverb Tone: smooth (triangle) to hard (square) |
| DIST | Reverb Size: drive | Reverb Tone: dark to open |
| CLOUD | Reverb Size: grain length, 21 to 360 ms | Reverb Tone: feedback, from a dry scatter to a long, dense tail |

Each pattern keeps its own two choices, saved with the pattern, as the
delay and reverb settings are. A new pattern, or a new project, starts with
the stock delay and reverb.

## Limits

- An effect you switch to starts silent: what the old one held is gone.
  The delay takes about 20 ms to start, the reverb's effects about 3 ms.
- PING and TAPE echo for 2.7 s at most. A longer Delay Time (a slow tempo,
  a long note value) stays at 2.7 s.
- SPRING is mono: the two sides are summed on the way in. It comes out in
  stereo.
- The Send FX menu and the types are not p-lockable.
- The stock delay and reverb keep running on silence while another effect
  is chosen, so that switching back is clean. The new effect's cost comes
  on top of theirs (see below).
- Sample memory is 768 KB smaller: the delay's lines (512 KB), and the
  spring's and CLOUD's (256 KB), sit at the top of the sample region.
- CLOUD needs its line to fill: for the first moments after you pick it,
  some grains still read silence. LFO 3/4's
  per-pattern blocks take another 12 KB there.

## How it works

The code is in `src/sendfx.inc`.

- **The slots.** The output stage calls the delay `0x40057488` (out
  `0x8000ba90`, in `0x8000b990`, params at state + 428: Time, Feedback)
  through `fx_a`, and the reverb `0x400579c4` (out `0x8000bb90`, params at
  state + 432: Size, Tone) through `fx_b`. Both now go through `sfx_dly` /
  `sfx_rev`.
  - With the stock type, they jump to the stock code.
  - With another type, the stock effect still runs, fed `SFX_ZERO`, so
    what it holds dies away with its rings, positions and DMA working as
    always. That buffer is cleared before every call, because the stock
    reverb uses its input buffer as scratch. Our effect then writes the
    bus's output in its place.
  - The output stage's routing is untouched. The delay's output still
    feeds the reverb bus, and both returns are mixed as stock.
  - The silence skip (`fx_off`) still freezes everything after 10.9 s with
    no sound.
- **The types** are bytes +120 and +121 of the kit's LFO 3/4 block
  (`lfo2.inc`, now 124 bytes). They follow the kit everywhere it goes:
  - Saved at +646 of the kit's storage record (delay in the low nibble,
    reverb in the high), with their complement at +647. A record without
    the complement, which covers every older project, loads the stock
    effects.
  - Copied with the kit by `kit_memcpy`.
  - Reset to stock by `sfx_fxd`, which hooks the FX default fill
    `0x40061318` (a new kit, a new project, an FX block that failed to
    load).
- **Starting clean.** On a change of type, the effect's lines are cleared
  16 KiB a block, and silence is output meanwhile: 32 blocks for the
  delay's 512 KB, 12 for the spring's and CLOUD's 192 KB. The first change also clears
  the state kept past the spring's lines (`sfx_init`).
- **Samples.** The bus samples are 32-bit (16-bit << 16). The effects work
  on the top 16 bits, with Q14 coefficients. Lines are 16-bit.
- **The effects:**
  - **PING / TAPE** read their lines at a fractional delay, linearly
    interpolated.
    - The time is the stock delay's: (Time + 256) x 48000 / 1024 x 900 /
      tempo. It glides there (a 16th of the way a block).
    - PING feeds the mono input into the left line, and each line's output
      into the other.
    - TAPE adds wow (0.6 Hz, ±48 samples) and flutter (7 Hz, ±2). It puts
      a one-pole lowpass (0.45) and a cubic soft clip in each side's
      feedback, which can reach 1.04.
  - **CHORUS:**
    - Up to half Feedback: 15 ms ± up to 5 ms, with no feedback.
    - Above half: 2 ms ± 1.8 ms, with feedback up to 0.9.
    - The right side's LFO is a quarter turn ahead.
  - **PHASER:** six first-order allpasses a side (`y = x1 + a (x - y1)`),
    swept 100 Hz to 4 kHz by a triangle LFO, with feedback up to 0.7. The
    output is (dry + phased) / 2.
  - **SPRING:** the summed input goes through four allpasses (142, 107,
    379, 277 samples; g = 5/8). Then come two damped combs a side (1557 and
    1867 left, 1617 and 1777 right), with feedback 0.5 to 0.97.
  - **RING:** the input times a triangle carrier, pushed towards a square
    by Tone. The right side's carrier is a quarter turn ahead.
  - **CLOUD:** the input, in mono, goes into a 65536-sample line (1.37 s),
    with the cloud fed back into it (Tone, 0 to 0.9). Four grains read it
    back, each under a triangle window. A grain's length is 1024 + Size / 2
    samples, up to half as long again at random. It starts its length plus
    0 to 32767 samples behind the write point. Grains 0 and 2 go left
    (right at half), 1 and 3 the other way.
  - **DIST:** the input times the drive, then a cubic soft clip, -6 dB,
    and a one-pole lowpass.
  - The curves are in `src/sfx_tables.inc`, written by
    `tools/gen_sfx_tables.py`.
- **The menu** is the Granular menu's list (`lm_build`) with four rows: a
  heading (label, empty value) and its effect (empty label, the value) for
  each send, so no line holds both. Both rows of a pair turn the same type.
  SETTINGS + PAGE (key 15) is handled in `key_hook` like the other
  SETTINGS chords (`kh_sfx`). It sets `mod_used`, so SETTINGS' release
  opens no Config menu.
- **Space.** The code fits in the eighth cache block. Its state lives in
  the sample region, past the spring's lines.

## Verification

Host verified (`tests/test_sendfx.py`):

- **Slots:**
  - Stock types, or an out-of-range byte, go straight to the stock
    effects with their own arguments.
  - Another type feeds the stock effect silence, re-cleared after the
    stock reverb scribbles on it. It puts out silence while its lines
    clear, then starts from silence.
  - A second change clears again.
- **PING:**
  - The first echo is on the left at exactly the time, at full level.
  - Then it comes on the right at twice the time, at half level, then on
    the left again.
  - It follows the tempo.
- **TAPE:**
  - The echo lands within the wow's range, smeared by the lowpass.
  - The second echo is quieter.
  - Each side keeps to itself.
  - At full feedback it saturates without running away.
- **CHORUS:**
  - With no depth, the output is exactly 15 ms late, with no feedback.
  - The flanger echoes 8 to 185 samples late and repeats.
  - The two sides differ.
- **PHASER:**
  - Silence stays silence.
  - An impulse spreads out in time.
  - It stays bounded with full feedback.
  - The sides differ.
- **SPRING:**
  - It rings and decays.
  - A higher Size gives a longer tail.
  - The sides differ.
- **RING:**
  - Silence stays silence.
  - A steady input follows the carrier, at the right frequency.
  - Tone squares it.
- **CLOUD:**
  - Silence stays silence.
  - Once its line has filled, it sounds, spread left and right.
  - With Tone 0 it is gone 1 s after the input stops; with Tone up it
    still sounds 1.5 s on.
  - Size sets the grains' length: 1024 to 1536 samples at 0, ~17,000 to
    26,000 at full.
- **DIST:**
  - Gentle settings are x1.06 at -6 dB.
  - Driven hard, it clips at -6 dB.
  - Tone darkens it.
- **Menu:**
  - The rows turn and clamp the playing pattern's types, and show them.
  - Four lines: each heading has an empty value, and each effect line an
    empty label.
  - SETTINGS + PAGE reaches the menu. PAGE alone does not.
  - Once the menu opens, the press and its release are swallowed.
- **Kits:**
  - Saved and loaded at +646 / +647.
  - A record without the complement, or with out-of-range types, loads
    stock.
  - The types are copied with the kit.
  - The FX default fill resets them, leaving the stock frame intact.

CPU, instructions a block (the Plaits engines run 880 to 6,830 a track):

| effect | instructions |
|---|---|
| PING | ~3,700 |
| TAPE | ~5,400 |
| CHORUS | ~3,650 |
| PHASER | ~6,400 |
| SPRING | ~8,700 |
| RING | ~2,500 |
| DIST | ~3,350 |
| CLOUD | ~5,370 |

These are on top of the stock delay and reverb, which keep running.

`build.py` asserts the stock bytes of the FX default fill's prologue.

Needs hardware:

1. **SETTINGS + PAGE** opens Send FX. SETTINGS alone still opens the Config
   menu, and Return closes Send FX.
2. **Delay = PING** with a hi-hat sent to the delay: the echoes alternate
   left and right, in time. Turn Delay Time and Feedback.
3. **TAPE:** darker, wobbling repeats. Turn Feedback up: they saturate and
   do not blow up.
4. **CHORUS:** Feedback low gives a chorus. Above half it flanges. Delay
   Time sets the speed.
5. **Reverb = PHASER, SPRING, RING, DIST, CLOUD:** each sounds as described, and
   the knobs act as in the table.
6. **Save the project**, then load another and come back: each pattern has
   its own choices. A new project starts with DELAY and REVERB.
7. **CPU page:** a busy pattern with SPRING and TAPE has no dropouts.
8. **Back to DELAY / REVERB:** they come back clean, with no burst of old
   echoes.
