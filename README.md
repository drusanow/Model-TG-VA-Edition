# Model-TG VA Edition

**Unofficial firmware additions for the Elektron Model:Cycles (OS 1.13):
a full Sampler machine, a two-oscillator VA synth machine, three analogue-
style drum machines, four LFOs a track, a trig generator, resampling,
beat-repeat with master FX, and more.**

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
- **A trig generator** (this fork): **SETTINGS** + **TRACK** fills the
  selected track with random trigs at a chosen density, or a Euclidean
  rhythm (hits and rotation), optionally with random notes in a key, scale
  and octave range, and random velocities in a range.
- **Three more LFOs** (this fork) on every track: press **LFO** again for
  LFO 2, 3 and 4. Same waves, sync and destinations as the stock LFO, never
  two on one destination; they share LFO 1's Setup (Trig Mode, Fade,
  Phase). LFO 2 p-locks and is saved; LFO 3 and 4 are not saved and do not
  p-lock (the Cycles' storage is full).
  Also fixes Model-TG's Attack/Filter/Resonance p-locks, which landed on
  the wrong parameters after a pattern was saved and reloaded.
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

The full, button-by-button guide is in **[docs/USER_GUIDE.md](docs/USER_GUIDE.md)**.

## Getting it

The easiest way is the web flasher in [`flasher/`](flasher/), published on
this fork's GitHub Pages (see [docs/FLASHER.md](docs/FLASHER.md)): pick your
stock OS 1.13 file and it builds Model-TG VA Edition right there in your
browser. (Upstream's [web flasher](https://tinygregaudio.github.io/Model-TG/)
builds plain Model-TG, without the VA.) Your file never leaves your computer. Install the result like an
official OS update.

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

[MIT](LICENSE) for the code and documentation here. Elektron's firmware is
not part of this project and is not covered by it. "Elektron" and
"Model:Cycles" are trademarks of Elektron Music Machines; they are used here
only to say what this works with.
