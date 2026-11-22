#!/usr/bin/env python3
"""Writes src/achord/achord_tables.h, the fixed-point tables of ACHORD (machine 14, src/achord/achord.c):
  ac_inc12   the oscillator increments of MIDI notes 120..131 at 48 kHz (2^32 a cycle); a note n is
             ac_inc12[n % 12] >> (10 - n / 12)
  ac_semi    the semitone thresholds of the note's step (Q16, 1.0 = note 60): geometric midpoints from
             -48.5 to +48.5 semitones, to round a step to its note with no logarithm
  ac_ratio   2^(k/12), Q16, k = -48..48: the exact step of each note, for the fine tuning
Rerun after changing them; --check verifies the file is up to date.
"""
import math, os, sys

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


def tables():
    inc12 = [round(440 * 2 ** ((n - 69) / 12) * 2 ** 32 / FS) for n in range(120, 132)]
    assert max(inc12) < 2 ** 32
    semi = [round(65536 * 2 ** ((k - 0.5) / 12)) for k in range(-48, 50)]        # 98: the lower edge of k
    ratio = [round(65536 * 2 ** (k / 12)) for k in range(-48, 49)]
    return ''.join([HEAD, '/* the tables of ACHORD: see tools/gen_achord.py */\n',
                    table('u32', 'ac_inc12', inc12, 6), table('u32', 'ac_semi', semi), table('u32', 'ac_ratio', ratio)])


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
