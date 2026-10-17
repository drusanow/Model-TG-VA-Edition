#!/usr/bin/env python3
"""Host tests for the LFOs sent as MIDI CCs (src/lfocc.inc).

The blob runs on Unicorn with the stock OS as `rts` stubs (tests/va_emu.py);
the stock routines the code calls are stood in for from Python (the CC send
0x40082800, the track's channel 0x40012e82, the selected track, the small
file calls, the menu item constructor), as tests/test_mutesolo.py does.

    python3 -m unittest tests.test_lfocc -v
"""
import os, sys, struct, unittest, math

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
        return self.s['lfo34_kits'] + 172 * self.KIT

    def area(self, kit=None):
        """A kit's MIDI CC settings: the numbers (0 OFF, CC + 1), then the
        bases ^ 64."""
        return self.s['lfo34_kits'] + 172 * (self.KIT if kit is None else kit) + 124

    def num(self, lfo, t, cc, kit=None):
        """LFO lfo of track t sends CC cc (None: OFF)."""
        self.e.w8(self.area(kit) + 6 * lfo + t, 0 if cc is None else cc + 1)
        self.e.run('lcc_remask', regs={A[0]: self.area()})

    def base(self, lfo, t, v, kit=None):
        self.e.w8(self.area(kit) + 24 + 6 * lfo + t, v ^ 64)

    def rbase(self, lfo, t):
        return self.e.r8(self.area() + 24 + 6 * lfo + t) ^ 64

    def state(self, lfo, t):
        return [LFO_STATE, self.s['lfo2_state'], self.s['lfo34_state'],
                self.s['lfo34_state'] + 192][lfo] + 32 * t

    def phase(self, lfo, t, p, fade=1 << 30):
        """The engine's phase (0..1 of a cycle) and fade gain."""
        self.e.w32(self.state(lfo, t), int(p * 1382400000) % 1382400000)
        self.e.w32(self.state(lfo, t) + 8, fade)

    def words(self, lfo, t):
        """-> (the Waveform word, the Depth word) addresses"""
        if lfo == 0:
            return PAR + 14 + 66 * t + 10, PAR + 14 + 66 * t + 16
        if lfo == 1:
            return PAR + 14 + 66 * t + 60, PAR + 14 + 66 * t + 64
        b = self.blk() + 60 * (lfo - 2) + 10 * t
        return b + 4, b + 8

    def setup(self, lfo, t, wave, depth_steps, base):
        w, d = self.words(lfo, t)
        self.e.w16(w, wave << 8)
        self.e.w16(d, 16384 + depth_steps * 256)
        self.base(lfo, t, base)

    def val(self, lfo, t):
        return self.e.r8(self.s['lcc_val'] + 6 * lfo + t)

    def calc(self):
        self.e.run('lcc_calc', regs={A[5]: PAR})


def model(wave, p, depth_steps, base, fade=1 << 30):
    """lcc_calc in Python: Base + Depth x the wave (0..1)."""
    q = min(int(p * 1382400000) % 1382400000 // 21094, 65535)
    if wave == 0:
        u = 2 * q if 2 * q <= 65535 else 131071 - 2 * q
    elif wave == 1:
        sv = round(32767 * math.sin(2 * math.pi * (q >> 9) / 256))
        u = sv * sv >> 14
    elif wave == 2:
        u = 65535 if q < 32768 else 0
    elif wave == 3:
        u = 65535 - q
    elif wave == 4:
        a = 65535 - q
        a = a * a >> 16
        u = a * a >> 16
    elif wave == 5:
        u = q
    u = u * (fade >> 14) >> 16
    v = (depth_steps * 256 * u + (1 << 23)) >> 24
    return min(max(base + v, 0), 127)


class TCalc(Base):
    def test_from_the_base(self):
        """Base 50, Depth -15: 50 down to 35, whatever the LFO and the wave."""
        for lfo in range(4):
            self.num(lfo, 3, 74)
            for wave in range(6):
                self.setup(lfo, 3, wave, -15, 50)
                seen = set()
                for i in range(64):
                    self.phase(lfo, 3, i / 64)
                    self.calc()
                    v = self.val(lfo, 3)
                    self.assertEqual(v, model(wave, i / 64, -15, 50), (lfo, wave, i))
                    seen.add(v)
                self.assertEqual((min(seen), max(seen)), (35, 50), (lfo, wave))
            self.assertEqual(self.val(lfo, 2), 0xff)            # OFF: left alone

    def test_waves_depth_fade_and_clip(self):
        e = self.e
        self.num(0, 0, 1)
        for wave, p, d, b, fade in ((0, 0.25, 20, 100, 1 << 30), (0, 0.5, 63, 0, 1 << 30),
                                    (1, 0.5, 40, 10, 1 << 30), (2, 0.1, -64, 64, 1 << 30),
                                    (3, 0.75, 30, 0, 1 << 29), (4, 0.2, 63, 64, 1 << 30),
                                    (5, 0.9, 63, 100, 1 << 30), (0, 0.5, 30, 50, 0)):
            self.setup(0, 0, wave, d, b)
            self.phase(0, 0, p, fade)
            self.calc()
            self.assertEqual(self.val(0, 0), model(wave, p, d, b, fade), (wave, p, d, b))
        self.setup(0, 0, 0, 63, 100)                            # clipped at 127
        self.phase(0, 0, 0.5)
        self.calc()
        self.assertEqual(self.val(0, 0), 127)

    def test_rnd_holds_a_cycle(self):
        self.num(1, 2, 5)
        self.setup(1, 2, 6, 63, 0)
        vals = []
        for cyc in range(6):
            got = set()
            for i in range(8):
                self.phase(1, 2, i / 8)
                self.calc()
                got.add(self.val(1, 2))
            self.assertEqual(len(got), 1)                       # held through the cycle
            vals.append(got.pop())
        self.assertGreater(len(set(vals)), 3)                   # and new each one

    def test_nothing_on_does_nothing(self):
        self.setup(0, 0, 0, 63, 64)
        self.phase(0, 0, 0.5)
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
    CLO, DATA, ITEM = SCRATCH + 0x9200, SCRATCH + 0x9210, SCRATCH + 0x9300

    def setUp(self):
        super().setUp()
        e = self.e
        e.w32(self.CLO, self.DATA)
        e.w32(self.DATA, 0)
        self.editing = 0
        self.toggles = []
        self.st.set(0x400732f8, lambda a: self.editing)         # the item: being edited?
        self.st.set(0x400732ec, self._toggle)                    # ...set it

    def _toggle(self, a):
        self.toggles.append(a[1] & 0xff)
        self.editing = a[1] & 0xff

    def press(self):
        self.e.run('lcc_press', stack=[self.CLO, self.ITEM])

    def turn(self, fn, delta):
        self.e.run(fn, stack=[self.CLO, 0, delta & M32])

    def shows(self, fn):
        e = self.e
        e.run_to_any(fn, (self.s['gm_val'],), stack=[0, 0, 0, 0, 0])
        b = bytes(e.uc.mem_read(e.r32(e.a(1)), 8))
        return b[:b.index(0)].decode()

    def test_cc_taken_on_the_press(self):
        e = self.e
        e.w32(self.s['lfo_page'], 2)                            # LFO 3
        i = self.area() + 12 + self.TRACK
        self.turn('lcc_rot', 1)                                  # not editing: nothing
        self.assertEqual(e.r8(i), 0)
        self.press()                                             # into the edit
        self.assertEqual(self.toggles, [1])
        for _ in range(75):
            self.turn('lcc_rot', 1)
        self.assertEqual(self.shows('lcc_value'), '74')         # shown, pending...
        self.assertEqual((e.r8(i), e.r32(self.s['lcc_mask'])), (0, 0))   # ...not in force
        self.press()                                             # the press: taken
        self.assertEqual(self.toggles, [1, 0])
        self.assertEqual((e.r8(i), e.r32(self.s['lcc_mask'])), (75, 0b100))
        self.assertEqual(self.shows('lcc_value'), '74')
        self.press()                                             # again, turned to OFF
        for _ in range(80):
            self.turn('lcc_rot', -1)
        self.assertEqual(self.shows('lcc_value'), 'OFF')
        self.assertEqual(e.r8(i), 75)
        self.press()
        self.assertEqual((e.r8(i), e.r32(self.s['lcc_mask'])), (0, 0))

    def test_unpressed_edit_takes_nothing(self):
        e = self.e
        e.uc.mem_write(0x400d97a2, bytes.fromhex('588f4e75'))
        i = self.area() + self.TRACK
        self.press()
        self.turn('lcc_rot', 1)
        self.turn('lcc_rot', 1)
        e.run('lfo_menu_dtor')                                   # closed mid-edit
        self.assertEqual((e.r8(i), e.r32(self.s['lcc_ed'])), (0, 0))
        self.assertEqual(self.shows('lcc_value'), 'OFF')

    def test_clamps(self):
        e = self.e
        i = self.area() + self.TRACK
        self.press()
        for _ in range(200):
            self.turn('lcc_rot', 1)
        self.press()
        self.assertEqual((e.r8(i), self.shows('lcc_value')), (128, '127'))

    def test_base(self):
        e = self.e
        e.w32(self.s['lfo_page'], 1)
        b = lambda: self.rbase(1, self.TRACK)
        self.assertEqual((b(), self.shows('lcc_valueb')), (64, '64'))   # a cleared kit: 64
        for _ in range(14):
            self.turn('lcc_rotb', -1)
        self.assertEqual((b(), self.shows('lcc_valueb')), (50, '50'))
        for _ in range(100):
            self.turn('lcc_rotb', 1)
        self.assertEqual(b(), 127)
        for _ in range(200):
            self.turn('lcc_rotb', -1)
        self.assertEqual((b(), self.shows('lcc_valueb')), (0, '0'))

    def test_shows(self):
        e = self.e
        e.w32(self.s['lfo_page'], 1)
        i = self.area() + 6 + self.TRACK
        for n, want in ((0, 'OFF'), (1, '0'), (8, '7'), (11, '10'), (75, '74'),
                        (100, '99'), (101, '100'), (128, '127')):
            e.w8(i, n)
            self.assertEqual(self.shows('lcc_value'), want)

    def test_row_added_to_the_lfo_menu(self):
        e = self.e
        MENU = SCRATCH + 0xb000
        heap = [SCRATCH + 0xc000]

        def alloc(a):
            heap[0] += 0x100
            return heap[0]
        made = []
        self.st.set(0x40080064, alloc)
        # the closures live in the caller's frame, reused row after row: read
        # them as the constructor gets them
        self.st.set(0x400734b0, lambda a: made.append(
            a[:7] + tuple(e.r32(c + 12) for c in a[1:5]) + (e.r32(e.r32(a[4])),)))
        added = []
        self.st.set(0x40072ce6, lambda a: added.append(a[:2]))
        self.st.set(0x40025798, lambda a: a[0])                # the stock constructor
        e.run('lfo_menu_new', stack=[MENU, 0x77, 0x88])
        self.assertEqual(e.d(0), MENU)
        self.assertEqual(len(made), 2)
        self.assertEqual(added, [(MENU, m[0]) for m in made])
        want = [('lcc_lab', 'lcc_press', 'lcc_value', 'lcc_rot'),
                ('lcc_labb', 'new_slot2_invoker', 'lcc_valueb', 'lcc_rotb')]
        for m, names in zip(made, want):
            self.assertEqual(list(m[7:11]), [self.s[n] for n in names])
            self.assertEqual(m[11], MENU)                       # the turn's data: the menu
            self.assertEqual(m[5:7], (M32, 8))


def fnv(b):
    h = 0x811c9dc5
    for x in b:
        h = ((h ^ x) * 16777619) & M32
    return h or 1


class TPattern(Base):
    """The settings belong to each pattern's kit: the playing one is used,
    saved with the kit (a hash at +700 of its record, the settings in a file
    named by it), loaded back, copied and cleared with it."""
    STO = SCRATCH + 0x10000

    def kit(self, n):
        return KIT_BASE + n * KIT_SIZE

    def save(self, n, fill=0):
        e = self.e
        e.uc.mem_write(self.STO, bytes([fill]) * 7108)
        e.uc.mem_write(0x4005b2b6, bytes.fromhex('70014fef001c4e75'))
        args = e.run('kit_save_hook', stack=[self.STO, self.kit(n), 0])
        self.assertEqual(e.sp(), args)

    def load(self, n):
        e = self.e
        e.uc.mem_write(0x4005d088, bytes.fromhex('70014fef00244e75'))
        args = e.run('kit_load_hook', stack=[self.kit(n), self.STO])
        self.assertEqual(e.sp(), args)

    def settings(self, kit):
        return bytes(self.e.uc.mem_read(self.area(kit), 48))

    def test_each_pattern_its_own(self):
        e = self.e
        T = self.TRACK
        self.num(0, T, 10, kit=4)
        self.num(0, T, 20, kit=5)
        self.base(0, T, 30, kit=5)
        self.assertEqual(TMenu.shows(self, 'lcc_value'), '10')   # kit 4 playing
        e.w32(CUR_KIT, self.kit(5))                              # the next pattern
        self.assertEqual(TMenu.shows(self, 'lcc_value'), '20')
        self.assertEqual(TMenu.shows(self, 'lcc_valueb'), '30')
        self.setup(0, T, 0, 0, 30)                               # Depth 0: the base
        self.phase(0, T, 0.3)
        self.calc()
        e.run('lcc_poll')
        self.assertEqual(self.sent[-1], (T + 2, 20, 30))         # kit 5's CC and base

    def test_saved_and_loaded(self):
        e = self.e
        self.num(0, 0, 74, kit=4)
        self.num(3, 5, 0, kit=4)
        self.base(1, 1, 33, kit=4)
        want = self.settings(4)
        self.save(4, fill=0)
        rec = bytes(e.uc.mem_read(self.STO + 700, 8))
        h = fnv(want)
        self.assertEqual(rec[:5], bytes([0x43]) + h.to_bytes(4, 'big'))
        self.assertEqual(len(self.posted), 1)                   # its file: a message
        e.run('lcc_write', stack=[0, 0])
        name = '/LFOCC/%08X' % h
        self.assertEqual(self.files, {name: want})
        e.uc.mem_write(self.area(9), b'\x11' * 48)               # into kit 9
        self.load(9)
        self.assertEqual(self.settings(9), bytes(48))           # cleared, then...
        self.assertEqual(e.r32(self.s['lcc_lpn']), 1)
        e.run('lcc_poll')
        self.assertEqual(self.settings(9), want)                # ...read on the UI clock
        e.run('lcc_poll')
        self.assertEqual(e.r32(self.s['lcc_lpn']), 0)

    def test_all_off_writes_nothing(self):
        e = self.e
        self.save(4, fill=0xa5)
        self.assertEqual(bytes(e.uc.mem_read(self.STO + 646, 2)), bytes([0, 0xff]))
        self.assertEqual(bytes(e.uc.mem_read(self.STO + 700, 8)), b'\xa5' * 8)
        self.assertEqual(self.posted, [])

    def test_older_or_odd_records_load_off(self):
        e = self.e
        for rec in (bytes(8), b'\x43\x12\x34\x56\x78\x00\x00\x00', b'\x44' + bytes(7)):
            e.uc.mem_write(self.area(6), b'\x22' * 48)
            e.uc.mem_write(self.STO + 700, rec)
            self.load(6)
            self.assertEqual(self.settings(6), bytes(48))
            self.assertEqual(e.r32(self.s['lcc_lh'] + 4 * 6), 0)

    def test_missing_or_wrong_file(self):
        e = self.e
        self.num(0, 0, 9, kit=4)
        self.save(4)
        e.run('lcc_write', stack=[0, 0])
        name = list(self.files)[0]
        for content in (None, b'\x01' * 48, b'\x00' * 40):
            if content is None:
                del self.files[name]
            else:
                self.files[name] = content
            self.load(7)
            e.run('lcc_poll')
            self.assertEqual(self.settings(7), bytes(48), content)

    def test_copied_and_cleared_with_the_kit(self):
        e = self.e
        self.num(2, 3, 50, kit=4)
        want = self.settings(4)
        e.run('kit_memcpy', stack=[self.kit(30), self.kit(4), KIT_SIZE])
        self.assertEqual(self.settings(30), want)
        e.run_to_any('sfx_fxd', (0x40061320,), stack=[self.kit(30) + 628])
        self.assertEqual(self.settings(30), bytes(48))


if __name__ == '__main__':
    unittest.main()
