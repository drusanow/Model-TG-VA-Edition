#!/usr/bin/env python3
"""Writes the machine-page icons of ACHORD (machine 14) into src/va_icons/, in the style of gen_lxr_icons.py:
  B (34x34) - a piece of keyboard, three of its keys pressed (a dot on each): a chord
  A (48x33) - the stock-style card: CLASS CHORD, STYLE ANALOG, STR/DEX/MAG
Rerun after changing them; --show prints them as text.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import canvas, fill, clear, in_rect, in_ellipse, card, pack, d   # noqa: E402

W = H = 34


def keys():
    px = canvas(W, H)
    fill(px, in_rect(1, 4, 32, 30))                    # the keys' frame
    clear(px, in_rect(2, 5, 31, 29))
    for x in (6, 11, 16, 21, 26):                      # five white keys' edges
        fill(px, in_rect(x, 5, x + 1, 30))
    for x in (6, 11, 21, 26):                          # and the black keys between them (none at E-F)
        fill(px, in_rect(x - 1, 5, x + 2, 19))
    for cx in (3.5, 13.5, 23.5):                       # a triad: three keys pressed
        fill(px, in_ellipse(cx + 0.5, 25, 1.8, 1.8))
    fill(px, in_rect(1, 0, 32, 1))                     # (a top bar)
    return px


def main():
    icons = {'ac_icon_A_48x33.bin': (48, 33, card('CHORD', 'ANALOG', (3, 4, 4))),
             'ac_icon_B_34x34.bin': (34, 34, keys())}
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
