#!/usr/bin/env python3
"""Host tests for the LFOs sent as MIDI CCs (src/lfocc.inc).

The blob runs on Unicorn with the stock OS as `rts` stubs (tests/va_emu.py);
the stock routines the code calls are stood in for from Python (the CC send
0x40082800, the track's channel 0x40012e82, the selected track, the small
file calls, the menu item constructor), as tests/test_mutesolo.py does.

    python3 -m unittest tests.test_lfocc -v
"""
import os, sys, struct, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

KIT_BASE, KIT_SIZE = 0x409C9C80, 7094
CUR_KIT = 0x40a78888
LFO_STATE = 0x40fde838
PAR = SCRATCH + 0x20000                      # the audio thread's parameter block
SEND, CHAN = 0x40082800, 0x40012e82
OPEN, READ, CLOSE, TRUNC, WRITE = 0x4007bc5e, 0x4007baaa, 0x4007bc1c, 0x4007bb76, 0x4007bb0a
POST = 0x40001fba
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


class Stubs:
    """Python stand-ins for stock routines: fn(args) -> d0, then return.
    Hooked on their addresses only."""
    def __init__(self, e):
        self.e = e

    def set(self, addr, fn):
        def hook(uc, a, size, ud):
            sp = uc.reg_read(UC_M68K_REG_A7)
            args = struct.unpack('>7I', uc.mem_read(sp + 4, 28))
            uc.reg_write(D[0], (fn(args) or 0) & M32)
            uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', uc.mem_read(sp, 4))[0])
            uc.reg_write(UC_M68K_REG_A7, sp + 4)
        self.e.uc.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)


class Base(unittest.TestCase):
    KIT = 4
    TRACK = 2

    def setUp(self):
        self.e = e = emu()
        self.s = e.sym
        e.w32(CUR_KIT, KIT_BASE + self.KIT * KIT_SIZE)
        self.sent = []
        self.chans = {t: t + 2 for t in range(6)}           # track t on channel t + 2
        self.files = {}
        self.posted = []
        st = self.st = Stubs(e)
        st.set(SEND, lambda a: self.sent.append((a[0], a[1] & 0xff, a[2] & 0xff)) or 1)
        st.set(CHAN, lambda a: self.chans[a[1]])
        st.set(0x40012412, lambda a: self.TRACK)            # the selected track
        st.set(OPEN, self._open)
        st.set(READ, self._read)
        st.set(WRITE, self._write)
        st.set(POST, lambda a: self.posted.append(a[1]))

    # a one-file file system
    def _open(self, a):
        path = bytes(self.e.uc.mem_read(a[0], 32)).split(b'\0')[0].decode()
        mode = {0x40126ce4: 'r', 0x401272db: 'w'}[a[1]]          # the stock's strings
        if mode == 'r' and path not in self.files:
            return 1
        self.cur = path
        if mode == 'w':
            self.files[path] = b''
        return 0

    def _read(self, a):
        data = self.files[self.cur][:a[1]]
        self.e.uc.mem_write(a[0], data)
        return len(data)

    def _write(self, a):
        self.files[self.cur] += bytes(self.e.uc.mem_read(a[0], a[1]))
        return a[1]

    def blk(self):
        return self.s['lfo34_kits'] + 124 * self.KIT

    def num(self, lfo, t, cc):
        """LFO lfo of track t sends CC cc (None: OFF)."""
        self.e.w8(self.s['lcc_num'] + 6 * lfo + t, 0 if cc is None else cc + 1)
        self.e.run('lcc_remask')

    def wave(self, lfo, t, v):
        base = [LFO_STATE, self.s['lfo2_state'], self.s['lfo34_state'], self.s['lfo34_state'] + 192][lfo]
        self.e.w32(base + 32 * t + 4, v)

    def depth(self, lfo, t, v):
        if lfo == 0:
            self.e.w16(PAR + 14 + 66 * t + 16, v)
        elif lfo == 1:
            self.e.w16(PAR + 14 + 66 * t + 64, v)
        else:
            self.e.w16(self.blk() + 60 * (lfo - 2) + 10 * t + 8, v)

    def val(self, lfo, t):
        return self.e.r8(self.s['lcc_val'] + 6 * lfo + t)

    def calc(self):
        self.e.run('lcc_calc', regs={A[5]: PAR})


class TCalc(Base):
    def test_values(self):
        e = self.e
        for lfo in range(4):
            self.num(lfo, 3, 74)
            for w, d, want in ((0x7fffffff, 32767, 127), (-0x80000000, 32767, 0),
                               (0x40000000, 32767, 95), (0, 32767, 64),
                               (-0x20000000, 24576, 56),             # -1/4 x +1/4 of the way
                               (0x7fffffff, 16384, 64),             # Depth 0
                               (0x7fffffff, 0, 0),                  # Depth -64: inverted
                               (-0x80000000, 0, 127)):
                self.wave(lfo, 3, w)
                self.depth(lfo, 3, d)
                self.calc()
                self.assertEqual(self.val(lfo, 3), want, (lfo, hex(w), d))
            self.assertEqual(self.val(lfo, 2), 0xff)            # OFF: left alone

    def test_nothing_on_does_nothing(self):
        self.wave(0, 0, 0x7fffffff)
        self.depth(0, 0, 32767)
        self.calc()
        self.assertEqual(self.val(0, 0), 0xff)

    def test_mask(self):
        e = self.e
        self.num(2, 5, 1)
        self.num(0, 0, 0)
        self.assertEqual(e.r32(self.s['lcc_mask']), 0b101)
        self.num(2, 5, None)
        self.assertEqual(e.r32(self.s['lcc_mask']), 0b001)

    def test_lfo3_runs_for_its_cc(self):
        """LFO 3 with no Destination on any track is not run - unless some
        track sends it as a CC."""
        e = self.e
        e.watch(0x40091ab2)
        e.run('lfo_run', stack=[PAR, 7200, 0])
        base = e.visits[0x40091ab2]                             # LFO 2 and LFO 1
        self.num(2, 1, 20)
        e.run('lfo_run', stack=[PAR, 7200, 0])
        self.assertEqual(e.visits[0x40091ab2] - base, base + 1)
        self.num(3, 1, 21)
        e.run('lfo_run', stack=[PAR, 7200, 0])
        self.assertEqual(e.visits[0x40091ab2] - 2 * base - 1, base + 2)


class TSend(Base):
    def setUp(self):
        super().setUp()
        self.e.w32(self.s['lcc_ld'], 1)                         # no file to read

    def poll(self, n=1):
        for _ in range(n):
            self.e.run('lcc_poll')

    def setval(self, lfo, t, v):
        self.e.w8(self.s['lcc_val'] + 6 * lfo + t, v)

    def test_sends_on_change_only(self):
        self.num(0, 1, 74)
        self.poll(3)
        self.assertEqual(self.sent, [])                         # nothing worked out yet
        self.setval(0, 1, 30)
        self.poll(20)
        self.assertEqual(self.sent, [(3, 74, 30)])              # track 1's channel
        self.setval(0, 1, 31)
        self.poll(20)
        self.assertEqual(self.sent[-1], (3, 74, 31))
        self.assertEqual(len(self.sent), 2)

    def test_rate(self):
        self.num(1, 0, 1)
        sent = 0
        for i in range(120):
            self.setval(1, 0, i % 100)
            self.poll()
        self.assertEqual(len(self.sent), 10)                    # one each 12 blocks
        self.assertTrue(all(c == (2, 1, c[2]) for c in self.sent))

    def test_one_a_block_round_the_lfos(self):
        for lfo, t in ((0, 0), (1, 1), (2, 2), (3, 3)):
            self.num(lfo, t, 10 + lfo)
            self.setval(lfo, t, 50)
        self.poll()
        self.assertEqual(len(self.sent), 1)
        self.poll(3)
        self.assertEqual(sorted(self.sent), [(2, 10, 50), (3, 11, 50), (4, 12, 50), (5, 13, 50)])

    def test_channel_off(self):
        self.chans[4] = 0xffffffff
        self.num(0, 4, 7)
        self.setval(0, 4, 9)
        self.poll(30)
        self.assertEqual(self.sent, [])

    def test_cc_zero_and_127(self):
        self.num(0, 0, 0)
        self.num(0, 1, 127)
        self.setval(0, 0, 1)
        self.setval(0, 1, 2)
        self.poll(2)
        self.assertEqual(sorted(self.sent), [(2, 0, 1), (3, 127, 2)])


class TMenu(Base):
    def test_turns(self):
        e = self.e
        clo, data = SCRATCH + 0x9200, SCRATCH + 0x9210
        e.w32(clo, data)
        e.w32(data, 0)
        e.w32(self.s['lfo_page'], 2)                            # LFO 3
        i = self.s['lcc_num'] + 12 + self.TRACK
        seen, v = [], 0
        for delta in (1, 1, -1, -1, -1, 5, 0):
            e.run('lcc_rot', stack=[clo, 0, delta & M32])
            v = min(max(v + (delta > 0) - (delta < 0), 0), 128)
            seen.append((e.r8(i), v))
        self.assertEqual([a for a, b in seen], [b for a, b in seen])
        e.w8(i, 127)
        e.run('lcc_rot', stack=[clo, 0, 1])
        e.run('lcc_rot', stack=[clo, 0, 1])
        self.assertEqual(e.r8(i), 128)                          # CC 127 the last
        self.assertEqual(e.r32(self.s['lcc_mask']), 0b100)
        self.assertEqual(e.r32(self.s['lcc_dirty']), 1)
        self.assertEqual(e.r8(self.s['lcc_sent'] + 12 + self.TRACK), 0xff)

    def test_shows(self):
        e = self.e
        e.w32(self.s['lfo_page'], 1)
        i = self.s['lcc_num'] + 6 + self.TRACK
        for n, want in ((0, 'OFF'), (1, '0'), (8, '7'), (11, '10'), (75, '74'),
                        (100, '99'), (101, '100'), (128, '127')):
            e.w8(i, n)
            st = e.run_to_any('lcc_value', (self.s['gm_val'],), stack=[0, 0, 0, 0, 0])
            self.assertEqual(st, self.s['gm_val'])
            self.assertEqual(e.r32(e.a(0)), 0)
            name = e.r32(e.a(1))
            b = bytes(e.uc.mem_read(name, 8))
            self.assertEqual(b[:b.index(0)].decode(), want)

    def test_row_added_to_the_lfo_menu(self):
        e = self.e
        MENU = SCRATCH + 0xb000
        heap = [SCRATCH + 0xc000]

        def alloc(a):
            heap[0] += 0x100
            return heap[0]
        made = []
        self.st.set(0x40080064, alloc)
        self.st.set(0x400734b0, lambda a: made.append(a))
        added = []
        self.st.set(0x40072ce6, lambda a: added.append(a[:2]))
        self.st.set(0x40025798, lambda a: a[0])                # the stock constructor
        e.run('lfo_menu_new', stack=[MENU, 0x77, 0x88])
        self.assertEqual(e.d(0), MENU)
        self.assertEqual(len(made), 1)
        item, lab, press, value, turn = made[0][:5]
        self.assertEqual(added, [(MENU, item)])
        fns = [e.r32(c + 12) for c in (lab, press, value, turn)]
        self.assertEqual(fns, [self.s['lcc_lab'], self.s['new_slot2_invoker'],
                               self.s['lcc_value'], self.s['lcc_rot']])
        self.assertEqual(e.r32(e.r32(turn)), MENU)              # the turn's data: the menu
        self.assertEqual(made[0][5:7], (M32, 8))


class TFile(Base):
    def test_round_trip(self):
        e = self.e
        self.num(0, 0, 74)
        self.num(3, 5, 0)
        e.w32(self.s['lcc_dirty'], 1)
        e.uc.mem_write(0x400d97a2, bytes.fromhex('588f4e75'))  # the stock rest: pop a2
        e.run('lfo_menu_dtor')                                 # the menu closes: posted
        self.assertEqual(len(self.posted), 1)
        self.assertEqual(e.r32(self.s['lcc_dirty']), 0)
        e.run('lcc_write', stack=[0, 0])
        f = self.files['/lfo_cc.bin']
        self.assertEqual(len(f), 32)
        self.assertEqual(f[:4], b'LCC1')
        self.assertEqual((f[4], f[4 + 23]), (75, 1))
        for i in range(24):                                     # forgotten, then read
            e.w8(self.s['lcc_num'] + i, 0)
        e.run('lcc_remask')
        e.run('lcc_poll')
        self.assertEqual(e.r32(self.s['lcc_ld']), 1)
        self.assertEqual((e.r8(self.s['lcc_num']), e.r8(self.s['lcc_num'] + 23)), (75, 1))
        self.assertEqual(e.r32(self.s['lcc_mask']), 0b1001)

    def test_no_file_or_a_bad_one(self):
        e = self.e
        e.run('lcc_poll')
        self.assertEqual(e.r32(self.s['lcc_mask']), 0)
        self.files['/lfo_cc.bin'] = b'LCC2' + bytes(28)
        e.w32(self.s['lcc_ld'], 0)
        e.run('lcc_poll')
        self.assertEqual(e.r32(self.s['lcc_mask']), 0)
        self.files['/lfo_cc.bin'] = b'LCC1' + bytes([200] + [3] * 23) + bytes(4)
        e.w32(self.s['lcc_ld'], 0)
        e.run('lcc_poll')
        self.assertEqual((e.r8(self.s['lcc_num']), e.r8(self.s['lcc_num'] + 1)), (0, 3))

    def test_closing_unchanged_writes_nothing(self):
        self.e.uc.mem_write(0x400d97a2, bytes.fromhex('588f4e75'))
        self.e.run('lfo_menu_dtor')
        self.assertEqual(self.posted, [])


if __name__ == '__main__':
    unittest.main()
