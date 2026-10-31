# Model-TG VA Edition - Plaits Edition

**Unofficial firmware additions for the Elektron Model:Cycles (OS 1.13):
a full Sampler machine, a two-oscillator VA synth machine, three analogue-
style drum machines, a Plaits-style macro machine with ten engines, a DX7-style FM machine, four
LFOs a track, a trig generator, resampling, beat-repeat with master FX, and
more.**

> **The Plaits Edition is this fork's main version** (branch `main`):
> everything in the VA Edition plus **PLAITS**, **DX** and **LXR**
> ([docs/PLAITS.md](docs/PLAITS.md), [docs/DX.md](docs/DX.md)). The earlier VA
> Edition stays on the branch `va-machine`.
>
> **Install it from your browser: <https://drusanow.github.io/Model-TG-VA-Edition/>**

This is a fork of [TinyGregAudio/Model-TG](https://github.com/TinyGregAudio/Model-TG)
that adds four machines - **VA** (index 7) and **VA KICK**, **VA SNARE**,
**VA HIHAT** (8..10) - **three more LFOs** on every track and a **trig
generator**, and keeps everything else Model-TG does. See
**[docs/VA.md](docs/VA.md)**, **[docs/DRUMS.md](docs/DRUMS.md)**,
**[docs/LFO2.md](docs/LFO2.md)** and **[docs/GEN.md](docs/GEN.md)** for how
they work and what has been verified. **The drum machines, LFO 2-4 and the
trig generator have not yet been tested on hardware**: back up first.

Model-TG is built on your computer from your own copy of the stock OS. It
patches new code into the firmware, so everything the Cycles already does
keeps working.

> **Unofficial.** Not made, supported or endorsed by Elektron. You install it
> at your own risk: back up first, and see [going back to stock](docs/BUILD.md#going-back-to-stock).
> No firmware images are distributed here or anywhere by this project.
> [Build your own](docs/BUILD.md).

## What it adds

- **A VA machine** (this fork), the eighth: two band-limited oscillators
  (saw, square, triangle each), OSC 2 tuning and a mix, with gentle
  analogue drift, through the same Attack, Amp Decay, Filter and Resonance
  as the stock machines. All four parameters p-lock and take LFOs.
- **VA KICK, VA SNARE, VA HIHAT** (this fork), after the VA: a gritty
  sine kick with pitch sweep, click and drive; a dropping sine with filtered
  white noise, a noise mix, pitch drop and drive; and the TR-808's six
  detuned square waves for hats, with tone, spread, noise and drive. Low
  CPU, integer DSP; every knob p-locks and takes LFOs.
- **PLAITS** (Plaits Edition), the twelfth machine: ten engines after
  Mutable Instruments Plaits - waveshaping, 2-op FM, a granular cloud,
  formants (grains), phase distortion, a chiptune arpeggiator, filtered
  noise, a struck modal resonator, particles and a plucked string - on Plaits' four controls: Contour picks the engine,
  Color / Shape / Sweep are HARMONICS / TIMBRE / MORPH, labelled per engine.
  Integer remakes of Plaits' algorithms (MIT, credited in docs/PLAITS.md).
- **DX** (Plaits Edition), the thirteenth machine: a 6-operator FM voice per
  track after the DX7, as Dexed's engine (msfa, Apache 2.0) models it - the
  32 algorithms, envelopes, keyboard scaling, feedback, LFO - playing DX7
  voices, checked against msfa voice by voice: 63 built in (Legowelt's free
  "Magic DX7" cartridge, a DX9 cartridge and its own), or your cartridges. The voice plays as its
  cartridge has it; three macros move it without breaking it: Color is
  Bright (the modulators' levels), Shape Env Time (all its envelopes
  shorter or longer), Sweep the feedback, SETTINGS + Shape the algorithm,
  and Contour
  picks the voice ([docs/DX.md](docs/DX.md)).
- **LXR** (Plaits Edition), the fourteenth machine: drum voices after the
  Erica Synths LXR-02 (a modified, fixed-point port of its GPL-3 firmware's
  voices; not Erica Synths' firmware, nor endorsed by them). Contour picks
  one of eight engines - KICK, TOM, PERC (FM drum voices with pitch and FM
  envelopes), SNAR, CLAP, CHAT, OHAT, CYMB - and Color / Shape / Sweep
  are three macros, labelled per engine; Attack, Filter, Resonance and Decay
  are the ones every machine has ([USER_GUIDE](docs/USER_GUIDE.md#lxr)).
- **A trig generator** (this fork): **SETTINGS** + **TRACK** fills the
  selected track with random trigs at a chosen density, or a Euclidean
  rhythm (hits and rotation), optionally with random notes in a key, scale
  and octave range, and random velocities in a range.
- **Three more LFOs** (this fork) on every track: tap **LFO** again for
  LFO 2, 3 and 4; hold **LFO** and turn a knob to set the destination of
  the LFO showing. Same waves, sync and destinations as the stock LFO, never
  two on one destination; they share LFO 1's Setup (Trig Mode, Fade,
  Phase). All four are saved with the pattern and reset with the track's
  sound; LFO 2 p-locks, LFO 3 and 4 do not (the Cycles' sound storage is
  full - they are kept in a spare part of the pattern's kit record).
  Also fixes Model-TG's Attack/Filter/Resonance p-locks, which landed on
  the wrong parameters after a pattern was saved and reloaded.
- **Quantised mute and track solo** (this fork): mutes wait for the next
  bar (or the pattern's top, or happen at once: Device Config > Mute Q), and
  **SETTINGS** + a pad solos that track, on the bar too. See
  [docs/MUTESOLO.md](docs/MUTESOLO.md).
- **Alternative send effects** (this fork): **SETTINGS** + **PAGE** opens
  Send FX, which puts a ping-pong, tape or chorus/flanger in the delay's
  place, and a phaser, spring, ring modulator, distortion or tuned resonator
  in the reverb's.
  The FX page's knobs drive them, and each pattern keeps its own. See
  [docs/SENDFX.md](docs/SENDFX.md).
- **LFOs to MIDI CC** (this fork): a **MIDI CC** row in the LFO menu sends
  any of the four LFOs of a track to a CC on that track's MIDI channel,
  sweeping from a Base value you set, as well as (or instead of) its
  Destination. See
  [docs/LFOCC.md](docs/LFOCC.md).
- **A Sampler machine**, the seventh machine on any track. It plays samples
  from the Cycles' internal storage, a different sample per pattern, with
  p-lockable per-step sample locks.
- **Seven playback modes:** One shot, Loop, Slice, Granular, Stretch
  (time-stretch), Pluck (Karplus-Strong) and Wave (wavetable), each with its
  own options.
- **Slicing:** equal, by transients, or your own markers set in a
  full-screen slice editor. You can tap them in time, snap them to zero
  crossings, and set a pattern's trigs to walk the slices with one press.
- **Resampling** of a track, the whole output or the computer's USB audio,
  in mono or stereo, straight onto a track, then saved to internal storage.
- **Stereo samples** in every mode.
- **Retrig page:** beat-repeat on the tempo grid, plus 12 master FX (drive,
  crush, rate, pump, filter, ring mod, noise riser, gate, tape stop, vinyl,
  flanger).
- **Attack, Filter and Resonance** added to all the stock machines.
- **Slide trigs:** a trig that glides every differing parameter, p-locks
  included, from the trig before it, on any machine.
- **Scale Lock**, **sample upload through Elektron Transfer**, a **System**
  page with CPU and memory use, and lower CPU use overall.

The full, button-by-button guide is in **[docs/USER_GUIDE.md](docs/USER_GUIDE.md)**, and every
knob, macro and shortcut fits on two pages in the
**[Quick Reference (PDF)](flasher/Model-TG-Plaits-Quick-Reference.pdf)**
(also on the [web flasher](https://drusanow.github.io/Model-TG-VA-Edition/Model-TG-Plaits-Quick-Reference.pdf)).

## Getting it

The easiest way is the **web flasher: <https://drusanow.github.io/Model-TG-VA-Edition/>**
(source in [`flasher/`](flasher/), see [docs/FLASHER.md](docs/FLASHER.md)). In
Chrome, Edge or Opera on a computer:

1. Download the stock **Model:Cycles OS 1.13** from elektron.se and pick it on
   the page. It builds the Plaits Edition right there in your browser; your
   file never leaves your computer.
2. Connect the Cycles over USB (Device Config > Transfer set to **CYC**),
   close Elektron Transfer, click **Find the Cycles**, then **Send to the
   Cycles** (about 30 seconds).
3. Confirm the update on the Cycles' screen (**YES**). It restarts by itself.

Or save the file from the page and send it with Elektron Transfer, as an
official OS update. The page always builds the latest version on `main`.

Or build it yourself:

```sh
git clone https://github.com/drusanow/model-tg-va-edition
cd model-tg-va-edition
python3 build.py --stock path/to/model-cycles_OS1.13.syx
```

Then install `Model-TG.syx` like an official OS update (choose another name
with `--out`). The tools you need,
and how to go back to stock, are in **[docs/BUILD.md](docs/BUILD.md)**.


## Contributing

Pull requests for new behaviour are welcome. See
[CONTRIBUTING.md](CONTRIBUTING.md) to get started, and
[docs/INTERNALS.md](docs/INTERNALS.md) for how the firmware is patched and
what is already known about it.

## Credits

- [Model-TG](https://github.com/TinyGregAudio/Model-TG) by TinyGregAudio, which
  this fork extends.

- [elektron-firmware-tool](https://github.com/mischa85/elektron-firmware-tool)
  unpacks and repacks the OS.
- [drumkilla/elektron-model-tweaks](https://github.com/drumkilla/elektron-model-tweaks)
  provides the latching mute, trig preview and browser scroll tweaks (MIT,
  vendored in `tweaks/`).

## License

[MIT](LICENSE) for the code and documentation here, except `src/lxr/`
(the LXR machine): GPL-3.0-only, derived from the Erica Synths LXR-02
firmware; the firmware patch this repository builds includes it and is
therefore conveyed under GPL-3.0-only as a whole ([src/lxr/NOTICE.md](src/lxr/NOTICE.md)).
"Erica Synths", "LXR" and "LXR-02" are Erica Synths' names; this is not
their firmware and is not endorsed by them. Elektron's firmware is not part
of this project and is not covered by it. "Elektron" and
"Model:Cycles" are trademarks of Elektron Music Machines; they are used here
only to say what this works with.
