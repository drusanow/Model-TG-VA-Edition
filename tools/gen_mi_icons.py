#!/usr/bin/env python3
"""Writes the machine-page icons of PLAITS into src/va_icons/, in the layout
and style of gen_drum_icons.py:
  B (34x34) - a bold wavefolded sine, Plaits' waveshaping engine's shape
  A (48x33) - the stock-style card: CLASS SYNTH, STYLE MACRO, STR/DEX/MAG
Rerun after changing them; CI checks that the files are up to date. --show
prints them as text.
"""
import math, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import canvas, fill, card, pack, d   # noqa: E402


def wave():
    """a sine folded back on itself, drawn as a thick solid stroke"""
    px = canvas(34, 34)
    pts = []
    for i in range(0, 301):
        t = i / 300
        x = 2 + 30 * t
        s = 1.9 * math.sin(2 * math.pi * t)
        while abs(s) > 1:                             # fold at +-1
            s = math.copysign(2, s) - s
        pts.append((x, 17 - 13 * s))
    fill(px, lambda x, y: min((x - a) ** 2 + (y - b) ** 2 for a, b in pts) <= 2.6 ** 2)
    return px


def main():
    icons = {'plaits_icon_A_48x33.bin': (48, 33, card('SYNTH', 'MACRO', (4, 4, 5))),
             'plaits_icon_B_34x34.bin': (34, 34, wave())}
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
