#!/usr/bin/env python3
"""Writes src/dx_algo_gfx.inc: DX's 32 algorithm diagrams for the full-screen
Algorithm popup (SETTINGS + Shape on a DX track, dx_algpop in dx.inc), laid out
as the DX7's own chart draws them: the carriers on the bottom row over the
output bus, each modulator above what it modulates, the feedback loop.

Each diagram is a list of rectangles on the 128 x 64 screen, rows counted from
the TOP (dx_algpop mirrors them), drawn in order with 0x40070dea: set, or
cleared when x0 has bit 7 (a box is filled, then its inside cleared, then its
number drawn in a 3 x 5 font). Rows 0..52 hold the diagram; the number goes
under it, in the stock font (dx_algpop).

The connections are checked against ALGOS (gen_dx_tables.py), which
tests/test_dx.py checks against the engine. The loops of algorithms 4 and 6 are
drawn as the DX7's chart draws them (OP4 / OP5 back into OP6); msfa, and so
this machine, feeds OP6 back on itself there (gen_dx_tables.py).

    python3 tools/gen_dx_algo_gfx.py [--png out.png] [--show N]
"""
import os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from gen_dx_tables import ALGOS   # noqa: E402

OUT = os.path.join(HERE, '..', 'src', 'dx_algo_gfx.inc')

# (column, row) of OP1..OP6; row 0 = the carriers; half columns centre a box
# over the two below it, as the chart does
POS = {
    1: [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (1, 3)],
    2: [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (1, 3)],
    3: [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2)],
    4: [(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2)],
    5: [(0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1)],
    6: [(0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1)],
    7: [(0, 0), (0, 1), (1, 0), (1, 1), (2, 1), (2, 2)],
    8: [(0, 0), (0, 1), (1, 0), (1, 1), (2, 1), (2, 2)],
    9: [(0, 0), (0, 1), (1, 0), (1, 1), (2, 1), (2, 2)],
    10: [(2, 0), (2, 1), (2, 2), (0.5, 0), (0, 1), (1, 1)],
    11: [(2, 0), (2, 1), (2, 2), (0.5, 0), (0, 1), (1, 1)],
    12: [(3, 0), (3, 1), (1, 0), (0, 1), (1, 1), (2, 1)],
    13: [(3, 0), (3, 1), (1, 0), (0, 1), (1, 1), (2, 1)],
    14: [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (2, 2)],
    15: [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (2, 2)],
    16: [(1, 0), (0, 1), (1, 1), (1, 2), (2, 1), (2, 2)],
    17: [(1, 0), (0, 1), (1, 1), (1, 2), (2, 1), (2, 2)],
    18: [(1, 0), (0, 1), (1, 1), (2, 1), (2, 2), (2, 3)],
    19: [(0, 0), (0, 1), (0, 2), (1, 0), (2, 0), (1.5, 1)],
    20: [(0, 0), (1, 0), (0.5, 1), (2.5, 0), (2, 1), (3, 1)],
    21: [(0, 0), (1, 0), (0.5, 1), (2, 0), (3, 0), (2.5, 1)],
    22: [(0, 0), (0, 1), (1, 0), (2, 0), (3, 0), (2, 1)],
    23: [(0, 0), (1, 0), (1, 1), (2, 0), (3, 0), (2.5, 1)],
    24: [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (3, 1)],
    25: [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (3.5, 1)],
    26: [(0, 0), (1, 0), (1, 1), (2.5, 0), (2, 1), (3, 1)],
    27: [(0, 0), (1, 0), (1, 1), (2.5, 0), (2, 1), (3, 1)],
    28: [(0, 0), (0, 1), (1, 0), (1, 1), (1, 2), (2, 0)],
    29: [(0, 0), (1, 0), (2, 0), (2, 1), (3, 0), (3, 1)],
    30: [(0, 0), (1, 0), (2, 0), (2, 1), (2, 2), (3, 0)],
    31: [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (4, 1)],
    32: [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0)],
}
# the chart's loops that are not an operator on itself: (from the top of, back from the output of)
LOOP = {4: (6, 4), 6: (6, 5)}

DIGITS = {1: ['.#.', '##.', '.#.', '.#.', '###'],
          2: ['##.', '..#', '.#.', '#..', '###'],
          3: ['##.', '..#', '.#.', '..#', '##.'],
          4: ['#.#', '#.#', '###', '..#', '..#'],
          5: ['###', '#..', '##.', '..#', '##.'],
          6: ['.##', '#..', '###', '#.#', '###']}

BW, BH = 13, 9            # a box, outline included
BUS = 51                  # the output bus's row
TOP = 1                   # the highest row a loop may use
LEFT, RIGHT = 2, 125


def layout(n):
    pos = POS[n]
    cols = max(c for c, _ in pos)
    rows = max(r for _, r in pos)
    lp = 4 if n not in LOOP and pos[ALGOS[n][2] - 1][0] == cols else 0
    lp = max(lp, 4 if n in LOOP and pos[LOOP[n][0] - 1][0] == cols else 0)
    px = min(26, (RIGHT - LEFT - BW - lp) / cols) if cols else 0
    py = min(16, (BUS - 3 - TOP - 3 - BH) / rows) if rows else 0
    width = cols * px + BW + lp
    x0 = LEFT + (RIGHT - LEFT - width) / 2
    ybot = BUS - 3 - BH                                    # the carriers' top row
    box = {}
    for op, (c, r) in enumerate(pos, 1):
        box[op] = (int(round(x0 + c * px)), int(round(ybot - r * py)))
    return box


def diagram(n):
    mods, carriers, fb = ALGOS[n]
    box = layout(n)
    pos = POS[n]
    # the layout agrees with the topology
    for s, d in mods:
        assert pos[s - 1][1] == pos[d - 1][1] + 1, (n, s, d)
    for op in range(1, 7):
        assert (pos[op - 1][1] == 0) == (op in carriers), (n, op)
    rects = []

    def cx(op):
        return box[op][0] + BW // 2

    # the connections: down from the modulator, across, down into the modulated
    for s, d in mods:
        sx, sy = cx(s), box[s][1] + BH
        dx, dy = cx(d), box[d][1] - 1
        j = dy - (dy - sy) // 2
        rects += [(sx, sy, sx, j), (min(sx, dx), j, max(sx, dx), j), (dx, j, dx, dy)]
    # the carriers onto the bus
    xs = [cx(c) for c in sorted(carriers)]
    for c in carriers:
        rects.append((cx(c), box[c][1] + BH, cx(c), BUS))
    lo, hi = min(xs), max(xs)
    if lo == hi:
        lo, hi = lo - 4, hi + 4
    rects.append((lo, BUS, hi, BUS))
    # the feedback loop
    a, b = LOOP.get(n, (fb, fb))
    xa = cx(a)
    ya = box[a][1] - 3
    xr = box[a][0] + BW + 2
    yb = box[b][1] + BH + 1
    rects += [(xa, ya, xa, box[a][1] - 1), (xa, ya, xr, ya), (xr, ya, xr, yb), (cx(b), yb, xr, yb)]
    # the boxes and their numbers
    for op in range(1, 7):
        x, y = box[op]
        rects.append((x, y, x + BW - 1, y + BH - 1))
        rects.append((0x80 | (x + 1), y + 1, x + BW - 2, y + BH - 2))
        gx, gy = x + (BW - 3) // 2, y + (BH - 5) // 2
        for r, row in enumerate(DIGITS[op]):
            c = 0
            while c < 3:
                if row[c] == '#':
                    e = c
                    while e + 1 < 3 and row[e + 1] == '#':
                        e += 1
                    rects.append((gx + c, gy + r, gx + e, gy + r))
                    c = e + 1
                else:
                    c += 1
    for x0, y0, x1, y1 in rects:
        assert 0 <= x0 & 0x7f <= x1 <= 127 and 0 <= y0 <= y1 <= 52, (n, x0, y0, x1, y1)
    return rects


def render(n):
    px = [[0] * 128 for _ in range(64)]
    for x0, y0, x1, y1 in diagram(n):
        v = 0 if x0 & 0x80 else 1
        for y in range(y0, y1 + 1):
            for x in range(x0 & 0x7f, x1 + 1):
                px[y][x] = v
    return px


def png(path, imgs, cols=4, scale=3):
    rows = (len(imgs) + cols - 1) // cols
    W, H = cols * 132, rows * 68
    canvas = [[40] * W for _ in range(H)]
    for i, im in enumerate(imgs):
        ox, oy = (i % cols) * 132 + 2, (i // cols) * 68 + 2
        for y in range(64):
            for x in range(128):
                canvas[oy + y][ox + x] = 255 if im[y][x] else 0
    raw = b''.join(b'\0' + bytes(v for v in row for _ in range(scale)) * 1
                   for row in canvas for _ in range(scale))
    w = W * scale

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, H * scale, 8, 0, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    out = ['| Written by tools/gen_dx_algo_gfx.py - do not edit.',
           '| DX\'s algorithm diagrams (dx_algpop): dx_agfx_ix, the offset of each in',
           '| dx_agfx; there, a count, then that many rectangles (x0, y0, x1, y1), rows',
           '| from the top, x0 bit 7 = cleared.',
           '    .align 2', 'dx_agfx_ix:']
    body, offs, total = [], [], 0
    for n in range(1, 33):
        r = diagram(n)
        assert len(r) < 256
        offs.append(total)
        body.append(f'    .byte {len(r)}   | {n}')
        for i in range(0, len(r), 4):
            body.append('    .byte ' + ', '.join('%d, %d, %d, %d' % t for t in r[i:i + 4]))
        total += 1 + 4 * len(r)
    for i in range(0, 32, 8):
        out.append('    .word ' + ', '.join(str(o) for o in offs[i:i + 8]))
    out.append('dx_agfx:')
    out += body
    out.append('    .align 2')
    open(OUT, 'w').write('\n'.join(out) + '\n')
    print(f'wrote src/dx_algo_gfx.inc ({total} bytes of diagrams)')
    if '--png' in sys.argv:
        p = sys.argv[sys.argv.index('--png') + 1]
        png(p, [render(n) for n in range(1, 33)])
        print('wrote', p)
    if '--show' in sys.argv:
        n = int(sys.argv[sys.argv.index('--show') + 1])
        for row in render(n)[:53]:
            print(''.join('#' if v else '.' for v in row))


if __name__ == '__main__':
    main()
