#!/usr/bin/env python3
"""Writes the machine-page icons of VA KICK, VA SNARE and VA HIHAT into
src/va_icons/, in the layout gen_va_icons.py uses (column-major, two
big-endian longs per column, bit n of the first long = row n from the top,
lit pixels set), in the style of the stock machines':
  B (34x34, the right of the page) - a bold, solid picture:
      kick  - the drum head as a thick ring, the beater in the middle, feet
      snare - a solid shell with its lugs cut out, a thick stick on top
      hihat - two solid cymbals on a thick rod
  A (48x33, the left panel under the name) - the stock "card": CLASS, STYLE
      and three ratings (STR, DEX, MAG) as five filled / empty diamonds, in
      a 3x5 capital font drawn here.
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


def fill(px, inside):
    for y in range(len(px)):
        for x in range(len(px[0])):
            if inside(x + 0.5, y + 0.5):
                px[y][x] = 1


def clear(px, inside):
    for y in range(len(px)):
        for x in range(len(px[0])):
            if inside(x + 0.5, y + 0.5):
                px[y][x] = 0


def in_ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def in_rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x < x1 and y0 <= y < y1


def in_bar(x0, y0, x1, y1, r):
    """within r of the segment (x0, y0)-(x1, y1): a thick line"""
    def f(x, y):
        dx, dy = x1 - x0, y1 - y0
        t = max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / (dx * dx + dy * dy)))
        return (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= r * r
    return f


def kick():
    """the drum head as a thick ring, the beater in the middle, two feet"""
    px = canvas(34, 34)
    fill(px, in_ellipse(17, 15.5, 14.5, 14.5))
    clear(px, in_ellipse(17, 15.5, 8, 8))
    fill(px, in_ellipse(17, 15.5, 3.5, 3.5))            # the beater
    fill(px, in_rect(3, 30, 9, 34))                     # the feet
    fill(px, in_rect(25, 30, 31, 34))
    return px


def snare():
    """a solid shell with its lugs cut out, a thick stick on the head"""
    px = canvas(34, 34)
    fill(px, in_rect(3, 17, 31, 29))                    # the shell
    fill(px, in_ellipse(17, 17, 14, 5))                 # the head
    fill(px, in_ellipse(17, 29, 14, 4))                 # the bottom rim
    for x in (9, 16, 23):                               # the lugs, cut out
        clear(px, in_rect(x, 21, x + 2, 29))
    clear(px, in_ellipse(17, 16.5, 11, 2.2))            # the head's face
    fill(px, in_bar(15, 13, 4, 2, 2))                   # the stick
    return px


def hihat():
    """two solid cymbals on a thick rod"""
    px = canvas(34, 34)
    fill(px, in_rect(15, 1, 19, 34))                    # the rod
    for cy in (10, 22):                                 # the cymbals: lenses
        fill(px, lambda x, y, cy=cy: abs(x - 17) <= 16 and abs(y - cy) <= 3 * (1 - ((x - 17) / 16.5) ** 2) + 0.6)
    fill(px, in_rect(13, 4, 21, 7))                     # the clutch on top
    return px


# ---- the A card: CLASS / STYLE / three ratings, as the stock machines' ----
FONT = {                                                # 3x5 capitals
    'A': ['.#.', '#.#', '###', '#.#', '#.#'], 'B': ['##.', '#.#', '##.', '#.#', '##.'],
    'C': ['.##', '#..', '#..', '#..', '.##'], 'D': ['##.', '#.#', '#.#', '#.#', '##.'],
    'E': ['###', '#..', '##.', '#..', '###'], 'G': ['.##', '#..', '#.#', '#.#', '.##'],
    'H': ['#.#', '#.#', '###', '#.#', '#.#'], 'I': ['###', '.#.', '.#.', '.#.', '###'],
    'K': ['#.#', '#.#', '##.', '#.#', '#.#'], 'L': ['#..', '#..', '#..', '#..', '###'],
    'M': ['#.#', '###', '###', '#.#', '#.#'], 'N': ['##.', '#.#', '#.#', '#.#', '#.#'],
    'O': ['.#.', '#.#', '#.#', '#.#', '.#.'], 'P': ['##.', '#.#', '##.', '#..', '#..'],
    'R': ['##.', '#.#', '##.', '#.#', '#.#'], 'S': ['.##', '#..', '.#.', '..#', '##.'],
    'T': ['###', '.#.', '.#.', '.#.', '.#.'], 'U': ['#.#', '#.#', '#.#', '#.#', '###'],
    'X': ['#.#', '#.#', '.#.', '#.#', '#.#'], 'Y': ['#.#', '#.#', '.#.', '.#.', '.#.'],
    ':': ['...', '.#.', '...', '.#.', '...'], ' ': ['...'] * 5,
}
DIAMOND = (['..#..', '.###.', '#####', '.###.', '..#..'],    # filled
           ['..#..', '.#.#.', '#...#', '.#.#.', '..#..'])    # empty


def text(px, x, y, s):
    for ch in s:
        for r, row in enumerate(FONT[ch]):
            for c, v in enumerate(row):
                if v == '#':
                    px[y + r][x + c] = 1
        x += 4
    return x


def card(cls, style, ratings):
    px = canvas(48, 33)
    text(px, 0, 0, 'CLASS:' + cls)
    text(px, 0, 7, 'STYLE:' + style)
    for i, (name, n) in enumerate(zip(('STR', 'DEX', 'MAG'), ratings)):
        y = 14 + 7 * i
        x = text(px, 0, y, name + ':')
        for k in range(5):
            for r, row in enumerate(DIAMOND[0 if k < n else 1]):
                for c, v in enumerate(row):
                    if v == '#':
                        px[y + r][x + 6 * k + c] = 1
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
for name, fn, cls, style, ratings in (('kick', kick, 'PERC', 'BASS', (5, 2, 2)),
                                      ('snare', snare, 'PERC', 'SNARE', (4, 4, 2)),
                                      ('hihat', hihat, 'PERC', 'METAL', (2, 5, 3))):
    icons[f'{name}_icon_A_48x33.bin'] = (48, 33, card(cls, style, ratings))
    icons[f'{name}_icon_B_34x34.bin'] = (34, 34, fn())
os.makedirs(d, exist_ok=True)
for name, (w, h, px) in icons.items():
    data = pack(px, w, h)
    assert len(data) == w * 8
    open(os.path.join(d, name), 'wb').write(data)
    print(f"wrote src/va_icons/{name} ({len(data)} B)")
    if '--show' in sys.argv:
        for row in px:
            print(''.join('#' if v else '.' for v in row))
