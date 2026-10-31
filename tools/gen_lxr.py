#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only (part of LXR: src/lxr/NOTICE.md - it carries the LXR-02 firmware's formulas or reads its sources)
"""Writes LXR's generated files (machine 13, src/lxr/, GPL-3.0-only):

  src/lxr/lxr_tables.h  the LXR-02's knob curves, one value per knob position (0..127), each evaluated here with the
                        LXR's own formula (Decay.c, SlopeEg2.c, MidiParser.c, ResonantFilter.c, valueShaper.h, ...)
                        in the LXR's float or double arithmetic, then turned to fixed point for lxr.c; and the tables
                        of its filter's soft clip and fast tanh
  src/lxr/lxr_gen.s     src/lxr/lxr.c compiled for the ColdFire (gcc -S), every section moved to the overlay
                        (.ovl, build.py), checked to call nothing outside itself (no FPU, no libgcc)

The LXR's wavetables and init kit are imported once by tools/lxr_import.py (lxr_wave.h, lxr_presets.h). The firmware
build stays an assembler-only build: build.py assembles lxr_gen.s. Rerun after changing lxr.c; CI checks that both
files are up to date. The assembly is gcc 13.3's (m68k-linux-gnu, Ubuntu 24.04); another gcc gives other bytes, so
--check then compares the tables only and says so.

    python3 tools/gen_lxr.py [--check]
"""
import math, os, re, shutil, subprocess, sys

import numpy as np

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
D = os.path.join(REPO, 'src', 'lxr')
TABLES = os.path.join(D, 'lxr_tables.h')
GEN = os.path.join(D, 'lxr_gen.s')
CC = os.environ.get('LXR_CC') or 'm68k-linux-gnu-gcc'
CFLAGS = ['-mcpu=54418', '-O2', '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-pic',
          '-fno-common', '-fno-tree-loop-distribute-patterns', '-fno-jump-tables', '-Wall', '-Werror']
HEAD = '/* Written by tools/gen_lxr.py - do not edit. GPL-3.0-only (src/lxr/LICENSE). */\n'


F = np.float32


def lxr_curves():
    """the LXR's curves, as it computes them (float32 where it uses float, double where double)"""
    d = np.arange(128)
    flt = (d / F(127)).astype(F)                       # value.flt
    c = {}
    # Decay.c, DecayEg_calcTime (USE_BIG_SAMPLE_BUFFER): the pitch EG's step a 64-sample block
    tk = F(2 * 0.99 / (1 - 0.99))
    v = (d / F(127)).astype(F)
    r = ((F(1) - ((F(1) + tk) * v / (F(1) + tk * np.abs(v)))) * F(2)).astype(F)
    c['pdec'] = np.minimum(r, F(1))
    # SlopeEg2.c, slopeEg2_calcTime: attack (0.99) and decay (0.999), double, returned through a float
    def slope_time(amount):
        val = d / 127.0
        ret = ((1.0 - ((1.0 + amount) * val / (1.0 + amount * np.abs(val)))) * 2.0).astype(F)
        return np.minimum(ret, F(1))
    c['att'] = slope_time(2 * 0.99 / (1.0 - 0.99))
    c['dec'] = slope_time(2 * 0.999 / (1.0 - 0.999))
    # the slopes, as a = k / (2 + k) (so that k = 2a / (1 - a)): DecayEg_setSlope x 2, slopeEg2_setSlope x 1.999
    c['psl'] = ((flt - F(0.5)) * F(2)).astype(F)
    c['vsl'] = ((flt - F(0.5)) * F(1.999)).astype(F)
    # MidiParser.c, calcPitchModAmount: (x / 127)^2 x PITCH_AMOUNT_FACTOR (32)
    c['pamt'] = (flt * flt * F(32)).astype(F)
    # MidiNoteNumbers.h and midiParser_calcDetune: +-1 semitone x (x / 127 - 0.5)
    c['freq'] = 440.0 * 2.0 ** ((d - 69) / 12.0)
    c['det'] = (F(1) + (flt - F(0.5)) * (F(1.0594630943592952645618252949463) - F(1))).astype(F)
    # snapEg.c: falls by SNAP_REDUCTION (0.2) x the transient's pitch a block; pitch = 1 + (x / 33.9 - 0.75)
    pitch = (F(1) + ((d / F(33.9)).astype(F) - F(0.75))).astype(F)
    c['snap'] = (F(0.2) * pitch).astype(F)
    # the filter: valueShaperF2F(x / 127, FILTER_SHAPER = -0.9), f = x 0.45, g = fastTan(pi f); reso: q = 1 - x / 127
    # (under 0.1: 0.02); SVF_setDrive: 0.4 + x^2 x 6
    k = F(2 * -0.9 / (1.0001 - -0.9))
    sh = ((F(1) + k) * flt / (F(1) + k * np.abs(flt))).astype(F)
    f = (sh * F(0.5 * 0.90)).astype(F)
    x = (F(math.pi) * f).astype(F)
    c['ff'] = f
    c['fg'] = ((-15 * x + x * x * x) / (F(3) * (F(-5) + F(2) * x * x))).astype(F)
    q = (F(1) - flt).astype(F)
    c['fq'] = np.where(q < F(0.1), F(0.02), q)
    c['fdrv'] = (F(0.4) + flt * flt * F(6)).astype(F)
    c['flp2'] = (f * F(2.21)).astype(F)
    # distortion.c, setDistortionShape: shape = x / 127 - 1/128 (k = 2 shape / (1 - shape))
    c['dist'] = (flt - F(0.0078125)).astype(F)
    # mixer.c's decimator: valueShaperF2F(x / 127, -0.7)
    k = F(2 * -0.7 / (1.0001 - -0.7))
    c['decim'] = ((F(1) + k) * flt / (F(1) + k * np.abs(flt))).astype(F)
    return c


def soft_tables():
    """ResonantFilter.c's softClipTwo over -16..16 (64 steps a unit) and fastTanh over -2..2 (128 a unit)"""
    def soft(x):
        a = (0.5 * x) ** 2
        return x * ((a + 105) * a + 945) / ((15 * a + 420) * a + 945)
    s = [round(soft(i / 64 - 16) * 4096) for i in range(2049)]          # -16 .. 16
    t = []
    for i in range(513):                                                  # -2 .. 2
        y = i / 128 - 2
        r = 1.0 if y > 1.95 else (-1.0 if y < -1.95 else 4.15 * y / (4.29 + y * y))
        t.append(max(-32767, min(32767, round(r * 32767))))
    return s, t


def tables():
    c = lxr_curves()
    fix = {  # name: (scale, C type)
        'pdec': (1 << 24, 'u32'), 'att': (1 << 24, 'u32'), 'dec': (1 << 24, 'u32'),
        'psl': (32768, 's32'), 'vsl': (32768, 's32'), 'pamt': (4096, 's32'),
        'freq': (256, 'u32'), 'det': (65536, 'u32'), 'snap': (1 << 24, 'u32'),
        'ff': (65536, 's32'), 'fg': (4096, 's32'), 'fq': (4096, 's32'), 'fdrv': (4096, 's32'),
        'flp2': (4096, 's32'), 'dist': (32768, 's32'), 'decim': (65536, 'u32')}
    t = [HEAD, '/* the LXR-02\'s knob curves (tools/gen_lxr.py: lxr_curves) and its filter\'s shapers, fixed point */\n']
    for name, (scale, typ) in fix.items():
        vals = [int(round(float(v) * scale)) for v in c[name]]
        if name in ('psl', 'vsl'):
            vals = [max(-32768, min(32768, v)) for v in vals]
        t.append('static const %s lx_%s[128] __attribute__((section(".lxr_ro"))) = {\n' % (typ, name))
        for i in range(0, 128, 16):
            t.append('\t' + ', '.join(map(str, vals[i:i + 16])) + ',\n')
        t.append('};\n')
    s, h = soft_tables()
    t.append('static const s32 lx_soft[2049] __attribute__((section(".lxr_ro"))) = {\n')
    for i in range(0, 2049, 16):
        t.append('\t' + ', '.join(map(str, s[i:i + 16])) + ',\n')
    t.append('};\n')
    t.append('static const s32 lx_tanh[513] __attribute__((section(".lxr_ro"))) = {\n')
    for i in range(0, 513, 16):
        t.append('\t' + ', '.join(map(str, h[i:i + 16])) + ',\n')
    t.append('};\n')
    return ''.join(t)


def compile_asm(tables_text):
    if not shutil.which(CC):
        return None
    os.makedirs(D, exist_ok=True)
    open(TABLES, 'w').write(tables_text)
    r = subprocess.run([CC, *CFLAGS, '-S', '-o', '-', os.path.join(D, 'lxr.c')], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stderr)
    out = [HEAD.replace('/*', '|').replace('*/', ''),
           '| from src/lxr/lxr.c by ' + subprocess.run([CC, '--version'], capture_output=True, text=True)
           .stdout.splitlines()[0] + ', ' + ' '.join(CFLAGS) + '\n']
    for line in r.stdout.splitlines():
        st = line.strip()
        if st.startswith(('.file', '.ident')) or '.note.GNU-stack' in st:
            continue
        if st in ('.text', '.data', '.bss') or st.startswith(('.section', '.local', '.comm')):
            if st.startswith(('.local', '.comm')):
                raise SystemExit(f'!! common symbol in lxr.c: {st}')
            line = '\t.section\t.ovl,"ax"'
        out.append(line + '\n')
    text = ''.join(out)
    calls = set(re.findall(r'\b(?:jsr|jbsr|bsr|jmp|jra)\s+([A-Za-z_]\w*)', text))
    local = set(re.findall(r'^([A-Za-z_]\w*):', text, re.M))
    if calls - local:
        raise SystemExit(f'!! lxr.c calls outside itself: {sorted(calls - local)}')
    if re.search(r'\bf(?:move|add|mul|div|sub)\b', text):
        raise SystemExit('!! FPU instructions in lxr_gen.s')
    return text


def main():
    check = '--check' in sys.argv
    tab = tables()
    old_tab = open(TABLES).read() if os.path.exists(TABLES) else None
    old_gen = open(GEN).read() if os.path.exists(GEN) else None
    asm = compile_asm(tab)
    if check:
        open(TABLES, 'w').write(old_tab or '')
        ok_tab = old_tab == tab
        ok_gen = asm is None or old_gen == asm
        print(f'lxr_tables.h {"up to date" if ok_tab else "OUT OF DATE"}; lxr_gen.s '
              f'{"not checked (no " + CC + ")" if asm is None else ("up to date" if ok_gen else "OUT OF DATE")}')
        sys.exit(0 if ok_tab and ok_gen else 1)
    if asm is None:
        raise SystemExit(f'!! {CC} not found (lxr_gen.s needs it)')
    open(GEN, 'w').write(asm)
    print(f'wrote src/lxr/lxr_tables.h, src/lxr/lxr_gen.s ({asm.count(chr(10))} lines)')


if __name__ == '__main__':
    main()
