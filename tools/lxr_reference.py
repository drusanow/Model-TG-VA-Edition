#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only (part of LXR: src/lxr/NOTICE.md - it carries the LXR-02 firmware's formulas or reads its sources)
"""Renders the Erica Synths LXR-02's own voices (its firmware's sources, built on this host by tools/lxr_ref/) with its
init kit, and writes what the LXR machine is checked against into tests/data/lxr_ref.json: for each of the seven
voices (the kit's three drum voices, snare, closed and open hat, cymbal - LXR presets KICK, TOM, PERC, SNAR, CHAT,
OHAT, CYMB), its envelope (10 ms RMS, dB, half a second), its spectrum (energy in bands, dB of the total, the first
300 ms) and, for the tonal voices, its pitch at the start and at 50 ms. Numbers only: no audio and no LXR code is
written into the repository.

    python3 tools/lxr_reference.py path/to/lxr-desktop-app [--wav DIR]

Needs gcc. The LXR's float-to-unsigned conversion of its phase modulation is given its ARM meaning (negative -> 0;
see src/lxr/lxr.c, pmod) - the host's x86 conversion would wrap instead.
"""
import json, os, re, shutil, subprocess, sys, tempfile, wave

import numpy as np

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
HERE = os.path.join(REPO, 'tools', 'lxr_ref')
OUT = os.path.join(REPO, 'tests', 'data', 'lxr_ref.json')
DSP = ['DrumVoice', 'Decay', 'SlopeEg2', 'snapEg', 'transientGenerator', 'transientTables', 'ResonantFilter',
       'distortion', 'BufferTools', 'random', '1PoleLp', 'dither', 'squareRootLut', 'wavetable', 'Snare', 'HiHat',
       'CymbalVoice', 'Samples', 'lfo', 'TiltEq']
VOICES = [('KICK', 0), ('TOM', 1), ('PERC', 2), ('SNAR', 3), ('CHAT', 5), ('OHAT', 6), ('CYMB', 4)]
TONAL = ('KICK', 'TOM', 'PERC', 'SNAR')
BANDS = [(50, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]
FS = 44140


def features(x, fs):
    """the measures, at any rate: tests/test_lxr.py computes the same of the machine's output"""
    x = np.asarray(x, float)
    # up to 20 kHz only: at 48 kHz a metallic hat has energy above the LXR's 22 kHz, which nobody hears
    sp = np.fft.rfft(x)
    sp[np.fft.rfftfreq(len(x), 1 / fs) > 20000] = 0
    x = np.fft.irfft(sp, len(x))
    w = int(round(fs / 100))
    env = [float(20 * np.log10(np.sqrt(np.mean(x[i * w:(i + 1) * w] ** 2)) + 1)) for i in range(50)]
    seg = x[:int(0.3 * fs)]
    n = 1024
    k = (len(seg) - n) // (n // 2)
    win = np.hanning(n)
    spec = np.mean([np.abs(np.fft.rfft(seg[i * n // 2:i * n // 2 + n] * win)) ** 2 for i in range(k)], 0)
    fr = np.fft.rfftfreq(n, 1 / fs)
    tot = spec.sum()
    bands = [float(10 * np.log10(spec[(fr >= a) & (fr < b)].sum() / tot + 1e-12)) for a, b in BANDS]

    def f0(t0):                                      # over 46 ms, whatever the rate
        m = int(round(0.0464 * fs))
        s = x[int(t0 * fs):int(t0 * fs) + m] * np.hanning(m)
        f = np.fft.rfftfreq(1 << 16, 1 / fs)
        return float(f[np.argmax(np.abs(np.fft.rfft(s, 1 << 16)))])
    return {'env': env, 'bands': bands, 'f0': [f0(0.0), f0(0.05)]}


def build(lxr, tmp):
    core = os.path.join(lxr, 'Core')
    inc = ['-I' + tmp, '-I' + os.path.join(core, 'Inc'), '-I' + os.path.join(core, 'Inc', 'Soundengine'),
           '-I' + os.path.join(core, 'Src', 'Soundengine', 'DSPAudio')]
    shutil.copy(os.path.join(HERE, 'stm32f7xx_hal.h'), tmp)
    open(os.path.join(tmp, 'stm32f7xx.h'), 'w').write('#include "stm32f7xx_hal.h"\n')
    open(os.path.join(tmp, 'usbd_cdc.h'), 'w').write('#pragma once\n#include "stm32f7xx_hal.h"\n')
    osc = open(os.path.join(core, 'Src', 'Soundengine', 'DSPAudio', 'Oscillator.c')).read()
    osc, k = re.subn(r'\(\(uint32_t\)\((modBuffer\[i\]|modOsc->output)\*osc->fmMod\)\)', r'arm_u32(\1*osc->fmMod)', osc)
    assert k >= 4, k
    osc = ('static inline unsigned int arm_u32(float x) '
           '{ return x <= 0.f ? 0u : (x >= 4294967295.f ? 4294967295u : (unsigned int)x); }\n') + osc
    open(os.path.join(tmp, 'Oscillator.c'), 'w').write(osc)
    srcs = [os.path.join(core, 'Src', 'Soundengine', 'DSPAudio', f + '.c') for f in DSP]
    srcs += [os.path.join(tmp, 'Oscillator.c'), os.path.join(core, 'Src', 'Soundengine', 'MIDI', 'MidiParser.c'),
             os.path.join(core, 'Src', 'Soundengine', 'MIDI', 'ParameterArray.c'),
             os.path.join(core, 'Src', 'Preset', 'initKit.c'),
             os.path.join(HERE, 'stubs.c'), os.path.join(HERE, 'driver.c')]
    exe = os.path.join(tmp, 'lxref')
    subprocess.run(['gcc', '-O1', '-w'] + inc + srcs + ['-o', exe, '-lm'], check=True)
    return exe


def main():
    lxr = sys.argv[1]
    wavdir = sys.argv[sys.argv.index('--wav') + 1] if '--wav' in sys.argv else None
    ref = {'fs': FS, 'bands': BANDS, 'voices': {}}
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(lxr, tmp)
        for name, v in VOICES:
            f = os.path.join(wavdir or tmp, f'lxr_{name}.wav')
            subprocess.run([exe, str(v), f, '1.0'], check=True)
            w = wave.open(f)
            x = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float)
            m = features(x, FS)
            if name not in TONAL:
                del m['f0']
            ref['voices'][name] = m
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(ref, open(OUT, 'w'), indent=1)
    print('wrote tests/data/lxr_ref.json')


if __name__ == '__main__':
    main()
