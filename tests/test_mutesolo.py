#!/usr/bin/env python3
"""Host tests for quantised mute and track solo (src/mutesolo.inc).

The blob runs on Unicorn with the stock OS as `rts` stubs (tests/va_emu.py);
the few stock routines the code calls are stood in for from Python: the mute
query 0x4001263c and setter 0x4001381c over one mask, the key-held query
0x4007faf4, and the object lookups. The sequencer's step counter 0x40a78840
and play flag 0x40a7883c are written directly.

    python3 -m unittest tests.test_mutesolo -v
"""
import os, sys, struct, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

QM_PLAY, QM_STEP = 0x40a7883c, 0x40a78840
OBJ = SCRATCH + 0x9000
EV = SCRATCH + 0x9100
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
        args = struct.unpack('>6I', uc.mem_read(sp + 4, 24))
        uc.reg_write(D[0], (fn(args) or 0) & M32)
        uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', uc.mem_read(sp, 4))[0])
        uc.reg_write(UC_M68K_REG_A7, sp + 4)


class TQuant(unittest.TestCase):
    def setUp(self):
        self.e = e = emu()
        self.s = e.sym
        self.mask = 0
        self.sets = []
        self.held = set()
        self.popups = []
        st = self.st = Stubs(e)
        st.set(0x400cf866, lambda a: OBJ)                   # the manager
        st.set(0x4000eb90, lambda a: a[0] + 48)              # manager + 48
        st.set(0x4001263c, lambda a: (self.mask >> a[1]) & 1)
        st.set(0x4001381c, self._setter)
        st.set(0x4007faf4, lambda a: int(a[0] in self.held))
        st.set(e.sym['show_popup'], self._popup)
        self.play(True)

    def _setter(self, a):
        obj, t, on, cc = a[:4]
        self.sets.append((obj, t, on & 0xff, cc & 0xff))
        self.mask = (self.mask | (1 << t)) if on & 0xff else (self.mask & ~(1 << t))

    def _popup(self, a):
        b = bytes(self.e.uc.mem_read(a[0], 16))
        self.popups.append(b[:b.index(0)].decode())

    def play(self, on, step=5):
        self.e.w32(QM_PLAY, 1 if on else 0)
        self.e.w32(QM_STEP, step)

    def mode(self, m):
        self.e.w32(self.s['qm_mode'], m)

    def call(self, name, d0=None, stack=()):
        self.e.run(name, regs={D[0]: d0} if d0 is not None else None, stack=stack)

    def dirty(self):
        return self.e.r32(self.s['qm_dirty'])

    def test_default_is_one_bar(self):
        self.assertEqual(self.e.r32(self.s['qm_mode']), 1)

    def test_stopped_or_off_is_at_once(self):
        self.play(False)
        self.call('qm_flip', 2)
        self.assertEqual((self.sets, self.mask, self.dirty()), ([(OBJ + 48, 2, 1, 1)], 4, 0))
        self.play(True)
        self.mode(0)
        self.call('qm_flip', 2)
        self.assertEqual((self.mask, self.dirty()), (0, 0))

    def test_one_bar(self):
        self.mode(1)
        self.call('qm_flip', 3)
        self.assertEqual((self.sets, self.dirty()), ([], 8))
        for step in (1, 5, 15, 17, 31, 63):
            self.play(True, step)
            self.call('qm_poll')
            self.assertEqual(self.sets, [], step)
        self.play(True, 32)                                 # a bar's first step is next
        self.call('qm_poll')
        self.assertEqual((self.sets, self.mask, self.dirty()), ([(OBJ + 48, 3, 1, 1)], 8, 0))
        self.call('qm_flip', 3)
        self.play(True, 0)                                  # the pattern's top is a bar too
        self.call('qm_poll')
        self.assertEqual(self.mask, 0)

    def test_pattern(self):
        self.mode(2)
        self.call('qm_flip', 0)
        for step in (16, 32, 48, 7):
            self.play(True, step)
            self.call('qm_poll')
            self.assertEqual(self.sets, [], step)
        self.play(True, 0)
        self.call('qm_poll')
        self.assertEqual((self.mask, self.dirty()), (1, 0))

    def test_twice_cancels_and_others_wait(self):
        self.call('qm_flip', 1)
        self.call('qm_flip', 4)
        self.call('qm_flip', 1)
        self.assertEqual(self.dirty(), 1 << 4)
        self.play(True, 16)
        self.call('qm_poll')
        self.assertEqual((self.sets, self.mask), ([(OBJ + 48, 4, 1, 1)], 1 << 4))

    def test_mute_mode_queue_goes_through(self):
        self.e.w32(self.s['mq_pending'], 0b100101)
        self.call('mq_apply')
        self.assertEqual((self.sets, self.dirty(), self.e.r32(self.s['mq_pending'])), ([], 0b100101, 0))
        self.play(True, 48)
        self.call('qm_poll')
        self.assertEqual(self.mask, 0b100101)

    def test_mute_pads_show_what_is_coming(self):
        self.mask = 0b10
        self.call('qm_flip', 0)                             # track 0 to be muted
        got = []
        for t in range(6):
            self.e.run('mq_muted', stack=[OBJ + 48, t])
            got.append(self.e.d(0) & 1)
        self.assertEqual(got, [1, 1, 0, 0, 0, 0])
        self.e.w32(self.s['mq_pending'], 0b1)               # and a press in mute mode flips it
        self.e.run('mq_muted', stack=[OBJ + 48, 0])
        self.assertEqual(self.e.d(0) & 1, 0)

    def test_solo_and_back(self):
        self.mask = 0b000100                                # track 2 muted before
        self.call('qm_solo', 3)
        self.assertEqual((self.sets, self.popups), ([], ['SOLO 4']))
        self.play(True, 16)
        self.call('qm_poll')
        self.assertEqual(self.mask, 0b110111)
        self.play(True, 20)
        self.call('qm_solo', 3)                             # again: the mutes from before
        self.assertEqual(self.popups[-1], 'SOLO OFF')
        self.play(True, 32)
        self.call('qm_poll')
        self.assertEqual(self.mask, 0b000100)

    def test_solo_another_track_and_pending(self):
        self.call('qm_solo', 0)
        self.call('qm_solo', 5)                             # before the bar: switch
        self.play(True, 0)
        self.call('qm_poll')
        self.assertEqual(self.mask, 0b011111)
        self.call('qm_solo', 5)
        self.call('qm_poll')
        self.assertEqual(self.mask, 0)                      # saved: before the first solo

    def test_pad_chord(self):
        e = self.e
        e.w32(self.s['mod_used'], 0)
        e.w32(EV + 16, 1)
        e.w32(EV + 20, 2)                                   # pad 2 = track 1
        st = e.run_to_any('qm_pad', (0x4001d3d4, RET), stack=[0, EV])
        self.assertEqual(st, 0x4001d3d4)                    # no SETTINGS: stock
        self.held = {13}
        st = e.run_to_any('qm_pad', (0x4001d3d4, RET), stack=[0, EV])
        self.assertEqual((st, e.d(0), self.popups, e.r32(self.s['mod_used'])), (RET, 1, ['SOLO 2'], 1))
        self.assertEqual(self.dirty(), 0b111101)
        e.w32(EV + 16, 0)                                   # its release: swallowed
        st = e.run_to_any('qm_pad', (0x4001d3d4, RET), stack=[0, EV])
        self.assertEqual((st, e.d(0), len(self.popups)), (RET, 1, 1))
        e.w32(EV + 16, 1)
        for pad in (0, 7, 9):                               # not a track pad
            e.w32(EV + 20, pad)
            self.assertEqual(e.run_to_any('qm_pad', (0x4001d3d4, RET), stack=[0, EV]), RET)
        self.assertEqual(len(self.popups), 1)

    def test_config_row_turns(self):
        e = self.e
        clo, data = SCRATCH + 0x9200, SCRATCH + 0x9210
        e.w32(clo, data)
        e.w32(data, 0)                                      # no menu: no redraw
        seen = []
        for delta, want in ((1, 2), (1, 2), (-1, 1), (-1, 0), (-1, 0), (0, 0), (5, 1)):
            e.run('qm_rot', stack=[clo, 0, delta & M32])
            seen.append(e.r32(self.s['qm_mode']))
            self.assertEqual(seen[-1], want, (delta, seen))


if __name__ == '__main__':
    unittest.main()
