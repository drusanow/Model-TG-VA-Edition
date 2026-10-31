#!/usr/bin/env python3
"""Writes the machine-page icons of LXR (machine 13) into src/va_icons/, in the layout and style of
gen_drum_icons.py:
  B (34x34) - a drum seen from the side with a hi-hat above it: the voices it plays
  A (48x33) - the stock-style card: CLASS DRUM, STYLE LXR, STR/DEX/MAG
  lx_<engine>_B_34x34 - one picture per engine, drawn on the Contour screen and on the machine
      page for the selected track's engine (lx_draw, lx_icon), as PLAITS' are:
      KICK  the bass drum's head with its beater, on its feet (gen_drum_icons' kick)
      TOM   a shallow drum seen from the side, tilted
      PERC  a cowbell
      SNAR  a snare with its stick (gen_drum_icons' snare)
      CLAP  a burst: a hit with long and short rays around it
      CHAT  two cymbals pressed together on their rod
      OHAT  two cymbals apart on their rod (gen_drum_icons' hihat)
      CYMB  one wide cymbal on a stand
Rerun after changing them; CI checks that the files are up to date. --show prints them as text.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import (canvas, fill, clear, in_rect, in_ellipse, in_bar, card, pack, d,   # noqa: E402
                            kick, snare, hihat)

W = H = 34


def lens(px, cx, cy, half, th):
    """a cymbal seen edge-on: a lens half wide, th thick in the middle"""
    fill(px, lambda x, y: abs(x - cx) <= half and abs(y - cy) <= th * (1 - ((x - cx) / (half + 0.5)) ** 2) + 0.6)


def tom():
    px = canvas(W, H)
    fill(px, in_rect(5, 13, 29, 26))                   # the shell
    fill(px, in_ellipse(17, 13, 12, 4.5))              # the head
    fill(px, in_ellipse(17, 26, 12, 4))
    clear(px, in_ellipse(17, 12.5, 9.5, 2))
    for x in (10, 16, 22):
        clear(px, in_rect(x, 17, x + 2, 26))
    fill(px, in_bar(17, 30, 17, 34, 1.5))              # its mount
    return px


def perc():
    px = canvas(W, H)
    fill(px, lambda x, y: 4 <= y <= 30 and abs(x - 17) <= 5 + (y - 4) * 0.42)   # the bell
    clear(px, lambda x, y: 8 <= y <= 27 and abs(x - 17) <= 2 + (y - 8) * 0.42)
    fill(px, in_rect(14, 0, 20, 5))                    # the handle
    fill(px, in_bar(24, 12, 31, 3, 1.6))               # the stick
    return px


def clap():
    px = canvas(W, H)
    import math
    fill(px, in_ellipse(17, 17, 3.5, 3.5))             # the hit
    for k in range(8):                                 # and its burst
        a = 2 * math.pi * k / 8 + math.pi / 8
        r0, r1 = (6, 16) if k % 2 == 0 else (7, 12)
        fill(px, in_bar(17 + r0 * math.cos(a), 17 + r0 * math.sin(a),
                        17 + r1 * math.cos(a), 17 + r1 * math.sin(a), 1.6))
    return px


def chat():
    px = canvas(W, H)
    fill(px, in_rect(15, 1, 19, 34))
    lens(px, 17, 13, 16, 3)
    lens(px, 17, 18, 16, 3)
    fill(px, in_rect(13, 4, 21, 7))
    return px


def cymb():
    px = canvas(W, H)
    lens(px, 17, 8, 16.5, 3.5)
    fill(px, in_ellipse(17, 6, 3, 2))                  # the bell
    fill(px, in_rect(15, 9, 19, 30))                   # the stand
    fill(px, in_bar(17, 29, 5, 34, 1.6))
    fill(px, in_bar(17, 29, 29, 34, 1.6))
    return px


ENGINES = (('kick', kick), ('tom', tom), ('perc', perc), ('snar', snare),
           ('clap', clap), ('chat', chat), ('ohat', hihat), ('cymb', cymb))


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
    for n, f in ENGINES:
        icons[f'lx_{n}_B_34x34.bin'] = (34, 34, f())
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
