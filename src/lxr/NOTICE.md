# LXR (machine 13): licence and origin

`src/lxr/` (`lxr.c`, and the files `tools/gen_lxr.py` writes from it: `lxr_tables.h`, `lxr_gen.s`) is free
software under the **GNU General Public License version 3 only** ([LICENSE](LICENSE)), with the additional terms
of the LXR firmware's licence notice (GPL-3.0 section 7, below).

## Origin

It is a **modified version** of parts of the LXR-02 firmware by Erica Synths, released under GPL-3.0-only
(<https://github.com/erica-synths/lxr-desktop-app>, copyright (C) 2012-2026 Erica Synths): a fixed-point port, for
the Elektron Model:Cycles' ColdFire processor, of its drum voices (`DrumVoice.c`, `Snare.c`, `HiHat.c`,
`CymbalVoice.c`, `Oscillator.c`, `Decay.c`, `SlopeEg2.c`, `snapEg.c`, `distortion.c`). Changed here: fixed point in
place of floating point, 48 kHz in place of 44.1 kHz, our own band-limited tables and clicks (the LXR's sampled
transients are not used), a state-variable noise filter in place of its ZDF filter, eight engines with three macros
each, and the glue to the Model:Cycles' voice loop.

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
