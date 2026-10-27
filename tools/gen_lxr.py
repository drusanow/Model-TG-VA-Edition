#!/usr/bin/env python3
"""Writes LXR's generated files (machine 13, src/lxr/, GPL-3.0-only):

  src/lxr/lxr_tables.h  the band-limited triangle, saw and square tables, eleven octave bands of 512 entries
                        (additive synthesis, computed here); the sine is the stock OS's, in its SRAM (lxr.c)
  src/lxr/lxr_gen.s     src/lxr/lxr.c compiled for the ColdFire (gcc -S), every section moved to the overlay
                        (.ovl, build.py), checked to call nothing outside itself (no FPU, no libgcc)

The firmware build stays an assembler-only build: build.py assembles lxr_gen.s. Rerun after changing lxr.c; CI
checks that both files are up to date. The assembly is gcc 13.3's (m68k-linux-gnu, Ubuntu 24.04); another gcc
gives other bytes, so --check then compares the tables only and says so.

    python3 tools/gen_lxr.py [--check]
"""
import math, os, re, shutil, subprocess, sys

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
D = os.path.join(REPO, 'src', 'lxr')
TABLES = os.path.join(D, 'lxr_tables.h')
GEN = os.path.join(D, 'lxr_gen.s')
BANDS, N, FS = 11, 512, 48000
CC = os.environ.get('LXR_CC') or 'm68k-linux-gnu-gcc'
CFLAGS = ['-mcpu=54418', '-O2', '-fomit-frame-pointer', '-ffreestanding', '-fno-builtin', '-nostdlib', '-fno-pic',
          '-fno-common', '-fno-tree-loop-distribute-patterns', '-fno-jump-tables', '-Wall', '-Werror']
HEAD = '/* Written by tools/gen_lxr.py - do not edit. GPL-3.0-only (src/lxr/LICENSE). */\n'


def wave(kind, band):
    """one period, band-limited for fundamentals up to 55 x 2^band Hz (band k: the increments band_of gives it)"""
    top = 55.0 * 2 ** band
    nh = max(1, int(20000 / top))
    out = []
    for i in range(N):
        x = 2 * math.pi * i / N
        if kind == 'tri':                        # odd harmonics, alternating, 1/n^2
            s = sum((-1) ** ((n - 1) // 2) * math.sin(n * x) / n ** 2 for n in range(1, nh + 1, 2)) * 8 / math.pi ** 2
        elif kind == 'saw':                      # all harmonics, 1/n
            s = -sum(math.sin(n * x) / n for n in range(1, nh + 1)) * 2 / math.pi
        else:                                    # square: odd harmonics, 1/n
            s = sum(math.sin(n * x) / n for n in range(1, nh + 1, 2)) * 4 / math.pi
        out.append(s)
    peak = max(abs(v) for v in out) or 1
    return [int(round(v / peak * 30000)) for v in out]


def tables():
    t = [HEAD, '#define LX_BANDS %d\n' % BANDS]
    t.append('static const short lx_wav[3][%d][%d] __attribute__((section(".lxr_ro"))) = {\n' % (BANDS, N))
    for kind in ('tri', 'saw', 'rec'):
        t.append(' {\n')
        for b in range(BANDS):
            w = wave(kind, b)
            t.append('  {' + ', '.join(map(str, w)) + '},\n')
        t.append(' },\n')
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
