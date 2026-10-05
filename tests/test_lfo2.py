#!/usr/bin/env python3
"""Host tests for LFO 2 (src/lfo2.inc). HOST/STATIC verification only.

Most tests run the assembled blob on Unicorn with the stock OS as `rts` stubs
(tests/va_emu.py), with tiny hand-written stand-ins where a hook needs a
stock routine to answer. With MODEL_CYCLES_STOCK set, TRealEngine builds the
full image and runs the REAL stock LFO engine under lfo_run: LFO 2 given LFO
1's settings must modulate its destination exactly as LFO 1 modulates its
own, and LFO 1 must be unchanged by LFO 2's presence.

    python3 -m unittest discover -s tests -v
"""
import os, sys, struct, subprocess, unittest, random

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

K0 = 28
IDS = [0x1d, 0x1e, 0x21, 0x20, 0x24]          # LFO 2's words 28..32, in order
DFLT = [28672, 1024, 0, 0, 16384]
SND = SCRATCH + 0x5000                          # a sound: words at +20 + 2k
STO = SCRATCH + 0x6000                          # a storage record
STUBS = SCRATCH + 0x7000
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


def code(e, addr, hexstr):
    e.uc.mem_write(addr, bytes.fromhex(hexstr.replace(' ', '')))


# stand-ins for three stock key-event leaves (our own encodings of what they
# do: read the code at +12, the FUNC bit (1) and the "not down" bit of +16)
KEYCODE = '206f0004 2028000c 4e75'                      # movel sp@(4),a0 ; movel a0@(12),d0
FUNCBIT = '206f0004 20280010 e288 7201 c081 4e75'       # (flags >> 1) & 1
RELEASE = '206f0004 20280010 7201 c081 b380 4e75'       # (flags & 1) ^ 1


class TSlot(unittest.TestCase):
    def test_remap_only_while_lfo2_shows(self):
        e = emu()
        for on in (0, 1):
            e.w32(e.sym['lfo2_on'], on)
            for i, pid in enumerate(IDS):
                st = e.run_to_any('lfo_slot', (0x4005a4ee, RET), stack=[pid])
                if on:
                    self.assertEqual((st, e.d(0)), (RET, K0 + i), hex(pid))
                else:
                    self.assertEqual(st, 0x4005a4ee)      # the stock lookup
                    self.assertEqual((e.d(0), e.d(1)), (pid, 76))
            for pid in (0x1f, 0x22, 0x23, 0x18, 0x2e):    # Setup and others: stock
                self.assertEqual(e.run_to_any('lfo_slot', (0x4005a4ee, RET), stack=[pid]),
                                 0x4005a4ee)


class TPack(unittest.TestCase):
    def _words(self, e):
        return [e.r16(SND + 20 + 2 * (K0 + i)) for i in range(5)]

    def test_round_trip(self):
        e = emu(); rnd = random.Random(5)
        for _ in range(200):
            spd, dep = rnd.randrange(0, 128) << 8, rnd.randrange(0, 255) << 7
            w = [spd, rnd.randrange(0, 24) << 8, rnd.randrange(0, 7) << 8,
                 rnd.randrange(0, 33) << 8, min(dep, 32512)]
            for i, v in enumerate(w):
                e.w16(SND + 20 + 2 * (K0 + i), v)
            e.run('lfo2_pack', regs={A[1]: SND, A[0]: STO, D[0]: 0x1234})
            self.assertEqual(e.d(0), 0x1234)
            packed = e.r32(STO + 88)
            self.assertEqual(packed >> 30, 2)
            for i in range(5):
                e.w16(SND + 20 + 2 * (K0 + i), 0x5555)
            e.run('lfo2_unpack', regs={A[0]: STO, A[1]: SND, D[0]: 0x4321})
            self.assertEqual(e.d(0), 0x4321)
            self.assertEqual(self._words(e), w)

    def test_old_record_gets_defaults(self):
        e = emu()
        e.w32(STO + 88, 0)                                 # every older record
        e.run('lfo2_unpack', regs={A[0]: STO, A[1]: SND})
        self.assertEqual(self._words(e), DFLT)

    def test_default_fill_hook(self):
        e = emu()
        for i in range(5):
            e.w16(SND + 20 + 2 * (K0 + i), 0)
        # 0x4006186e, the fill's body, stands in as its own epilogue: drop
        # the 36-byte frame and return into our post
        e.uc.mem_write(0x4006186e, bytes.fromhex('4fef00244e75'))
        args = e.run('lfo_init_hook', stack=[SND, 0, 3])
        self.assertEqual(self._words(e), DFLT)
        self.assertEqual(e.sp(), args)                     # balanced: only the args left
        e.run('lfo_init_hook', stack=[0, 0, 3])            # no sound: nothing


class TLockRows(unittest.TestCase):
    def test_save_slot(self):
        e = emu()
        MAP = SCRATCH + 0x8000                              # a stand-in save map
        for k in range(23):
            e.w32(MAP + 4 * k, 0x100 + k)                   # low byte = k
        ROW = SCRATCH + 0x9000
        want = {k: k for k in range(23)}
        want.update({23: 24, 24: 25, 25: 26, 26: 0xff, 27: 0xff,
                     28: 27, 29: 28, 30: 29, 31: 30, 32: 31})
        for k, slot in want.items():
            e.run('lk_slot_save', regs={A[5]: MAP, D[4]: 4 * k, A[3]: ROW, D[2]: 3})
            self.assertEqual((e.r8(ROW), e.r8(ROW + 1)), (slot, 3), k)
            self.assertEqual(e.d(4), 4 * k)                 # kept

    def test_load_word(self):
        e = emu()
        ext = {24: 23, 25: 24, 26: 25, 27: 28, 28: 29, 29: 30, 30: 31, 31: 32}
        for t in range(6):
            for slot, k in ext.items():
                st = e.run_to_any('lk_slot_load', (0x4005aa22, RET), stack=[t, slot])
                self.assertEqual((st, e.d(0)), (RET, k))
        for t, slot in ((0, 3), (5, 23), (2, 32), (6, 24), (6, 2)):
            st = e.run_to_any('lk_slot_load', (0x4005aa22, RET), stack=[t, slot])
            self.assertEqual(st, 0x4005aa22, (t, slot))     # stock, replayed
            self.assertEqual((e.d(2), e.d(1)), (6, t))

    def test_round_trip_all_lockable_words(self):
        e = emu()
        MAP = SCRATCH + 0x8000
        for k in range(23):
            e.w32(MAP + 4 * k, k)
        ROW = SCRATCH + 0x9000
        for k in [23, 24, 25] + list(range(28, 33)):
            e.run('lk_slot_save', regs={A[5]: MAP, D[4]: 4 * k, A[3]: ROW, D[2]: 1})
            slot = e.r8(ROW)
            self.assertLessEqual(slot, 32)                  # the loader's row check
            e.run_to_any('lk_slot_load', (0x4005aa22, RET), stack=[1, slot])
            self.assertEqual(e.d(0), k)


class TKeys(unittest.TestCase):
    VIEW = SCRATCH + 0xa000
    EV = SCRATCH + 0xa100

    def setUp(self):
        self.e = e = emu()
        code(e, 0x4007240c, KEYCODE)
        code(e, 0x40072490, FUNCBIT)
        code(e, 0x40072470, RELEASE)
        e.watch(0x40076082)                                 # redraw

    def key(self, keycode, down, func=False, repeat=False):
        e = self.e
        e.w32(self.EV + 12, keycode)
        e.w32(self.EV + 16, (1 if down else 0) | (2 if func else 0) | (8 if repeat else 0))
        st = e.run_to_any('lfo_key_hook', (0x40024e86, RET), stack=[self.VIEW, self.EV])
        return 'stock' if st == 0x40024e86 else ('ours', e.d(0))

    def test_sequence(self):
        e = self.e
        e.run_to_any('lfo_menu_new', (0x40025798,), stack=[SCRATCH])
        on = lambda: e.r32(e.sym['lfo2_on'])
        self.assertEqual((on(), e.r32(e.sym['lfo_live'])), (0, 1))
        self.assertEqual(self.key(8, False), 'stock')       # the opening press's release
        self.assertEqual(self.key(8, True), ('ours', 1))    # second press: LFO 2
        self.assertEqual(on(), 1)
        self.assertEqual(e.visits[0x40076082], 1)           # redrawn
        self.assertEqual(self.key(8, False), ('ours', 1))   # its release: swallowed
        self.assertEqual(self.key(8, True), ('ours', 1))    # third press: LFO 3
        self.assertEqual(on(), 0)                           # (TMenu34 has the rest)
        e.run_to_any('lfo_menu_dtor', (0x400d97a2,), regs={A[2]: 0x1234})
        self.assertEqual((on(), e.r32(e.sym['lfo_live'])), (0, 0))
        self.assertEqual(e.d(0), 0x401016dc)                # replayed instructions
        self.assertEqual(e.r32(e.sp()), 0x1234)

    def test_other_keys_and_func(self):
        e = self.e
        e.run_to_any('lfo_menu_new', (0x40025798,), stack=[SCRATCH])
        for kc in (1, 5, 7, 9, 16, 32):
            self.assertEqual(self.key(kc, True), 'stock')
        self.assertEqual(self.key(8, True, func=True), 'stock')   # LFO Setup
        self.assertEqual(self.key(8, True, repeat=True), ('ours', 1))
        self.assertEqual(e.r32(e.sym['lfo2_on']), 0)        # a repeat does not switch
        e.run_to_any('lfo_menu_new', (0x40025798,), stack=[SCRATCH])
        self.assertEqual(e.r32(e.sym['lfo2_on']), 0)        # a new menu starts at LFO 1


class TDest(unittest.TestCase):
    """fine_hook -> lfo_dest_step: the step never lands on another LFO's
    destination. The stock step is stood in for by `current + delta`."""
    HANDLE = SCRATCH + 0xb000
    TRK = SCRATCH + 0xb100

    def setUp(self):
        self.e = e = emu()
        # 0x4000c264 stand-in: d0 = current + delta (sp@(12) + sp@(16)), clamped 0..32<<8
        code(e, 0x4000c264, '202f 000c d0af 0010 6c02 7000 0c80 0000 2000 6f06 203c 0000 2000 4e75')
        code(e, 0x40012412, '7003 4e75')                    # the selected track: 3
        e.w32(self.HANDLE + 4, self.TRK)
        vt = SCRATCH + 0xb200
        e.w32(self.TRK, vt)
        e.w32(vt + 40, STUBS)
        code(e, STUBS, '203c' + '%08x' % SND + '4e75')       # the sound
        e.w32(e.sym['lfo_live'], 1)

    def l34(self, lfo, t, dest):                            # LFO 3 (0) or 4 (1)
        self.e.w16(self.e.sym['lfo34_w'] + 60 * lfo + 10 * t + 6, dest << 8)

    def step(self, cur, delta):
        self.e.run('fine_hook', stack=[self.HANDLE, 0x20, cur << 8, delta << 8])
        return self.e.d(0) >> 8

    def test_lfo2_skips_lfo1_dest(self):
        e = self.e
        e.w32(e.sym['lfo_page'], 1)
        e.w16(SND + 20 + 8, 5 << 8)                          # LFO 1 -> word 5
        self.assertEqual(self.step(4, 1), 6)
        self.assertEqual(self.step(6, -1), 4)
        self.assertEqual(self.step(2, 1), 3)
        e.w16(SND + 20 + 8, 32 << 8)                         # at the list's end: stay
        self.assertEqual(self.step(31, 1), 31)

    def test_lfo1_skips_lfo2_dest(self):
        e = self.e
        e.w32(e.sym['lfo_page'], 0)
        e.w16(SND + 20 + 2 * (K0 + 3), 9 << 8)               # LFO 2 -> word 9
        self.assertEqual(self.step(8, 1), 10)
        e.w16(SND + 20 + 2 * (K0 + 3), 0)                    # None: no exclusion
        self.assertEqual(self.step(8, 1), 9)

    def test_lfo3_skips_the_other_three(self):
        e = self.e
        e.w32(e.sym['lfo_page'], 2)
        e.w16(SND + 20 + 8, 5 << 8)                          # LFO 1 -> 5
        e.w16(SND + 20 + 2 * (K0 + 3), 6 << 8)               # LFO 2 -> 6
        self.l34(1, 3, 7)                                    # LFO 4 -> 7 (track 3)
        self.l34(0, 3, 8)                                    # LFO 3 itself: not "other"
        self.assertEqual(self.step(4, 1), 8)                 # three in a row skipped
        self.assertEqual(self.step(8, -1), 4)
        self.l34(1, 2, 9)                                    # another track's: free
        self.assertEqual(self.step(8, 1), 9)
        e.w16(SND + 20 + 2 * (K0 + 3), 32 << 8)              # end of the list: stay
        self.assertEqual(self.step(31, 1), 31)

    def test_lfo1_and_2_skip_lfo3_4(self):
        e = self.e
        self.l34(0, 3, 10); self.l34(1, 3, 11)
        for page, want in ((0, 12), (1, 12), (3, 11)):        # LFO 4 may take its own 11
            e.w32(e.sym['lfo_page'], page)
            self.assertEqual(self.step(9, 1), want, page)

    def test_no_menu_plain_step(self):
        e = self.e
        e.w32(e.sym['lfo_live'], 0)
        e.w16(SND + 20 + 8, 5 << 8)
        e.w32(e.sym['lfo_page'], 1)
        self.assertEqual(self.step(4, 1), 5)


class TRunStub(unittest.TestCase):
    """lfo_run with the engine as an rts: the parameter block LFO 2's engine
    run would read, the state swap, and the application of LFO 2's output."""
    BASE = SCRATCH + 0xc000

    def test_block_and_apply(self):
        e = emu(); s = e.sym
        words = lambda t, k: self.BASE + 14 + 66 * t + 2 * k
        rnd = random.Random(9)
        real = {}
        for t in range(6):
            for k in range(33):
                v = rnd.randrange(0, 32513)
                real[(t, k)] = v
                e.w16(words(t, k), v)
        for t in range(6):                                    # LFO 1 destinations
            e.w16(words(t, 4), (t + 10) << 8)
            real[(t, 4)] = (t + 10) << 8
        stock_state = [rnd.randrange(0, 1 << 32) for _ in range(48)]
        for i, v in enumerate(stock_state):
            e.w32(0x40fde838 + 4 * i, v)
        # LFO 2's state (the engine is a stub, so this is what it "leaves")
        cases = [(3, 2000), (0, 5000), (40, 5000), (13, 9999), (20, 32000), (21, -40000)]
        for t, (dst, mod) in enumerate(cases):
            e.w32(s['lfo2_state'] + 32 * t + 16, dst & M32)
            e.w32(s['lfo2_state'] + 32 * t + 20, mod & M32)
        e.run('lfo_run', stack=[self.BASE, 0x1234, 0x3f])
        # the block: k1..k8 per track = 28, 29, 3, 31, 30, 6, 7, 32
        blk = s['lfo2_buf'] + 14
        for t in range(6):
            got = [e.r16(blk + 66 * t + 2 * k) for k in range(1, 9)]
            want = [real[(t, k)] for k in (28, 29, 3, 31, 30, 6, 7, 32)]
            self.assertEqual(got, want, t)
        # the stock state is back, untouched
        self.assertEqual([e.r32(0x40fde838 + 4 * i) for i in range(48)], stock_state)
        # applied: track 0 word 3 += 2000; 1 None; 2 >32; 3 = LFO 1's dest (13);
        # 4 clamped high; 5 clamped low
        exp = dict(real)
        exp[(0, 3)] = min(32512, real[(0, 3)] + 2000)
        exp[(4, 20)] = 32512 if real[(4, 20)] + 32000 > 32512 else real[(4, 20)] + 32000
        exp[(5, 21)] = 0
        for (t, k), v in exp.items():
            self.assertEqual(e.r16(words(t, k)), v, (t, k))


class TRun34(unittest.TestCase):
    """LFO 3 and 4 in lfo_run (engine as an rts): skipped with no destination,
    else their block comes from lfo34_w and their state is swapped in."""
    BASE = SCRATCH + 0xc000

    def setup(self, e):
        for t in range(6):
            for k in range(33):
                e.w16(self.BASE + 14 + 66 * t + 2 * k, 1000 + 40 * t + k)
            e.w16(self.BASE + 14 + 66 * t + 8, 0)            # LFO 1: no destination
            e.w16(self.BASE + 14 + 66 * t + 2 * (K0 + 3), 0)  # LFO 2: none

    def test_skipped_without_destinations(self):
        e = emu()
        self.setup(e)
        e.watch(0x40091ab2)
        e.run('lfo_run', stack=[self.BASE, 0x1234, 0x3f])
        self.assertEqual(e.visits[0x40091ab2], 2)            # LFO 2 and LFO 1 only

    def test_lfo3_block_state_apply(self):
        e = emu(); s = e.sym
        self.setup(e)
        W = s['lfo34_w']
        for t in range(6):                                   # LFO 3: distinct words
            for i in range(5):
                e.w16(W + 10 * t + 2 * i, 300 * t + 7 * i + 1)
        e.w16(W + 10 * 2 + 6, 17 << 8)                       # track 2 LFO 3 -> word 17
        blk = []
        st3 = s['lfo34_state']

        def eng(uc, addr, size, ud):                         # at each engine call:
            sp = uc.reg_read(UC_M68K_REG_A7)
            buf = struct.unpack('>I', uc.mem_read(sp + 4, 4))[0]
            blk.append((buf, bytes(uc.mem_read(buf + 14, 6 * 66)),
                        bytes(uc.mem_read(0x40fde838, 192))))
            if len(blk) == 2:                                # LFO 3's run "leaves" this
                uc.mem_write(0x40fde838 + 32 * 2 + 16, struct.pack('>ii', 17, 1500))
        e.uc.hook_add(UC_HOOK_CODE, eng, begin=0x40091ab2, end=0x40091ab2)
        for i in range(48):
            e.w32(st3 + 4 * i, 0xa0000000 + i)               # LFO 3's own state
        e.w32(0x40fde838, 0x5555)                            # LFO 1's
        e.run('lfo_run', stack=[self.BASE, 0x1234, 0x3f])
        self.assertEqual(len(blk), 3)                        # LFO 2, 3, then 1
        buf, words, state = blk[1]
        self.assertEqual(buf, s['lfo2_buf'])
        self.assertEqual(struct.unpack('>I', state[:4])[0], 0xa0000000)   # swapped in
        for t in range(6):
            k = lambda n: struct.unpack('>H', words[66 * t + 2 * n:66 * t + 2 * n + 2])[0]
            w = lambda i: 300 * t + 7 * i + 1 if not (t == 2 and i == 3) else 17 << 8
            self.assertEqual([k(1), k(2), k(4), k(5), k(8)], [w(0), w(1), w(3), w(2), w(4)])
            self.assertEqual([k(3), k(6), k(7)], [1000 + 40 * t + n for n in (3, 6, 7)])
        self.assertEqual(blk[2][0], self.BASE)               # LFO 1 on the real block
        self.assertEqual(e.r32(0x40fde838), 0x5555)          # its state back in place
        self.assertEqual(e.r32(st3 + 32 * 2 + 20), 1500)     # LFO 3's kept its own
        self.assertEqual(e.r16(self.BASE + 14 + 66 * 2 + 34), 1000 + 80 + 17 + 1500)


class TMenu34(unittest.TestCase):
    """LFO 3 and 4 as pages 3 and 4 of the stock LFO menu: the key sequence,
    the handle get/set hooks, slotOf and the p-lock editor gate."""
    def setUp(self):
        self.e = e = emu()
        code(e, 0x4007240c, KEYCODE); code(e, 0x40072490, FUNCBIT); code(e, 0x40072470, RELEASE)
        code(e, 0x40012412, '7004 4e75')                    # the selected track: 4

    def key(self, down, func=False, repeat=False, kc=8):
        e = self.e
        EV, VIEW = SCRATCH + 0xa100, SCRATCH + 0xa000
        e.w32(EV + 12, kc)
        e.w32(EV + 16, (1 if down else 0) | (2 if func else 0) | (8 if repeat else 0))
        st = e.run_to_any('lfo_key_hook', (0x40024e86, RET), stack=[VIEW, EV])
        return 'stock' if st == 0x40024e86 else ('ours', e.d(0))

    def test_four_pages(self):
        e = self.e; s = e.sym
        e.watch(0x40076082)
        e.run_to_any('lfo_menu_new', (0x40025798,), stack=[SCRATCH])
        page = lambda: (e.r32(s['lfo_page']), e.r32(s['lfo2_on']))
        self.assertEqual(page(), (0, 0))
        self.assertEqual(self.key(False), 'stock')          # the opening press's release
        for want in ((1, 1), (2, 0), (3, 0)):                # LFO 2, 3, 4
            self.assertEqual(self.key(True), ('ours', 1))
            self.assertEqual(page(), want)
            self.assertEqual(self.key(False), ('ours', 1))   # its release swallowed
        self.assertEqual(e.visits[0x40076082], 3)
        self.assertEqual(e.r32(s['lfo_view']), SCRATCH + 0xa000)
        self.assertEqual(self.key(True, repeat=True), ('ours', 1))
        self.assertEqual(page(), (3, 0))
        self.assertEqual(self.key(True, func=True), 'stock') # LFO Setup
        self.assertEqual(self.key(True), 'stock')            # LFO 4: stock closes it
        self.assertEqual(self.key(False), 'stock')
        e.run_to_any('lfo_menu_dtor', (0x400d97a2,), regs={A[2]: 0})
        self.assertEqual((e.r32(s['lfo_page']), e.r32(s['lfo_live']), e.r32(s['lfo_view'])),
                         (0, 0, 0))

    def test_slot_answers(self):
        e = self.e; s = e.sym
        for page, on, want in ((0, 0, 'stock'), (1, 1, 'lfo2'), (2, 0, -1), (3, 0, -1)):
            e.w32(s['lfo_page'], page); e.w32(s['lfo2_on'], on)
            for i, pid in enumerate(IDS):
                st = e.run_to_any('lfo_slot', (0x4005a4ee, RET), stack=[pid])
                if want == 'stock':
                    self.assertEqual(st, 0x4005a4ee)
                elif want == 'lfo2':
                    self.assertEqual((st, e.d(0)), (RET, K0 + i))
                else:
                    self.assertEqual((st, s32(e.d(0))), (RET, -1))   # no word: no lock
            for pid in (0x1f, 0x22, 0x18):                     # Setup and others: stock
                self.assertEqual(e.run_to_any('lfo_slot', (0x4005a4ee, RET), stack=[pid]),
                                 0x4005a4ee)

    def test_get_set(self):
        e = self.e; s = e.sym
        e.watch(0x40076082)
        W = s['lfo34_w']
        e.w32(s['lfo_view'], 0x1234)
        for page, lfo in ((2, 0), (3, 1)):
            e.w32(s['lfo_page'], page)
            for i, pid in enumerate(IDS):                    # Speed, Mult, Wave, Dest, Depth
                addr = W + 60 * lfo + 40 + 2 * i            # track 4
                e.w16(addr, (-300 - i) & 0xffff)
                self.assertEqual(e.run_to_any('lfo34_get', (0x4000ae20, RET),
                                              stack=[0, pid]), RET)
                self.assertEqual(s32(e.d(0)), -300 - i)
                self.assertEqual(e.run_to_any('lfo34_set', (0x4000ba7a, RET),
                                              stack=[0, pid, 5000 + i, 0, 0]), RET)
                self.assertEqual(e.r16(addr), 5000 + i)
        self.assertEqual(e.visits[0x40076082], 10)           # each set redrawn
        for page in (0, 1):                                   # LFO 1 / 2: the sound
            e.w32(s['lfo_page'], page)
            self.assertEqual(e.run_to_any('lfo34_get', (0x4000ae20, RET), stack=[0, 0x1d]),
                             0x4000ae20)
            self.assertEqual(e.run_to_any('lfo34_set', (0x4000ba7a, RET),
                                          stack=[0, 0x1d, 1, 0, 0]), 0x4000ba7a)
        e.w32(s['lfo_page'], 2)
        for pid in (0x1f, 0x22, 0x23, 0x2a):                  # not one of the five
            self.assertEqual(e.run_to_any('lfo34_get', (0x4000ae20, RET), stack=[0, pid]),
                             0x4000ae20)

    def test_lock_gate(self):
        e = self.e; s = e.sym
        for page in (2, 3):
            e.w32(s['lfo_page'], page)
            self.assertEqual(e.run_to_any('lfo34_lock_gate', (0x40025e16, RET), stack=[1, 2]),
                             RET)                             # ignored
        for page in (0, 1):
            e.w32(s['lfo_page'], page)
            e.uc.reg_write(A[2], 0x7777)
            self.assertEqual(e.run_to_any('lfo34_lock_gate', (0x40025e16, RET),
                                          regs={A[2]: 0x7777}, stack=[1, 2]), 0x40025e16)
            sp = e.sp()
            self.assertEqual(e.r32(sp), 0x7777)               # movel a2,sp@-
            self.assertEqual(e.r32(sp + 4 + 16), RET)          # after lea -16

    def test_number_drawn(self):
        e = self.e; s = e.sym
        calls = []
        e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: calls.append(
            struct.unpack('>I', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 28, 4))[0]),
            begin=0x40071a04, end=0x40071a04)
        for page, want in ((0, None), (1, '2'), (2, '3'), (3, '4')):
            calls.clear()
            e.w32(s['lfo_page'], page)
            e.run('lfo_render_hook', stack=[0, 0x99])
            if want is None:
                self.assertEqual(calls, [])
            else:
                self.assertEqual(len(calls), 1)
                self.assertEqual(bytes(e.uc.mem_read(calls[0], 2)), want.encode() + b'\0')


@unittest.skipUnless(os.environ.get('MODEL_CYCLES_STOCK'), "set MODEL_CYCLES_STOCK for the real-engine test")
class TRealEngine(unittest.TestCase):
    """The REAL stock LFO engine, inside the full patched image."""
    @classmethod
    def setUpClass(cls):
        stock = os.environ['MODEL_CYCLES_STOCK']
        tool = os.environ.get('ELEKTRON_FIRMWARE_TOOL', 'elektron-firmware-tool')
        r = subprocess.run([sys.executable, os.path.join(REPO, 'build.py'), '--stock', stock,
                            '--tool', tool, '--out', os.path.join(BUILD, 'test-lfo2.syx')],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
        cls.img = open(os.path.join(BUILD, 'Model-TG.bin'), 'rb').read()
        cls.sym = symbols(os.path.join(BUILD, '_b.elf'))

    def machine(self):
        from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
        uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        uc.mem_map(0x40000000, 0x10000000)
        uc.mem_map(0x80000000, 0x100000)
        uc.mem_map(0xfc000000, 0x1000000)
        uc.mem_write(0x40000400, self.img)
        uc.mem_write(STUBS, bytes.fromhex('a93c00000020 4e75'))   # movel #0x20,%macsr
        return uc

    def call(self, uc, fn, args):
        sp = 0x4fe00000
        for v in reversed(args):
            sp -= 4; uc.mem_write(sp, struct.pack('>I', v & M32))
        sp -= 4; uc.mem_write(sp, struct.pack('>I', RET))
        uc.reg_write(UC_M68K_REG_SR, 0x2700)        # first: it selects the stack pointer
        uc.reg_write(UC_M68K_REG_A7, sp)
        uc.emu_start(fn, RET, count=5_000_000)
        self.assertEqual(uc.reg_read(UC_M68K_REG_PC), RET)

    def setup_words(self, uc, base, lfo1, lfo2):
        for t in range(6):
            for k in range(33):
                uc.mem_write(base + 14 + 66 * t + 2 * k, struct.pack('>H', 16384))
            for k, v in zip((1, 2, 3, 4, 5, 6, 7, 8), lfo1):
                uc.mem_write(base + 14 + 66 * t + 2 * k, struct.pack('>H', v))
            for k, v in zip(range(28, 33), lfo2):
                uc.mem_write(base + 14 + 66 * t + 2 * k, struct.pack('>H', v))

    def word(self, uc, base, t, k):
        return struct.unpack('>H', uc.mem_read(base + 14 + 66 * t + 2 * k, 2))[0]

    def test_lfo2_matches_lfo1(self):
        base = 0x80001000 + 0xc                         # where the OS keeps them
        for wave in range(7):
            for spd, mult in ((100 << 8, 5 << 8), (64 << 8, 12 << 8), (127 << 8, 20 << 8)):
                uc = self.machine()
                self.call(uc, STUBS, [])
                dep = 100 << 8
                # LFO 1 -> word 18 (Amp Decay); LFO 2 -> word 22 (Pan); same settings
                lfo1 = (spd, mult, 16384, 18 << 8, wave << 8, 0, 0, dep)
                lfo2 = (spd, mult, wave << 8, 22 << 8, dep)
                seen, seen1 = set(), set()
                for blk in range(40):
                    self.setup_words(uc, base, lfo1, lfo2)
                    self.call(uc, self.sym['lfo_run'], [base, 7200, 0x3f if blk == 0 else 0])
                    for t in range(6):
                        m1 = self.word(uc, base, t, 18) - 16384
                        m2 = self.word(uc, base, t, 22) - 16384
                        if wave == 6:      # RND: both draw from the one RNG, so the
                            seen.add(m2)   # values differ; LFO 2 must move as often
                            seen1.add(m1)
                            continue
                        self.assertEqual(m1, m2, (wave, spd, mult, blk, t))
                if wave == 6:
                    self.assertGreaterEqual(len(seen), len(seen1) // 2, (spd, mult))
                    self.assertEqual(len(seen) > 1, len(seen1) > 1, (spd, mult))

    def test_lfo3_and_4_match_lfo1(self):
        base = 0x80001000 + 0xc
        W = self.sym['lfo34_w']
        for wave in (0, 1, 3, 5):
            uc = self.machine()
            self.call(uc, STUBS, [])
            spd, mult, dep = 100 << 8, 5 << 8, 100 << 8
            lfo1 = (spd, mult, 16384, 18 << 8, wave << 8, 0, 0, dep)
            for t in range(6):                      # LFO 3 -> word 22, LFO 4 -> word 21
                for lfo, dst in ((0, 22), (1, 21)):
                    for i, v in enumerate((spd, mult, wave << 8, dst << 8, dep)):
                        uc.mem_write(W + 60 * lfo + 10 * t + 2 * i, struct.pack('>H', v))
            for blk in range(30):
                self.setup_words(uc, base, lfo1, (0, 0, 0, 0, 0))
                self.call(uc, self.sym['lfo_run'], [base, 7200, 0x3f if blk == 0 else 0])
                for t in range(6):
                    m1 = self.word(uc, base, t, 18) - 16384
                    self.assertEqual(self.word(uc, base, t, 22) - 16384, m1, (wave, blk, t))
                    self.assertEqual(self.word(uc, base, t, 21) - 16384, m1, (wave, blk, t))

    def test_lfo1_unchanged_by_lfo2(self):
        """LFO 1's output under lfo_run equals the stock engine called alone."""
        base = 0x80001000 + 0xc
        lfo1 = (90 << 8, 8 << 8, 16384, 19 << 8, 1 << 8, 30 << 8, 1 << 8, 120 << 8)
        lfo2 = (40 << 8, 3 << 8, 2 << 8, 20 << 8, 70 << 8)
        a, b = self.machine(), self.machine()
        self.call(a, STUBS, []); self.call(b, STUBS, [])
        for blk in range(60):
            self.setup_words(a, base, lfo1, lfo2)
            self.setup_words(b, base, lfo1, lfo2)
            mask = 0x3f if blk in (0, 30) else 0
            self.call(a, self.sym['lfo_run'], [base, 7200, mask])
            self.call(b, 0x40091ab2, [base, 7200, mask])        # the stock engine alone
            for t in range(6):
                self.assertEqual(self.word(a, base, t, 19), self.word(b, base, t, 19), (blk, t))


if __name__ == '__main__':
    unittest.main(verbosity=2)
