# VA KICK, VA SNARE, VA HIHAT

*VA Edition only.* Three analogue-style drum machines, after the VA on the
machine page (machines 9, 10 and 11 on the page; indexes 8, 9 and 10). They
are modelled on the Roland TR-909 / TR-808 and on Elektron's analogue drum
machines. They were kept simple, to keep CPU low.

**Status: verified on the host only (see [Verification](#verification)).
Not yet tested on hardware: back up first.**

## Using them

Choose one for any track the way you choose any machine. They need no
sample.

The stock controls work as on every machine:
- **Pitch** and the trig's note tune the drum. Note 60 gives the base
  frequencies below.
- **Decay** is the drum's length, through the stock amp envelope.
- **Attack**, **Filter** and **Resonance** are the Model-TG extras (hold
  **PRESET** and turn Decay / Sweep / Contour).

The four machine knobs p-lock, slide, take LFOs and are saved, like the
VA's.

### VA KICK

A gritty sine kick: one sine at 55 Hz (at note 60) whose pitch starts high
and sweeps down to it. It adds a short noise click and drive.

| knob | label | does | default |
|---|---|---|---|
| **Color** | SWP | sweep depth: how far above the base the pitch starts. 0 = none (808-like); 48 is about 2.5x; 127 = 5x (a zap) | 48 |
| **Shape** | STM | sweep time: how fast the pitch falls, 2 ms (0) to 150 ms (127) | 40 |
| **Sweep** | SAT | drive: extra saturation before the envelope, from 0 (none) up to about 5x into a soft clip | 24 |
| **Contour** | CLK | click: a 1.3 ms burst of noise at the start | 40 |

### VA SNARE

A dropping sine mixed with filtered white noise.
- **Body:** a sine at 180 Hz (at note 60), the 909's lower shell. Each hit
  makes its pitch drop over about 15 ms, and it fades in about 85 ms.
- **Noise:** white noise through a low-pass (**TONE**) and a fixed ~480 Hz
  low cut. It lasts as long as **Decay** lets it.

| knob | label | does | default |
|---|---|---|---|
| **Color** | NOIS | the mix: 0 = body only, 64 = half and half, 127 = noise only | 64 |
| **Shape** | PENV | pitch drop: how far above 180 Hz the body starts. 0 = none, 127 = 3x | 32 |
| **Sweep** | TONE | the noise's colour: low-pass from 300 Hz (dark) to 16 kHz (bright) | 88 |
| **Contour** | SAT | drive, as the kick's | 16 |

### VA HIHAT

The TR-808's metallic source: six square waves at 205.3, 304.4, 369.6,
522.7, 540 and 800 Hz (at note 60). Their dissonant, non-harmonic
frequencies are summed, then high-passed. Use a short **Decay** for a
closed hat and a long one for open.

| knob | label | does | default |
|---|---|---|---|
| **Color** | TONE | the high-pass (two poles): 3 kHz (fuller) to 12 kHz (thin and sizzling) | 80 |
| **Shape** | SPRD | spread: 64 = the 808's own frequencies. Lower pulls the six together (0 = one pitch, bell-like); higher spreads them (127 = twice as far, clangorous) | 64 |
| **Sweep** | NOIS | white noise mixed in, for a washier hat | 16 |
| **Contour** | SAT | drive | 0 |

### Level and punch

All three are made loud on their own, with no knob needed. The VA and
Sampler path only reaches half of a stock machine's level. So after the
envelope each drum gets a fixed make-up gain:

| drum | make-up gain |
|---|---|
| kick | ×2 (+6 dB) |
| snare | ×2.5 (+8 dB) |
| hi-hat | ×4 (+12 dB) |

The gain goes into a smooth soft clip at full scale, which works like an
analogue mixer channel driven hard:
- The loud start of each hit is rounded and dense: it punches through.
- The decay stays clean.
- Nothing hard-clips or wraps.

**SAT** adds character on top, before the envelope, and is now gentler (up
to ~5x, a cubic curve that leaves quiet parts clean). For loudness alone,
leave SAT low. Measured over the first 50 ms of a hit at default settings,
against full scale:

| drum | before | now |
|---|---|---|
| kick | -7.3 dB RMS | -2.5 dB RMS |
| snare | -13.5 dB RMS | -6.2 dB RMS |
| hi-hat | -32.4 dB RMS | -20.4 dB RMS |

Peaks reach -1 to -2 dBFS.

### Notes

- **Defaults:** a track switched to one of them gets the defaults above.
- **Choke:** there is none between tracks. To choke an open hat, put the
  closed and open hat on one track and p-lock Decay (or trig a short
  note).
- **Older firmware:** projects that use them need the VA Edition. On stock
  Model-TG or the stock OS those tracks will not play correctly.

## How it works

The code is in `src/drums.inc`, and the tables come from
`tools/gen_drum_tables.py` (`src/drum_tables.inc`).

**Routing.** The drums take exactly the VA's route (see
[VA.md](VA.md#architecture)):
- `sampler_pre` sends machines 7..10 to `va_pre`. For the drums, `va_pre`
  reads the four dials (`dr_params`, the same trackData words +22..+28)
  and marks a new note (`dr_trig`). It then hands the stock dispatch a
  Sampler voice.
- `sampler_render` calls `dr_fill(track, step, machine)` instead of
  `va_fill`. It writes the same 64-sample, 96 kHz window, followed by the
  same tail.
- No stock audio code ever sees an index past 6.

**DSP**, all integer:
- **Sines** are a parabola, `y = x (1 - |x|) x 4`. Its few-percent third
  harmonic is the grit, close to the 909's shaped triangle.
- **Pitch sweeps** decay once a block (from `dr_ktab` for STM, fixed for
  the snare). The increment is interpolated linearly across the block.
- **Click:** an LCG noise burst with a per-sample shift envelope (1/128).
- **Snare noise:** a one-pole low-pass (`dr_ftab`, 300 Hz..16 kHz), less
  a shift-only one-pole low cut (1/32, about 480 Hz).
- **Hat:** six naive squares whose increments are spread around their mean
  each block. They are counted, not added: the sum is `top - (squares low)
  x step`. Then come two one-pole high-pass stages. At 96 kHz the squares'
  aliases sit far below the cluster.
- **Drive (SAT):** `u = x x gain x 2/3`, clipped to ±1, then the cubic
  `y = 1.5u - 0.5u³`. It is clean while quiet, rounds the peaks, and is full
  scale at most. Gain is 1 + dial/32; SAT = 0 bypasses it.
- **Punch (`dr_punch`, from `amp_hook`):** after the amp envelope, at 48 kHz
  on the channel's 32 samples, the same cubic with a fixed gain per drum (×2
  kick, ×2.5 snare, ×4 hi-hat), held to ±32767 so `<< 16` never wraps. It
  makes up the 6 dB the Sampler path sits below the stock machines, and
  compresses each hit's attack. PLAITS (Plaits Edition) gets a clean x2
  here instead. Other machines are untouched.

**Machine plumbing.** These are the sites listed in
[VA.md](VA.md#machine-index-7-and-every-place-that-knows-the-count),
widened from 7 to 10:
- The machine bound bytes (`MACH_MAX = 10` in `build.py`).
- The ParameterPageView list (11 longs).
- The descriptor hooks and lookups (machine+1 8..11 / raw 7..10 share
  the Sampler's descriptor and the VA's modifier variant).
- The LFO and Amp Decay gates.
- `mc_commit_hook` (defaults).
- `apply_names` (label sets 3..5: `kick_swap`, `snare_swap`, `hat_swap`).
- The name table and icons (eleven entries), from `tools/gen_drum_icons.py`
  in the stock machines' style:
  - On the right, a bold solid picture: the kick drum's head as a thick
    ring, a snare with its stick, and two hi-hat cymbals on their rod.
  - In the left panel, under the name, the stock "card": CLASS PERC and
    STYLE BASS / SNARE / METAL, with STR / DEX / MAG ratings as diamonds.
- The machine page's markers. Eleven don't fit one line, so the stock loop
  at 0x400a26a2 is replaced by `mp_markers`. Machines 0..7 sit on the stock
  line (5x5 marks every 7 pixels from x 76). The three drums go on a
  second line below, under the first three.

**CPU** (`tests/measure_cpu.py`): instructions of our code per block for
the whole track path, against the VA's ~3,700 and the Sampler Wave mode's
~3,950.

| machine | defaults | full drive |
|---|---|---|
| VA KICK | ~5,810 | ~5,870 |
| VA SNARE | ~6,210 | ~6,280 |
| VA HIHAT | ~5,960 | ~7,240 |

These figures include the punch stage (~1,300 a block).

Idle drum tracks cost what idle stock tracks cost: 0.25 s after silence the
track is skipped until its next trig.

## Verification

Host verified (`tests/test_drums.py`, Unicorn ColdFire V4e):

- **Kick:**
  - The pitch follows the note (55 / 110 / 27.5 Hz within 1%).
  - Sweep depth and time move the start pitch and the settling time, and
    every sweep ends on the base pitch.
  - The click is at the start only: the signal is sample-identical to no
    click after 25 ms.
  - Drive raises the level and the third harmonic and never exceeds full
    scale.
  - It starts at a zero crossing.
- **Snare:**
  - The body's peak is at 180 Hz and it decays steadily.
  - PENV starts it higher.
  - NOIS 127 removes the body, and TONE tilts the noise by over 20x
    (8-20 kHz against 0.6-2 kHz).
  - The noise has no low end.
- **Hihat:**
  - The six increments match a Python model for every SPRD tested and two
    pitches; SPRD 64 gives the 808's six frequencies within 1%, and
    SPRD 0 one pitch.
  - TONE moves the spectrum; there is no low end (under 900 Hz is under
    1/20 of 3-16 kHz).
  - Noise blends in.
- **All three:**
  - Output stays within full scale for random settings, pitches and trigs.
  - Tracks are independent.
  - CPU is bounded (the figures above).
- **Plumbing:**
  - `sampler_pre` routes 8..10 to `va_pre`, a Sampler voice and the dials.
  - The render calls `dr_fill` with the right track and machine, then the
    tail.
  - Labels follow the machine and go back to stock.
  - Commit defaults are written.
  - The machine list, names and the 11-entry icon vectors are correct.
- **Existing suites:** the VA, regression (upstream Model-TG machines
  unchanged), LFO and generator suites still pass. With the stock OS the
  full build checks the machine-count bytes and the marker loop.

Needs hardware:

1. The machine page shows eight markers on the top line and three (the
   drums) on a second line below, with the names VA KICK / VA SNARE /
   VA HIHAT and their drum icons. Selecting each
   works, and the parameter page shows SWP STM SAT CLK, NOIS PENV TONE SAT,
   or TONE SPRD NOIS SAT.
2. Each drum plays on trigs and pads. Pitch and notes tune it, and Decay
   sets its length.
3. Kick: SWP 0 to 127 goes from a soft thud to a zap; STM lengthens the
   sweep; SAT adds grit; CLK adds a click.
4. Snare: NOIS from body to noise; PENV gives a drop; TONE goes from dark
   to bright.
5. Hihat: short Decay gives a closed hat and long gives open; TONE and
   SPRD sound metallic, not musical-pitched.
6. P-lock and LFO the four knobs, then save and reload: they are kept.
7. CPU page with six drum tracks playing: no dropouts.
