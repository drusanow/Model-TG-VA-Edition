# Quantised mute and track solo

*Plaits Edition.*

**Status: verified on the host only. Not yet tested on hardware: back up
first.**

## Using it

- **Mutes wait for the bar.** Mute tracks as usual: hold **FUNC** and press
  pads, or use latched mute mode (hold **TRACK**, tap **FUNC**). With the
  sequencer playing, the mutes change on the first step of the next bar, not
  when you let go of FUNC. While you are still in mute mode, the pads already
  show the state the tracks will have.
- **Solo.** Hold **SETTINGS** and press a track's pad. Every other track
  mutes at the next bar, and the screen shows **SOLO n**. Do it again on the
  same track to bring back the mutes you had before the solo
  (**SOLO OFF**). SETTINGS + another track's pad moves the solo to that track
  and still remembers the mutes from before the first solo.
- **Choose when they happen**, in **Device Config > Mute Q** (tap
  **SETTINGS** for the Config menu):
  - **1BAR**, the default: on the next bar (every 16 steps from the pattern's
    first step);
  - **PTN**: when the pattern comes round to its first step;
  - **OFF**: at once, as on the stock OS.
- With the sequencer **stopped**, mutes and solos happen at once.
- Pressing a pad twice before the bar cancels that track's change.

## Limits

- Mute Q goes back to 1BAR at every power-on.
- Only mute mode's pads and the solo chord are quantised. A mute sent by
  MIDI CC is stock: at once.
- A "bar" is 16 steps of the pattern's own step count. In a pattern shorter
  than 16 steps, 1BAR waits for the pattern's first step, like PTN.
- A mute by hand clears the solo's memory: SETTINGS + pad on the soloed track
  then unmutes every track.
- Solo works from the main page (the parameter pages). Other pages keep
  their own pad functions.

## How it works

The code is in `src/mutesolo.inc`.

- **The state.** The stock mute is a mask in the playing pattern, read by
  `0x4001263c(obj, track)` and set by `0x4001381c(obj, track, on, cc)`.
  `0x4001381c` also updates the sequencer and sends CC 94. Nothing here
  writes the mask itself: a change waits in `qm_want` / `qm_dirty` and is made
  through `0x4001381c`, exactly as a stock pad makes it.
- **The moment.** The sequencer tick (`0x40054c1e`) builds a step's trigs,
  then advances its step counter `0x40a78840` (`0x40055610`). So the counter
  holds the *next* step, and 0 after the last. While it reads a multiple of
  16, the bar's first step has not been built yet; while it reads 0, neither
  has the pattern's. A change made then lands exactly on that step.
  `qm_poll` runs on the UI clock: `led_hook` (at most once an audio block)
  and `tick_hook`. It applies the waiting changes as soon as the counter is
  on such a step. With OFF, or the sequencer stopped (`0x40a7883c` not 1), it
  applies them at once.
- **Mute mode.** Model-TG already queues mute mode's pads and applies them
  when the mode closes (`mq_apply`). Each queued pad now goes to `qm_flip`.
  The mode's LED query (`mq_muted`) shows `qm_fut`, the state to come.
- **Solo.** The main page's pad handler entry (`0x401002b0`, the thunk
  `0x4001d3d4` to `0x4001d180`) goes through `qm_pad`. With SETTINGS (key 13)
  held, a pad press solos its track and is not passed on. The chord sets
  `mod_used`, so SETTINGS' release opens no Config menu.
- **Mute Q** is one more Device Config row, built like the Transfer row.

## Verification

Host verified (`tests/test_mutesolo.py`):

- **Default and timing:**
  - the default is 1BAR;
  - stopped, or with OFF, changes happen at once;
  - with 1BAR, nothing happens until the counter reads a multiple of 16 (0
    included);
  - with PTN, nothing happens until the counter reads 0.
- **Queued changes:**
  - a second press cancels;
  - other tracks still wait;
  - mute mode's queue goes through the quantiser;
  - the mute pads show the state to come.
- **Solo:**
  - solo and back, and from one solo to another;
  - the SETTINGS + pad chord: stock without SETTINGS, the release swallowed,
    `mod_used` set, other pads ignored.
- **Mute Q row:** turning it steps OFF / 1BAR / PTN and clamps at the ends.

`build.py` asserts the stock bytes of the pad entry, its thunk and the step
counter update.

Needs hardware:

1. Pattern playing, hold FUNC, press two pads, release FUNC: both tracks mute
   on the next bar's first step, in time. In mute mode the pads already show
   the state to come.
2. Mute Q = PTN: the mutes wait for the pattern to come round. OFF: at once.
3. SETTINGS + pad 3: "SOLO 3", the others mute on the bar. Again: back as
   before. SETTINGS alone still opens the Config menu.
4. Stopped: mutes and solos at once.
5. High tempo (300+ BPM): the change still lands on the first step of the bar.
