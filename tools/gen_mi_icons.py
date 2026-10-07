#!/usr/bin/env python3
"""Writes the machine-page icons of PLAITS into src/va_icons/, in the layout
and style of gen_drum_icons.py:
  B (34x34) - a bold wavefolded sine: PLAITS' own picture
  A (48x33) - the stock-style card: CLASS SYNTH, STYLE MACRO, STR/DEX/MAG
  mi_<engine>_B_34x34 - one picture per engine; the machine page shows the
      selected track's (mi_watch / mi_icon):
      WSHP  a triangle folding inside a frame
      FM    two operators: two rings joined by a diagonal line
      GRAN  a cloud of grain squares rising diagonally
      PD    a trapezoid wave: slanted edges, flat tops
      CHIP  a square wave
      NOIS  speckle thinning out to the right (filtered noise)
      PART  scattered dust
      STRG  three strings from their pegs, the middle one plucked
Rerun after changing them; CI checks that the files are up to date. --show
prints them as text.
"""
import math, os, random, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import canvas, fill, clear, in_ellipse, in_rect, in_bar, card, pack, d   # noqa: E402

W = H = 34


def stroke(px, pts, r):
    """a thick polyline: every pixel within r of one of its segments"""
    segs = list(zip(pts, pts[1:]))

    def inside(x, y):
        for (x0, y0), (x1, y1) in segs:
            dx, dy = x1 - x0, y1 - y0
            n = dx * dx + dy * dy or 1e-9
            t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / n))
            if (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= r * r:
                return True
        return False
    fill(px, inside)


def curve(f, x0=2, x1=32, n=240):
    return [(x0 + (x1 - x0) * i / n, f(i / n)) for i in range(n + 1)]


def fold(s):
    while abs(s) > 1:
        s = math.copysign(2, s) - s
    return s


def wave():
    """PLAITS: a sine folded back on itself, drawn as a thick solid stroke"""
    px = canvas(W, H)
    pts = [(2 + 30 * i / 300, 17 - 13 * fold(1.9 * math.sin(2 * math.pi * i / 300))) for i in range(301)]
    fill(px, lambda x, y: min((x - a) ** 2 + (y - b) ** 2 for a, b in pts) <= 2.6 ** 2)
    return px


def wshp():
    px = canvas(W, H)
    fill(px, in_rect(1, 1, 33, 33))
    clear(px, in_rect(4, 4, 30, 30))
    stroke(px, curve(lambda t: 17 - 10 * fold(2.2 * (2 * t - 1)), 6, 28), 2.0)
    return px


def fm():
    px = canvas(W, H)
    for cx, cy in ((9, 9), (25, 25)):
        fill(px, in_ellipse(cx, cy, 7.5, 7.5))
        clear(px, in_ellipse(cx, cy, 3.5, 3.5))
    fill(px, in_bar(14, 14, 20, 20, 1.8))
    return px


def gran():
    px = canvas(W, H)
    for x, y, s in ((2, 26, 5), (8, 21, 4), (13, 24, 3), (12, 14, 5), (19, 17, 4), (20, 8, 4),
                    (26, 11, 5), (27, 3, 3), (25, 22, 3), (5, 16, 2)):
        fill(px, in_rect(x, y, x + s, y + s))
    return px


def pd():
    px = canvas(W, H)
    stroke(px, [(1, 26), (4, 26), (9, 8), (16, 8), (19, 26), (25, 26), (30, 8), (33, 8)], 2.1)
    return px


def chip():
    px = canvas(W, H)
    stroke(px, [(1, 26), (8, 26), (8, 8), (17, 8), (17, 26), (26, 26), (26, 8), (33, 8)], 2.2)
    return px


def nois():
    """dots on a jittered grid, so they never merge, thinning out to the right"""
    px = canvas(W, H)
    rnd = random.Random(5)
    for y in range(1, 33, 3):
        for x in range(1, 33, 3):
            if rnd.random() < (1 - x / 34) ** 1.2:
                s = rnd.choice((1, 2, 2))
                jx, jy = rnd.randrange(3 - s + 1), rnd.randrange(3 - s + 1)
                fill(px, in_rect(x + jx, y + jy, x + jx + s, y + jy + s))
    return px


def part():
    px = canvas(W, H)
    for cx, cy, r in ((7, 8, 2.5), (20, 5, 1.5), (28, 12, 3), (12, 19, 3.5), (25, 25, 2),
                      (6, 29, 1.6), (17, 30, 1.2), (30, 31, 1.5)):
        fill(px, in_ellipse(cx, cy, r, r))
    return px


def strg():
    px = canvas(W, H)
    for y in (8, 17, 26):
        fill(px, in_ellipse(4, y, 3, 3))
        fill(px, in_rect(29, y - 2, 33, y + 2))
        if y == 17:
            stroke(px, curve(lambda t: 17 - 6 * math.sin(math.pi * t), 6, 29), 1.1)
        else:
            stroke(px, [(6, y), (29, y)], 0.9)
    return px


ENGINES = (('wshp', wshp), ('fm', fm), ('gran', gran), ('pd', pd),
           ('chip', chip), ('nois', nois), ('part', part), ('strg', strg))


def main():
    icons = {'plaits_icon_A_48x33.bin': (48, 33, card('SYNTH', 'MACRO', (4, 4, 5))),
             'plaits_icon_B_34x34.bin': (34, 34, wave())}
    for name, fn in ENGINES:
        icons[f'mi_{name}_B_34x34.bin'] = (34, 34, fn())
    os.makedirs(d, exist_ok=True)
    for name, (w, h, px) in icons.items():
        data = pack(px, w, h)
        assert len(data) == w * 8
        open(os.path.join(d, name), 'wb').write(data)
        print(f"wrote src/va_icons/{name} ({len(data)} B)")
        if '--show' in sys.argv:
            for row in px:
                print(''.join('#' if v else '.' for v in row))


if __name__ == '__main__':
    main()
