# LXR (machine 13): licence and origin

`src/lxr/` (`lxr.c`; the files `tools/gen_lxr.py` writes from it, `lxr_tables.h` and `lxr_gen.s`; the files
`tools/lxr_import.py` imports from the LXR's sources, `lxr_wave.h` and `lxr_presets.h`) and `tools/lxr_ref/` (the
reference renderer, which builds the LXR's own sources on a computer), with the tools that carry the LXR's formulas
or read its sources (`tools/gen_lxr.py`, `tools/lxr_import.py`, `tools/lxr_reference.py`), are free software under
the **GNU General
Public License version 3 only** ([LICENSE](LICENSE)), with the additional terms of the LXR firmware's licence notice
(GPL-3.0 section 7, below). `tests/data/lxr_ref.json` holds measurements of that renderer's output (numbers).

## Origin

It is a **modified version** of parts of the LXR-02 firmware by Erica Synths, released under GPL-3.0-only
(<https://github.com/erica-synths/lxr-desktop-app>, copyright (C) 2012-2026 Erica Synths): a fixed-point port, for
the Elektron Model:Cycles' ColdFire processor, of its drum voices (`DrumVoice.c`, `Snare.c`, `HiHat.c`,
`CymbalVoice.c`, `Oscillator.c`, `ResonantFilter.c`, `Decay.c`, `SlopeEg2.c`, `snapEg.c`, `distortion.c`, the
decimator of `mixer.c`), with its knob curves (`MidiParser.c`, `valueShaper.h`, ...), its wavetables
(`wavetable.c`) and its init kit (`initKit.c`). Changed here: fixed point in place of floating point; the voice run
at the LXR's rate and interpolated to the Cycles' 48 kHz; the stock OS's sine table read in place of the LXR's; eight
presets with three macros each; the glue to the Model:Cycles' voice loop. Left out: the LXR's sampled attack
transients (their source recordings carry their own terms), user samples, its LFOs, velocity routing and pan.

**This is not Erica Synths' firmware**, and it is not made, approved, supported or endorsed by Erica Synths. "Erica
Synths", "LXR" and "LXR-02" are Erica Synths' names; the machine's name on the Cycles says what its voices are derived
from, nothing more.

## The LXR licence's additional terms (GPL-3.0 section 7), as they apply here

1. Trademarks (7(e)): no right is granted in the names "Erica Synths", "LXR", "LXR-02" or their logos; they may not
   be used in a way that suggests that this comes from, is approved by or is affiliated with Erica Synths.
2. Marking (7(c)): modified versions must be marked as different from the original; this is a modified version, and
   it is presented as such everywhere it is described.

## What this means for the rest of the repository

The rest of this repository stays MIT ([../../LICENSE](../../LICENSE)). The firmware patch built from it (the
flasher's `model-tg.json`, a `.syx` you build) includes the compiled LXR code and is therefore conveyed under
GPL-3.0-only as a whole; this repository is its corresponding source. MIT code may be combined into a GPL-3 work;
the opposite is not allowed, so code from `src/lxr/` must not be copied into the MIT parts. Elektron's firmware is
not part of this project and is not covered by any of it: you build from your own official file.
