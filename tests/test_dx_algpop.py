#!/usr/bin/env python3
"""Host tests for DX's Algorithm popup (dx_algpop in src/dx.inc, reached from
wf_popup in src/model_tg.s): SETTINGS + Shape on a DX track shows the
algorithm's diagram full screen, and its number under it, instead of the stock
dial.

The blob runs on Unicorn with the stock OS as `rts` stubs (tests/va_emu.py);
the drawing calls are stood in for from Python: 0x40070dea (a rectangle:
set, cleared or inverted) on a 128 x 64 screen counted from the bottom, as
the Cycles' is, and 0x40071a04 (text: recorded). The view's render inverts
the left 64 x 64 box after the popup, so the test does too.

    python3 -m unittest tests.test_dx_algpop -v
"""
import os, sys, struct, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from va_emu import *  # noqa
import gen_dx_algo_gfx as G

DX = 12
OBJ = SCRATCH + 0x9000
SND = SCRATCH + 0x9200
STOCK_POPUP = 0x4001d818
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


class Stubs:
    """Python stand-ins for stock routines: fn(args) -> d0, then return."""
    def __init__(self, e):
        self.e, self.fns = e, {}
        e.uc.hook_add(UC_HOOK_CODE, self._hook)

    def set(self, addr, fn):
        self.fns[addr] = fn

    def _hook(self, uc, addr, size, ud):
        fn = self.fns.get(addr)
        if fn is None:
            return
        sp = uc.reg_read(UC_M68K_REG_A7)
        args = struct.unpack('>8I', uc.mem_read(sp + 4, 32))
        uc.reg_write(D[0], (fn(args) or 0) & M32)
        uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', uc.mem_read(sp, 4))[0])
        uc.reg_write(UC_M68K_REG_A7, sp + 4)


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


class TAlgPopup(unittest.TestCase):
    def setUp(self):
        self.e = e = emu()
        self.track, self.mach = 2, DX
        self.texts = []
        self.scr = [[1] * 128 for _ in range(64)]            # rows from the bottom; junk to clear
        st = Stubs(e)
        st.set(0x400cf866, lambda a: OBJ)
        st.set(0x4000eb90, lambda a: a[0] + 48)
        st.set(0x40012412, lambda a: self.track)
        st.set(e.sym['sound_obj'], lambda a: SND)
        st.set(0x40070dea, self._rect)
        st.set(0x40071a04, self._text)
        e.w8(SND + 38, DX)

    def _rect(self, a):
        _, x0, y0, x1, y1, mode = a[:6]
        mode = s32(mode)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.scr[y][x] = 1 if mode == 1 else 0 if mode == 0 else self.scr[y][x] ^ 1

    def _text(self, a):
        ctx, font, x, y, flags, fmt, arg = a[:7]
        b = bytes(self.e.uc.mem_read(fmt, 32))
        self.texts.append((x, y, flags, b[:b.index(0)].decode() % arg))

    def popup(self, value, pid=0x0a, locked=0):
        self.e.w8(SND + 38, self.mach)
        stop = self.e.run_to_any('wf_popup', [RET, STOCK_POPUP],
                               stack=[0x1234, 0x5678, pid, value, locked, 1])
        return stop

    def shown(self):
        """the screen as the render leaves it: the left box inverted once more, rows from the top"""
        self._rect((0, 0, 0, 63, 63, M32))
        return [self.scr[63 - r] for r in range(64)]

    def test_every_algorithm(self):
        for n in range(1, 33):
            self.setUp()
            v = ((n * 128 + 32) // 33) << 8                   # dx_algfmt reads this as An
            self.popup(v)
            got = self.shown()
            want = G.render(n)
            self.assertEqual(got[:53], want[:53], n)
            self.assertTrue(all(not any(r) for r in got[53:]), n)
            self.assertEqual(self.texts, [(64, 1, 0x12, f'{n}')], n)

    def test_vce_shows_the_voice_own(self):
        self.e.w8(self.e.sym['dx_tst'] + 512 * self.track + 64 + 110, 21)   # algorithm 22
        self.popup(0)
        self.assertEqual(self.shown()[:53], G.render(22)[:53])
        self.assertEqual(self.texts, [(64, 1, 0x12, 'VCE 22')])

    def test_locked_is_boxed(self):
        self.popup(127 << 8, locked=1)
        self.assertEqual(self.texts, [(64, 1, 0x1a, '32')])

    def test_others_get_the_stock_popup(self):
        self.mach = 4                                         # TONE: its own 0x0a, if any
        self.assertEqual(self.popup(64 << 8), STOCK_POPUP)
        self.mach = DX
        self.assertEqual(self.popup(64 << 8, pid=0x0b), STOCK_POPUP)
        self.track = 6                                        # no track
        self.assertEqual(self.popup(64 << 8), STOCK_POPUP)
        self.assertEqual(self.texts, [])


if __name__ == '__main__':
    unittest.main()
