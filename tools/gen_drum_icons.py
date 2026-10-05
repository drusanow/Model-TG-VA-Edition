#!/usr/bin/env python3
"""Writes the machine-page icons of VA KICK, VA SNARE and VA HIHAT into
src/va_icons/, in the layout gen_va_icons.py uses (column-major, two
big-endian longs per column, bit n of the first long = row n, lit pixels
set), as line drawings in 2-pixel strokes:
  kick  - a bass drum from the front: head, beater and pedal, two feet
  snare - a snare drum from the side, its stick on the head
  hihat - two cymbals on their rod
Rerun after changing them; CI checks that the files are up to date. --show
prints them as text.
"""
import math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'va_icons')


def canvas(w, h):
    return [[0] * w for _ in range(h)]


def dot(px, x, y, r=1):
    h, w = len(px), len(px[0])
    for dx in range(-r, r + 1):
        for dy in range(-r, r + 1):
            X, Y = x + dx, y + dy
            if 0 <= X < w and 0 <= Y < h:
                px[Y][X] = 1


def line(px, x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        dot(px, round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n))


def curve(px, pts):
    for (a, b), (c, e) in zip(pts, pts[1:]):
        line(px, a, b, c, e)


def brush(px, x, y):
    """a 2x2 pen: strokes as heavy as the stock icons'"""
    h, w = len(px), len(px[0])
    for dx in (0, 1):
        for dy in (0, 1):
            X, Y = int(round(x)) + dx, int(round(y)) + dy
            if 0 <= X < w and 0 <= Y < h:
                px[Y][X] = 1


def seg(px, x0, y0, x1, y1):
    n = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
    for i in range(n + 1):
        brush(px, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n)


def ellipse(px, cx, cy, rx, ry, a0=0.0, a1=2 * math.pi):
    n = int(8 * (rx + ry)) + 8
    for i in range(n + 1):
        a = a0 + (a1 - a0) * i / n
        brush(px, cx + rx * math.cos(a) - 0.5, cy + ry * math.sin(a) - 0.5)


def kick(w, h):
    """a bass drum from the front: the head, the beater, two feet"""
    px = canvas(w, h)
    cx, cy, r = w / 2, h / 2 - 1, min(w, h) / 2 - 3
    ellipse(px, cx, cy, r, r)
    ellipse(px, cx, cy - r * 0.3, r * 0.2, r * 0.2)        # the beater's head
    seg(px, cx - 0.5, cy - r * 0.1, cx - 0.5, cy + r * 0.35)  # its stem
    ellipse(px, cx, cy + r * 0.62, r * 0.36, r * 0.3, math.pi, 2 * math.pi)  # the pedal
    for sx in (-1, 1):                                     # the feet
        brush(px, cx + sx * (r + 2) - 0.5, cy + r + 1)
    return px


def snare(w, h):
    """a snare drum from the side, its stick resting on the head"""
    px = canvas(w, h)
    cx = w / 2 + 1
    rx = min(w * 0.36, h * 0.48)
    ry = rx * 0.28
    top, bot = h * 0.45, h * 0.82
    ellipse(px, cx, top, rx, ry)                           # the head
    ellipse(px, cx, bot, rx, ry, 0, math.pi)               # the bottom rim
    seg(px, cx - rx - 0.5, top, cx - rx - 0.5, bot)        # the shell
    seg(px, cx + rx - 0.5, top, cx + rx - 0.5, bot)
    for f in (-0.5, 0.0, 0.5):                             # the lugs
        x = cx + f * rx - 0.5
        seg(px, x, top + ry * 0.95, x, bot + ry * 0.85 * math.cos(math.asin(f)))
    seg(px, cx - rx * 0.15, top - ry * 0.4, cx - rx * 1.05, top - h * 0.38)   # the stick
    return px


def hihat(w, h):
    """a hi-hat: two cymbals on their rod"""
    px = canvas(w, h)
    cx = w / 2
    rx = min(w * 0.36, h * 0.5)
    ry = rx * 0.18
    seg(px, cx - 0.5, h * 0.1, cx - 0.5, h * 0.92)        # the rod
    ellipse(px, cx, h * 0.42, rx, ry)                      # the top cymbal
    ellipse(px, cx, h * 0.58, rx, ry, 0, math.pi)          # the bottom one
    seg(px, cx - rx * 0.95, h * 0.58, cx - rx * 0.6, h * 0.58)
    seg(px, cx + rx * 0.6, h * 0.58, cx + rx * 0.95, h * 0.58)
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
        b += (lo.to_bytes(4, 'big') + hi.to_bytes(4, 'big'))
    return b


icons = {}
for name, fn in (('kick', kick), ('snare', snare), ('hihat', hihat)):
    icons[f'{name}_icon_A_48x33.bin'] = (48, 33, fn(48, 33))
    icons[f'{name}_icon_B_34x34.bin'] = (34, 34, fn(34, 34))
os.makedirs(d, exist_ok=True)
for name, (w, h, px) in icons.items():
    data = pack(px, w, h)
    assert len(data) == w * 8
    open(os.path.join(d, name), 'wb').write(data)
    print(f"wrote src/va_icons/{name} ({len(data)} B)")
    if '--show' in sys.argv:
        for row in px:
            print(''.join('#' if v else '.' for v in row))
