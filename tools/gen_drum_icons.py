#!/usr/bin/env python3
"""Writes the machine-page icons of VA KICK, VA SNARE and VA HIHAT into
src/va_icons/, in the layout gen_va_icons.py uses (column-major, two
big-endian longs per column, bit n of the first long = row n, lit pixels
set) and its 3-pixel strokes:
  kick  - one decaying sine swing, its pitch falling
  snare - a short sine swing into a burst of noise
  hihat - a comb of short metallic spikes
Rerun after changing them; CI checks that the files are up to date. --show
prints them as text.
"""
import math, os, random, sys

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


def kick(w, h, top, bot):
    px = canvas(w, h)
    mid, amp = (top + bot) / 2, (bot - top) / 2
    pts, ph = [], 0.0
    for x in range(2, w - 2):
        t = (x - 2) / (w - 5)
        ph += 0.9 * math.exp(-2.2 * t) + 0.12        # the falling pitch
        pts.append((x, round(mid - amp * math.exp(-1.6 * t) * math.sin(ph))))
    curve(px, pts)
    return px


def snare(w, h, top, bot):
    px = canvas(w, h)
    mid, amp = (top + bot) / 2, (bot - top) / 2
    rnd = random.Random(909)
    pts = []
    for x in range(2, w - 2):
        t = (x - 2) / (w - 5)
        if t < 0.3:
            y = mid - amp * math.sin(t / 0.3 * 2 * math.pi)
        else:
            y = mid + amp * math.exp(-2.5 * (t - 0.3)) * rnd.uniform(-1, 1)
        pts.append((x, round(y)))
    curve(px, pts)
    return px


def hihat(w, h, top, bot):
    px = canvas(w, h)
    mid = (top + bot) // 2
    rnd = random.Random(808)
    x = 3
    while x < w - 3:
        t = x / w
        a = round((bot - top) / 2 * math.exp(-1.2 * t) * rnd.uniform(0.6, 1.0))
        line(px, x, mid - a, x, mid + a)
        x += 4
    line(px, 2, mid, w - 3, mid)
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
    icons[f'{name}_icon_A_48x33.bin'] = (48, 33, fn(48, 33, 3, 27))   # as the VA's
    icons[f'{name}_icon_B_34x34.bin'] = (34, 34, fn(34, 34, 3, 27))
os.makedirs(d, exist_ok=True)
for name, (w, h, px) in icons.items():
    data = pack(px, w, h)
    assert len(data) == w * 8
    open(os.path.join(d, name), 'wb').write(data)
    print(f"wrote src/va_icons/{name} ({len(data)} B)")
    if '--show' in sys.argv:
        for row in px:
            print(''.join('#' if v else '.' for v in row))
