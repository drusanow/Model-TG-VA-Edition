#!/usr/bin/env python3
"""Writes the machine-page icons of LXR (machine 13) into src/va_icons/, in the layout and style of
gen_drum_icons.py:
  B (34x34) - a drum seen from the side with a hi-hat above it: the voices it plays
  A (48x33) - the stock-style card: CLASS DRUM, STYLE LXR, STR/DEX/MAG
Rerun after changing them; CI checks that the files are up to date. --show prints them as text.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import canvas, fill, clear, in_rect, in_ellipse, card, pack, d   # noqa: E402


def drums():
    px = canvas(34, 34)
    fill(px, in_rect(4, 18, 29, 31))                   # the shell
    clear(px, in_rect(6, 20, 27, 29))
    fill(px, in_ellipse(16.5, 18, 13, 3))              # its head
    clear(px, in_ellipse(16.5, 18, 11, 1.5))
    for x in (9, 16, 23):                              # lugs
        fill(px, in_rect(x, 22, x + 1, 27))
    fill(px, in_ellipse(16.5, 6, 12, 2))               # a cymbal above it, on its stand
    clear(px, in_ellipse(16.5, 6, 10, 0.8))
    fill(px, in_rect(16, 7, 17, 15))
    return px


def main():
    icons = {'lxr_icon_A_48x33.bin': (48, 33, card('DRUM', 'LXR', (4, 3, 4))),
             'lxr_icon_B_34x34.bin': (34, 34, drums())}
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
