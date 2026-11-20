#!/usr/bin/env python3
"""Writes src/achord/achord_tables.h, the fixed-point tables of ACHORD (machine 14, src/achord/achord.c):
  ac_inc12   the oscillator increments of MIDI notes 120..131 at 48 kHz (2^32 a cycle); a note n is
             ac_inc12[n % 12] >> (10 - n / 12)
  ac_semi    the semitone thresholds of the note's step (Q16, 1.0 = note 60): geometric midpoints from
             -48.5 to +48.5 semitones, to round a step to its note with no logarithm
  ac_ratio   2^(k/12), Q16, k = -48..48: the exact step of each note, for the fine tuning
  ac_dec     the wave envelope's decay a block (Q16 of what is left), for ENV DECAY 0..10: 30 ms to 4 s
  ac_spec    the 32 waves' spectra (four banks of eight: ANALOG, SYNC, DIGITAL, FM), harmonics 1..64, each its sine
             and cosine amplitude (s16): achord.c builds the wavetables from them at run time (ac_wt, five sizes).
             Each wave is drawn here (a waveform of 4096 samples, or its harmonics), its harmonics taken (FFT), and
             scaled to a common loudness (RMS 11000) under a peak of 30000 at every size
Rerun after changing them; --check verifies the file is up to date.
"""
import math, os, sys
import numpy as np

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(REPO, 'src', 'achord', 'achord_tables.h')
FS = 48000
HEAD = '/* Written by tools/gen_achord.py - do not edit. MIT (part of ACHORD, src/achord/achord.c). */\n'


def table(typ, name, vals, per=8):
    t = ['static const %s %s[%d] __attribute__((section(".lxr_ro"))) = {\n' % (typ, name, len(vals))]
    for i in range(0, len(vals), per):
        t.append('\t' + ', '.join(str(v) for v in vals[i:i + per]) + ',\n')
    t.append('};\n')
    return ''.join(t)


N = 4096
H = 64


def waves():
    """the 32 waves, each a waveform over one cycle (N samples) or ('spec', a): sine amplitudes of harmonics 1..H"""
    t = np.arange(N) / N
    h = np.arange(1, H + 1)
    saw = 1 - 2 * t
    sq = np.where(t < 0.5, 1.0, -1.0)
    pulse = lambda d: np.where(t < d, 1.0, -1.0) - (2 * d - 1)
    out = []
    # ANALOG: sine, triangle, a warm saw, saw, saw + square, square, pulse 25 %, pulse 10 %
    out += [np.sin(2 * np.pi * t), 2 * np.abs(2 * ((t + 0.25) % 1) - 1) - 1,
            ('spec', 2 / (np.pi * h) * np.exp(-(h / 9.0) ** 2)), saw, 0.6 * saw + 0.6 * sq, sq, pulse(0.25), pulse(0.1)]
    # SYNC: a saw hard-synced to the cycle, the slave 1x .. 7.5x
    out += [1 - 2 * ((t * r) % 1) for r in (1.0, 1.5, 2.0, 2.6, 3.3, 4.2, 5.5, 7.5)]
    # DIGITAL: a saw's harmonics with a resonant peak climbing them
    out += [('spec', (1 / h) * (1 + 6 * np.exp(-((h - c) / (0.6 + 0.2 * c)) ** 2))) for c in (2, 3, 5, 8, 12, 18, 27, 40)]
    # FM: two operators, the modulator at r x, index I
    out += [np.sin(2 * np.pi * t + i * np.sin(2 * np.pi * r * t))
            for r, i in ((1, 0.7), (1, 1.6), (1, 3.0), (2, 1.2), (2, 2.6), (3, 1.5), (3, 3.5), (1, 7.0))]
    return out


def spec_table():
    rows = []
    t = np.arange(256) / 256
    for w in waves():
        if isinstance(w, tuple):
            a, b = np.asarray(w[1], float), np.zeros(H)
        else:
            X = np.fft.rfft(np.asarray(w, float))[1:H + 1]
            a, b = -2 * X.imag / N, 2 * X.real / N
        peak = 0
        for l in range(5):                     # the five sizes: 64 >> l harmonics
            hn = H >> l
            k = np.arange(1, hn + 1)[:, None]
            x = (a[:hn, None] * np.sin(2 * np.pi * k * t) + b[:hn, None] * np.cos(2 * np.pi * k * t)).sum(0)
            peak = max(peak, np.max(np.abs(x)))
            if l == 0:
                rms = np.sqrt(np.mean(x ** 2))
        g = min(30000 / peak, 11000 / rms)
        rows.append([int(round(v)) for p in zip(a * g, b * g) for v in p])
    t = ['static const s16 ac_spec[32][%d] __attribute__((section(".lxr_ro"))) = {\n' % (2 * H)]
    for r in rows:
        assert max(abs(v) for v in r) < 32768
        t.append('\t{' + ', '.join(str(v) for v in r) + '},\n')
    t.append('};\n')
    return ''.join(t)


def tables():
    inc12 = [round(440 * 2 ** ((n - 69) / 12) * 2 ** 32 / FS) for n in range(120, 132)]
    assert max(inc12) < 2 ** 32
    semi = [round(65536 * 2 ** ((k - 0.5) / 12)) for k in range(-48, 50)]        # 98: the lower edge of k
    ratio = [round(65536 * 2 ** (k / 12)) for k in range(-48, 49)]
    dec = []
    for i in range(11):
        tau = 0.03 * (4.0 / 0.03) ** (i / 10)          # seconds to 1/e
        dec.append(round(65536 * (1 - math.exp(-(32 / FS) / tau))))
    return ''.join([HEAD, '/* the tables of ACHORD: see tools/gen_achord.py */\n',
                    table('u32', 'ac_inc12', inc12, 6), table('u32', 'ac_semi', semi), table('u32', 'ac_ratio', ratio),
                    table('u16', 'ac_dec', dec, 11), spec_table()])


def main():
    t = tables()
    if '--check' in sys.argv:
        old = open(OUT).read() if os.path.exists(OUT) else ''
        print('achord_tables.h ' + ('up to date' if old == t else 'OUT OF DATE'))
        sys.exit(0 if old == t else 1)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'w').write(t)
    print('wrote src/achord/achord_tables.h')


if __name__ == '__main__':
    main()
