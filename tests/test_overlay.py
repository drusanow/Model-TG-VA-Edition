#!/usr/bin/env python3
"""Host tests for the overlay (build.py, phase1.s): code linked to run from the
top of the sample region, carried by the image after the resident blob, and
copied up by boot_extra_hook before the boot clear. HOST verification only:
whether the Cycles' updater and bootstrap take the larger image needs one.

    python3 -m unittest discover -s tests -v
"""
import os, sys, struct, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa


def boot_copy(e, want=None):
    """run boot_extra_hook up to its clear loop: the copy and the check only"""
    s = e.sym
    size = s['ovl_end'] - s['ovl_start']
    e.uc.mem_write(s['ovl_start'], bytes(size))           # nothing there before boot
    if want is not None:
        e.w32(s['ovl_want'], want)
    h = s['boot_extra_hook']                              # movec acr1: not in Unicorn
    code = bytes(e.uc.mem_read(h, 32))
    k = code.index(bytes.fromhex('4e7b4005'))
    e.uc.mem_write(h + k, bytes.fromhex('4e714e71'))
    e.run('boot_extra_hook', s['clear_loop'])
    return size


class TOverlay(unittest.TestCase):
    def test_layout(self):
        e = Emu(); s = e.sym
        self.assertEqual(s['ovl_lma'] % 16, 0)
        self.assertEqual(s['ovl_lma'], s['reserved_end'])      # straight after the blob
        self.assertGreaterEqual(s['ovl_start'], 0x4c000000)    # the cached block (ACR1)
        self.assertLessEqual(s['ovl_end'] - s['ovl_start'], s['ovl_max'])
        self.assertLessEqual(s['ovl_end'], s['lfo34_kits'])    # below LFO 3/4's blocks
        self.assertLessEqual(s['ovl_lma'] + s['ovl_end'] - s['ovl_start'], 0x40200000)
        self.assertTrue(s['ovl_start'] <= s['mi_fill'] < s['ovl_end'])   # PLAITS runs there

    def test_boot_copies_and_checks(self):
        e = Emu(); s = e.sym
        lo = s['ovl_lma'] - BLOB
        img = e.blob[lo:lo + s['ovl_end'] - s['ovl_start']]
        good = sum(struct.unpack(f'>{len(img) // 4}I', img)) & M32
        size = boot_copy(e, good)
        self.assertEqual(bytes(e.uc.mem_read(s['ovl_start'], size)), img)
        self.assertEqual(e.r32(s['ovl_ok']), 1)
        self.assertEqual(e.r32(s['sfx_desc']), s['sfx_title'])
        e = Emu()
        boot_copy(e, good ^ 1)                                  # a bad image: said so
        self.assertEqual(e.r32(s['ovl_ok']), 0)
        title = e.r32(s['sfx_desc'])
        self.assertEqual(bytes(e.uc.mem_read(title, 10)), b'OVL ERROR\0')


if __name__ == '__main__':
    unittest.main(verbosity=2)
