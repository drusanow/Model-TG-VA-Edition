#!/usr/bin/env python3
"""Writes the machine-page icons of DX into src/va_icons/, in the layout and
style of gen_drum_icons.py:
  B (34x34) - six operators as an algorithm draws them: three stacks of two
              boxes, each modulator above its carrier, the carriers on a bus
  A (48x33) - the stock-style card: CLASS SYNTH, STYLE FM, STR/DEX/MAG
Rerun after changing them; CI checks that the files are up to date. --show
prints them as text.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_drum_icons import canvas, fill, clear, in_rect, in_bar, card, pack, d   # noqa: E402


def ops():
    px = canvas(34, 34)
    for x in (2, 13, 24):
        for y in (2, 15):
            fill(px, in_rect(x, y, x + 8, y + 8))
            clear(px, in_rect(x + 2, y + 2, x + 6, y + 6))
        fill(px, in_rect(x + 3, 10, x + 5, 15))     # modulator -> carrier
        fill(px, in_rect(x + 3, 23, x + 5, 28))     # carrier -> the bus
    fill(px, in_rect(5, 28, 30, 30))                 # the output bus
    fill(px, in_rect(16, 30, 18, 33))
    return px


def main():
    icons = {'dx_icon_A_48x33.bin': (48, 33, card('SYNTH', 'FM', (5, 3, 4))),
             'dx_icon_B_34x34.bin': (34, 34, ops())}
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
