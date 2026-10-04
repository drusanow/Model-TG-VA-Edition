#!/usr/bin/env python3
"""Writes the VA machine's two machine-page icons, src/va_icons/*.bin, in the
layout the Sampler's (src/sampler_icons/) use: column-major, two big-endian
longs per column (ceil(h/32) words), bit n of the first long = row n, lit
pixels set. The picture is a sawtooth in the Sampler icons' 3-pixel strokes:
two ramps with their vertical resets. Rerun after changing it; CI checks
that the files are up to date. --show prints them as text.
"""
import os, struct, sys

d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'va_icons')


def saw(w, h, top, bot, x0, period, cycles):
    px = [[0] * w for _ in range(h)]

    def dot(x, y):
        for dx in range(3):
            for dy in range(3):
                X, Y = x + dx - 1, y + dy - 1
                if 0 <= X < w and 0 <= Y < h:
                    px[Y][X] = 1
    for c in range(cycles):
        xa = x0 + c * period
        for i in range(period):           # the ramp, rising left to right
            dot(xa + i, round(bot - (bot - top) * i / (period - 1)))
        xr = xa + period - 1              # the reset, straight down
        for y in range(top, bot + 1):
            dot(xr, y)
    return px


def pack(px, w, h):
    b = b''
    for x in range(w):
        lo = hi = 0
        for y in range(h):
            if px[y][x]:
                if y < 32:
                    lo |= 1 << y
                else:
                    hi |= 1 << (y - 32)
        b += struct.pack('>II', lo, hi)
    return b


icons = {
    # 48x33 like sampler_icon_A: the Sampler's bars span rows 0..30
    'va_icon_A_48x33.bin': (48, 33, saw(48, 33, 3, 27, 4, 20, 2)),
    # 34x34 like sampler_icon_B: its bars span rows 0..30
    'va_icon_B_34x34.bin': (34, 34, saw(34, 34, 3, 27, 3, 14, 2)),
}
os.makedirs(d, exist_ok=True)
for name, (w, h, px) in icons.items():
    data = pack(px, w, h)
    assert len(data) == w * 8
    open(os.path.join(d, name), 'wb').write(data)
    print(f"wrote src/va_icons/{name} ({len(data)} B)")
    if '--show' in sys.argv:
        for row in px:
            print(''.join('#' if v else '.' for v in row))
