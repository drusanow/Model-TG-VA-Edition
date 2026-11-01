#!/usr/bin/env python3
"""Host tests for LXR's voice menu (SETTINGS + PRESET on an LXR track: src/lxr/lxr_menu.c, GPL-3.0-only; glue
src/lxr_glue.inc, mm_open in src/model_tg.s): its rows for each voice, the values it shows, the edits it makes - the
preset's knobs copied in at the first, clamped, put back - and what they do: the voice plays them on their preset
only, the macros still move from them, each pattern's kit has its own, saved and loaded with it (its MIDI CC file,
src/lfocc.inc), copied and cleared with it.

    python3 -m unittest tests.test_lxr_menu -v
"""
import os, sys, struct, unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
from test_lfocc import Stubs, fnv, KIT_BASE, KIT_SIZE, CUR_KIT, OPEN, READ, WRITE, POST
import test_lxr as L

LX = 13
MM_LXR = 6
ED = SCRATCH + 0x62000                            # a stand-in edits block
RECT, TEXT = 0x40070dea, 0x40071a04
N_P = 31
P = dict(WAVE=0, WAVE2=1, WAVE3=2, COARSE=3, FINE=4, MODF1=5, MODF2=6, GAIN1=7, GAIN2=8, MIXMODE=9, PDEC=10,
         PAMT=11, PSLOPE=12, ATT=13, DEC=14, DECO=15, VSLOPE=16, REPEAT=17, NOISEF=18, MIX=19, FTYPE=20, FFREQ=21,
         FRESO=22, FDRIVE=23, DIST=24, VOL=25, DECIM=26, TWAVE=27, TVOL=28, TFREQ=29, PWM=30)
# the rows of each voice, as the page lists them (then the row that puts the preset back)
DRUM = ['OSC WAVE', 'COARSE', 'FINE', 'MOD WAVE', 'MOD FREQ', 'FM AMOUNT', 'MOD MODE', 'PITCH DEC', 'PITCH AMT',
        'PITCH SLOPE', 'ATTACK', 'DECAY', 'VOL SLOPE', 'FILTER', 'CUTOFF', 'RESONANCE', 'FILT DRIVE', 'DISTORTION',
        'SAMPLE RATE', 'TRANSIENT', 'TRANS VOL', 'TRANS PITCH', 'PWM', 'VOLUME']
SNARE = ['OSC WAVE', 'COARSE', 'FINE', 'NOISE FREQ', 'NOISE MIX', 'PITCH DEC', 'PITCH AMT', 'PITCH SLOPE', 'ATTACK',
         'DECAY', 'VOL SLOPE', 'REPEAT', 'FILTER', 'CUTOFF', 'RESONANCE', 'FILT DRIVE', 'DISTORTION', 'SAMPLE RATE',
         'TRANSIENT', 'TRANS VOL', 'TRANS PITCH', 'PWM', 'VOLUME']
METAL = ['OSC WAVE', 'COARSE', 'FINE', 'MOD1 WAVE', 'MOD1 FREQ', 'MOD1 GAIN', 'MOD2 WAVE', 'MOD2 FREQ', 'MOD2 GAIN',
         'ATTACK']
CYM = METAL + ['DECAY', 'VOL SLOPE', 'REPEAT'] + DRUM[13:]
HAT = METAL + ['DECAY', 'OPEN DECAY', 'VOL SLOPE'] + DRUM[13:]
ROWS = {L.KICK: DRUM, L.TOM: DRUM, L.PERC: DRUM, L.SNARE: SNARE, L.CLAP: SNARE, L.CHAT: HAT, L.OHAT: HAT, L.CYMB: CYM}
TITLE = ['LXR KICK', 'LXR TOM', 'LXR PERC', 'LXR SNARE', 'LXR CLAP', 'LXR CLOSED HAT', 'LXR OPEN HAT', 'LXR CYMBAL']
FONT_W = 6                                        # the small font's widest character and its gap, at most


def presets():
    """lxr_presets.h's eight presets, in P_* order"""
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'lxr', 'lxr_presets.h')).read()
    out = []
    for line in src.splitlines():
        if line.strip().startswith('/*') and '{' in line and '}' in line and 'static' not in line:
            out.append([int(x) for x in line.split('{')[1].split('}')[0].split(',')])
    assert len(out) == 8
    return out


PRE = presets()


class Base(unittest.TestCase):
    def setUp(self):
        self.e = e = L.emu()
        self.s = e.sym
        e.uc.mem_write(ED, bytes(32))
        e.run('lxr_menu_open')

    def turn(self, knob, clicks, eng=L.KICK, ed=ED):
        self.e.run('lxr_menu_turn', stack=[ed, L.contour(eng), knob, clicks & M32])
        return self.e.d(0)

    def press(self, eng=L.KICK, ed=ED):
        self.e.run('lxr_menu_press', stack=[ed, L.contour(eng)])
        return self.e.d(0)

    def ed(self, at=ED):
        return list(self.e.uc.mem_read(at, 32))

    def to_row(self, r, eng=L.KICK):
        self.turn(1, -100, eng)
        if r:
            self.turn(1, r, eng)


class TEdits(Base):
    def test_first_edit_copies_the_preset(self):
        self.to_row(DRUM.index('DECAY'))
        self.assertEqual(self.ed(), [0] * 32)            # moving through the rows changes nothing
        self.assertEqual(self.turn(5, 3), 1)             # any knob but DATA: the value
        want = list(PRE[L.KICK])
        want[P['DEC']] += 3
        self.assertEqual(self.ed(), [L.KICK + 1] + want)

    def test_data_press_makes_data_change_the_value(self):
        self.to_row(DRUM.index('CUTOFF'))
        self.press()
        self.assertEqual(self.e.run('lxr_menu_accel', stack=[1]) and self.e.d(0), 4)
        self.turn(1, -10)                                # DATA now changes CUTOFF
        self.assertEqual(self.ed()[1 + P['FFREQ']], 117)
        self.press()                                     # ...and moves through the rows again
        self.e.run('lxr_menu_accel', stack=[1])
        self.assertEqual(self.e.d(0), 1)
        self.turn(1, 1)
        self.turn(5, -1)
        self.assertEqual(self.ed()[1 + P['FRESO']], 0)   # (clamped at 0)
        self.assertEqual(self.ed()[1 + P['FFREQ']], 117)

    def test_ranges(self):
        for name, p, top in (('OSC WAVE', 'WAVE', 5), ('FILTER', 'FTYPE', 7), ('MOD MODE', 'MIXMODE', 1),
                             ('TRANSIENT', 'TWAVE', 13), ('DECAY', 'DEC', 127), ('FINE', 'FINE', 127)):
            self.to_row(DRUM.index(name))
            self.turn(7, 500)
            self.assertEqual(self.ed()[1 + P[p]], top, name)
            self.assertEqual(self.turn(7, 1), 0, name)   # at the end: nothing to redraw
            self.turn(7, -500)
            self.assertEqual(self.ed()[1 + P[p]], 0, name)

    def test_every_row_edits_its_own_knob(self):
        for eng, rows in ROWS.items():
            names = {v: k for k, v in P.items()}
            for r, name in enumerate(rows):
                self.e.uc.mem_write(ED, bytes(32))
                self.to_row(r, eng)
                self.turn(5, -200, eng)
                got = self.ed()
                self.assertEqual(got[0], eng + 1)
                changed = [names[i] for i in range(N_P) if got[1 + i] != PRE[eng][i]]
                self.assertLessEqual(len(changed), 1, (eng, name))   # (a knob already at 0 stays)

    def test_put_back_the_preset(self):
        self.to_row(DRUM.index('DECAY'))
        self.turn(5, 9)
        self.turn(1, 100)                                # the last row
        self.assertEqual(self.turn(5, 3), 0)             # a value knob does nothing there
        self.assertEqual(self.press(), 1)
        self.assertEqual(self.ed(), [0] * 32)

    def test_another_presets_edits_give_way(self):
        self.to_row(DRUM.index('DECAY'))
        self.turn(5, 9)                                  # KICK's
        self.turn(5, 2, eng=L.TOM)                       # the first edit on TOM: TOM's knobs
        want = list(PRE[L.TOM])
        want[P['DEC']] += 2
        self.assertEqual(self.ed(), [L.TOM + 1] + want)

    def test_rows_stop_at_the_ends(self):
        self.assertEqual(self.turn(1, -1), 0)
        self.turn(1, 1000)
        self.assertEqual(self.turn(1, 1), 0)
        self.assertEqual(self.turn(1, 0), 0)


class TDraw(Base):
    def setUp(self):
        super().setUp()
        e = self.e
        self.calls = []
        st = Stubs(e)
        st.set(RECT, lambda a: self.calls.append(('rect',) + tuple(x - (1 << 32) if x >> 31 else x for x in a[1:6])))
        st.set(TEXT, lambda a: self.calls.append(('text', a[2], a[3], a[4],
                                                  bytes(e.uc.mem_read(a[6], 32)).split(b'\0')[0].decode())))

    def draw(self, eng, ed=ED):
        e = self.e
        self.calls.clear()
        e.run('lxr_menu_draw', stack=[0x5555, ed, L.contour(eng)])
        return [c[1:] for c in self.calls if c[0] == 'text']

    def selected(self, eng, ed=ED):
        """-> (the name, the value) on the cursor's row"""
        t = self.draw(eng, ed)
        cur = [c[1:] for c in self.calls if c[0] == 'rect'][2]
        y = 55 - (63 - cur[3] + 1)
        row = [x[3] for x in t if x[1] == y]
        return tuple(row) if len(row) == 2 else (row[0], None)

    def test_first_page(self):
        t = self.draw(L.KICK)
        self.assertEqual(t[:2], [(2, 55, 0x10, 'LXR KICK'), (125, 55, 0x14, 'PRESET')])
        rows = t[2:]
        self.assertEqual([r[3] for r in rows], ['OSC WAVE', 'SINE', 'COARSE', '31', 'FINE', '-1', 'MOD WAVE', 'SINE',
                                                'MOD FREQ', '0'])
        self.assertEqual([r[1] for r in rows[::2]], [43, 33, 23, 13, 3])   # 10 px rows under the title
        rects = [c[1:] for c in self.calls if c[0] == 'rect']
        self.assertEqual(rects[0], (0, 0, 127, 63, 0))                       # cleared
        self.assertEqual(rects[1], (0, 53, 127, 53, 1))                      # the line under the title
        self.assertEqual(rects[2], (0, 43, 123, 52, -1))                     # the cursor: the first row lit
        self.assertEqual(rects[3], (126, 42, 127, 51, 1))                    # the scroll bar: rows 1-5 of 25

    def test_scrolls_and_shows_edits(self):
        self.to_row(DRUM.index('DECAY'))
        self.turn(5, 4)
        t = self.draw(L.KICK)
        self.assertEqual(t[1][3], 'EDITED')
        rows = [r[3] for r in t[2:]]
        self.assertEqual(rows[-2:], ['DECAY', '25'])                         # the selected row, last on screen
        rects = [c[1:] for c in self.calls if c[0] == 'rect']
        self.assertEqual(rects[2], (0, 3, 123, 12, -1))
        self.press()                                                         # DATA changes it: the value lit
        self.draw(L.KICK)
        self.assertEqual([c[1:] for c in self.calls if c[0] == 'rect'][2], (72, 3, 123, 12, -1))
        self.assertEqual(self.draw(L.TOM)[1][3], 'PRESET')                  # TOM plays its preset

    def test_value_names(self):
        e = self.e
        def shown(name, p, v, eng=L.KICK):
            e.uc.mem_write(ED, bytes([eng + 1] + PRE[eng]))
            e.w8(ED + 1 + P[p], v)
            self.to_row(ROWS[eng].index(name), eng)
            n, v = self.selected(eng)
            self.assertEqual(n, name)
            return v
        self.assertEqual([shown('FILTER', 'FTYPE', v) for v in range(8)],
                         ['LP', 'HP', 'BP', 'UNITY BP', 'NOTCH', 'PEAK', 'LP2', 'OFF'])
        self.assertEqual([shown('OSC WAVE', 'WAVE', v) for v in range(6)], ['SINE', 'TRI', 'SAW', 'SQUARE', 'NOISE', 'PWM'])
        self.assertEqual([shown('MOD MODE', 'MIXMODE', v) for v in (0, 1)], ['FM', 'MIX'])
        self.assertEqual([shown('TRANSIENT', 'TWAVE', v) for v in (0, 1, 2, 9, 13)],
                         ['SNAP', 'OFFSET', 'EXPCLICK', '808 CLAP', 'MD 16-81'])
        self.assertEqual([shown('FINE', 'FINE', v) for v in (0, 63, 64, 65, 127)], ['-64', '-1', '0', '+1', '+63'])
        self.assertEqual([shown('DECAY', 'DEC', v) for v in (0, 9, 10, 99, 100, 127)], ['0', '9', '10', '99', '100', '127'])

    def test_every_row_fits(self):
        """Every name and its widest value fit side by side; the title and its tag too."""
        widest = {'OSC WAVE': 'SQUARE', 'MOD WAVE': 'SQUARE', 'MOD1 WAVE': 'SQUARE', 'MOD2 WAVE': 'SQUARE',
                  'FILTER': 'UNITY BP', 'TRANSIENT': 'PEDAL CH', 'FINE': '+63', 'MOD MODE': 'MIX'}
        for eng, rows in ROWS.items():
            for name in rows:
                v = widest.get(name, '127')
                self.assertLess(2 + FONT_W * len(name), 72, name)           # left of the edit cursor
                self.assertGreaterEqual(122 - FONT_W * len(v) + 1, 72, (name, v))
            self.assertLess(2 + FONT_W * len(TITLE[eng]), 125 - FONT_W * len('EDITED'))
        self.assertLessEqual(2 + FONT_W * len('PUT BACK THE PRESET'), 125)

    def test_every_voice_lists_its_rows(self):
        for eng, rows in ROWS.items():
            seen = []
            self.e.run('lxr_menu_open')
            for r in range(len(rows) + 1):
                self.to_row(r, eng)
                seen.append(self.selected(eng)[0])
            self.assertEqual(seen, rows + ['PUT BACK THE PRESET'], TITLE[eng])
            self.assertEqual(self.draw(eng)[0][3], TITLE[eng])


class TSound(Base):
    def blocks(self, eng, edits=None, n=200, **kw):
        """n blocks of a fresh voice; edits: the block's bytes (None: no block)"""
        e = self.e
        e.reset()
        if edits is not None:
            e.uc.mem_write(ED, bytes(edits) + bytes(32 - len(edits)))
        L.dials(e, 0, eng, **kw)
        e.run('lxr_params', stack=[0, L.TD, 0 if edits is None else ED])
        return L.play(e, n)

    @staticmethod
    def kick(**knobs):
        k = list(PRE[L.KICK])
        for n, v in knobs.items():
            k[P[n]] = v
        return [L.KICK + 1] + k

    def test_edits_play_on_their_preset_only(self):
        stock = self.blocks(L.KICK)
        self.assertTrue(np.array_equal(self.blocks(L.KICK, self.kick()), stock))   # the preset's knobs: the same
        longer = self.blocks(L.KICK, self.kick(DEC=60))                      # a longer decay
        self.assertGreater(L.rms(longer[4800:]), 1.4 * L.rms(stock[4800:]))
        self.assertTrue(np.array_equal(self.blocks(L.KICK, [L.TOM + 1] + PRE[L.TOM]), stock))  # TOM's: none here
        self.assertTrue(np.array_equal(self.blocks(L.KICK, [0] + PRE[L.TOM]), stock))          # none at all

    def test_macros_move_from_the_edits(self):
        pa = PRE[L.KICK][P['PAMT']]
        a = self.blocks(L.KICK, self.kick(PAMT=pa + 20, GAIN1=30), color=64)
        b = self.blocks(L.KICK, self.kick(GAIN1=30), color=74)               # Color 10 steps up: the same
        self.assertTrue(np.array_equal(a, b))
        self.assertFalse(np.array_equal(b, self.blocks(L.KICK, self.kick(), color=74)))

    def test_an_edit_while_playing_is_heard(self):
        e = self.e
        e.reset()
        e.uc.mem_write(ED, bytes(self.kick()))
        L.dials(e, 0, L.KICK)
        e.run('lxr_params', stack=[0, L.TD, ED])
        L.play(e, 10)
        e.w8(ED + 1 + P['VOL'], 0)                                           # volume 0, mid-note
        self.assertEqual(L.rms(L.play(e, 20, trig=False)[-200:]), 0)

    def test_the_playing_kits_edits(self):
        """lx_params hands lxr_params the playing kit's edits for the track."""
        e = self.e
        T = 2
        e.reset()
        blk = lambda k: self.s['lfo34_kits'] + 364 * k + 172 + 32 * T
        e.uc.mem_write(blk(4), bytes([L.KICK + 1] + PRE[L.KICK]))
        e.w8(blk(4) + 1 + P['VOL'], 0)                                       # kit 4: silent
        for kit, silent in ((4, True), (5, False)):
            e.w32(CUR_KIT, KIT_BASE + kit * KIT_SIZE)
            for i, v in enumerate((64 << 8, 64 << 8, 64 << 8, L.contour(L.KICK))):
                e.w16(L.TD + 22 + 2 * i, v)
            e.run('lx_params', regs={D[2]: T, A[2]: L.TD})
            x = L.play(e, 20, t=T)
            self.assertEqual(L.rms(x) == 0, silent, kit)


class TKits(Base):
    """The edits are each kit's: in its block (lfo2.inc: +172, 32 bytes a track), saved and loaded in its MIDI CC
    file, copied with it, cleared with the track's sound."""
    STO = SCRATCH + 0x10000

    def setUp(self):
        super().setUp()
        e = self.e
        e.w32(CUR_KIT, KIT_BASE + 4 * KIT_SIZE)
        e.uc.mem_write(0x4006186e, bytes.fromhex('4fef00244e75'))    # the default fill: its epilogue (test_lfo2)
        self.files, self.posted = {}, []
        st = Stubs(e)
        st.set(OPEN, self._open)
        st.set(READ, self._read)
        st.set(WRITE, self._write)
        st.set(POST, lambda a: self.posted.append(a[1]))

    def _open(self, a):
        path = bytes(self.e.uc.mem_read(a[0], 32)).split(b'\0')[0].decode()
        mode = {0x40126ce4: 'r', 0x401272db: 'w'}[a[1]]
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

    def kit(self, n):
        return KIT_BASE + n * KIT_SIZE

    def area(self, n):                                   # the file: MIDI CC settings, then LXR's edits
        return self.s['lfo34_kits'] + 364 * n + 124

    def edits(self, n, t):
        return self.area(n) + 48 + 32 * t

    def save(self, n):
        e = self.e
        e.uc.mem_write(self.STO, bytes(7108))
        e.uc.mem_write(0x4005b2b6, bytes.fromhex('70014fef001c4e75'))
        e.run('kit_save_hook', stack=[self.STO, self.kit(n), 0])

    def load(self, n):
        e = self.e
        e.uc.mem_write(0x4005d088, bytes.fromhex('70014fef00244e75'))
        e.run('kit_load_hook', stack=[self.kit(n), self.STO])
        e.run('lcc_poll')

    def test_saved_and_loaded(self):
        e = self.e
        mine = bytes([L.SNARE + 1] + PRE[L.SNARE])
        e.uc.mem_write(self.edits(4, 3), mine)
        want = bytes(e.uc.mem_read(self.area(4), 240))
        self.save(4)
        h = fnv(want)
        self.assertEqual(bytes(e.uc.mem_read(self.STO + 700, 5)), bytes([0x43]) + h.to_bytes(4, 'big'))
        e.run('lcc_write', stack=[0, 0])
        self.assertEqual(self.files, {'/LFOCC/%08X' % h: want})              # 240 bytes: CC and edits
        e.uc.mem_write(self.area(9), b'\x11' * 240)
        self.load(9)
        self.assertEqual(bytes(e.uc.mem_read(self.edits(9, 3), 32)), mine)
        self.assertEqual(bytes(e.uc.mem_read(self.area(9), 240)), want)

    def test_no_edits_the_file_as_before(self):
        """Only MIDI CC settings: the 48-byte file, hashed as before, which loads with no edits."""
        e = self.e
        e.w8(self.area(4), 11)
        cc = bytes(e.uc.mem_read(self.area(4), 48))
        self.save(4)
        e.run('lcc_write', stack=[0, 0])
        self.assertEqual(self.files, {'/LFOCC/%08X' % fnv(cc): cc})
        e.uc.mem_write(self.area(9), b'\x11' * 240)
        self.load(9)
        self.assertEqual(bytes(e.uc.mem_read(self.area(9), 240)), cc + bytes(192))

    def test_wrong_files_load_nothing(self):
        e = self.e
        e.uc.mem_write(self.edits(4, 0), bytes([1] + PRE[0]))
        self.save(4)
        e.run('lcc_write', stack=[0, 0])
        name, good = list(self.files.items())[0]
        for bad in (good[:-1], good[:100], good[:48], good[:-1] + b'\x01'):
            self.files[name] = bad
            e.uc.mem_write(self.area(7), b'\x22' * 240)
            self.load(7)
            self.assertEqual(bytes(e.uc.mem_read(self.area(7), 240)), bytes(240), len(bad))

    def test_copied_with_the_kit_and_cleared_with_the_track(self):
        e = self.e
        mine = bytes([L.CYMB + 1] + PRE[L.CYMB])
        e.uc.mem_write(self.edits(4, 5), mine)
        e.uc.mem_write(self.edits(4, 1), mine)
        e.run('kit_memcpy', stack=[self.kit(30), self.kit(4), KIT_SIZE])
        self.assertEqual(bytes(e.uc.mem_read(self.edits(30, 5), 32)), mine)
        e.run('lfo_init_hook', stack=[self.kit(30) + 28 + 5 * 100, 5, -1])    # track 5's sound to its defaults
        self.assertEqual(bytes(e.uc.mem_read(self.edits(30, 5), 32)), bytes(32))
        self.assertEqual(bytes(e.uc.mem_read(self.edits(30, 1), 32)), mine)   # the others stay
        e.run_to_any('sfx_fxd', (0x40061320,), stack=[self.kit(30) + 628])  # a new kit: none
        self.assertEqual(bytes(e.uc.mem_read(self.edits(30, 1), 32)), bytes(32))


class TOpen(unittest.TestCase):
    """SETTINGS + PRESET (mode_menu_open) on an LXR track opens the voice menu; on a Sampler the playback mode menu
    as before; on anything else nothing."""
    SND = SCRATCH + 0x63000

    def open(self, mach, smp_byte):
        e = self.e = L.emu()
        st = Stubs(e)
        st.set(0x40012412, lambda a: 2)                  # the selected track: 2
        st.set(0x400802e0, lambda a: 0)                  # operator new: none (the page goes no further)
        stub = SCRATCH + 0x64000                         # sound_obj: our sound
        e.uc.mem_write(stub, bytes.fromhex('203c%08x4e75' % self.SND))
        e.uc.mem_write(e.sym['sound_obj'], bytes.fromhex('4ef9%08x' % stub))
        e.w8(self.SND + 38, smp_byte)
        e.w32(e.arr('trk_mach', 2), mach)
        e.w32(0x40fe4178, 1)                             # the view machinery is up
        e.w32(e.sym['mm_kind'], 99)
        e.w32(e.sym['mm_track'], 99)
        e.uc.reg_write(UC_M68K_REG_SR, 0x2000)
        e.run('mode_menu_open', regs={})
        return e.r32(e.sym['mm_kind']), e.r32(e.sym['mm_track'])

    def test_lxr_track(self):
        self.assertEqual(self.open(LX, 13), (MM_LXR, 2))

    def test_sampler_track_as_before(self):
        self.assertEqual(self.open(6, 6), (0, 2))

    def test_other_tracks_nothing(self):
        for m in (0, 7, 11, 12):
            self.assertEqual(self.open(m, m), (99, 99), m)


class TPage(unittest.TestCase):
    """The page's own handlers (mm_enc, mm_key, mm_render) hand LXR's events to the menu, on the playing kit's
    edits for mm_track."""
    T = 3

    def setUp(self):
        e = self.e = L.emu()
        self.s = e.sym
        e.w32(CUR_KIT, KIT_BASE + 6 * KIT_SIZE)
        e.w32(e.sym['mm_kind'], MM_LXR)
        e.w32(e.sym['mm_track'], self.T)
        e.w32(e.arr('lx_cont', self.T), L.contour(L.SNARE))
        e.run('lxr_menu_open')
        self.clicks, self.redraws, self.texts = 0, [], []
        self.code = 32
        st = Stubs(e)
        st.set(0x4006f73a, lambda a: self.clicks)            # the encoder's clicks
        st.set(0x40076082, lambda a: self.redraws.append(a[0]))
        st.set(0x4007240c, lambda a: self.code)              # the key's code
        st.set(0x40072434, lambda a: 1)                      # a click
        st.set(TEXT, lambda a: self.texts.append(bytes(e.uc.mem_read(a[6], 32)).split(b'\0')[0].decode()))
        st.set(RECT, lambda a: 0)

    def edits(self):
        return list(self.e.uc.mem_read(self.s['lfo34_kits'] + 364 * 6 + 172 + 32 * self.T, 32))

    def knob(self, k, clicks):
        EV = SCRATCH + 0x9000
        self.e.w32(EV + 12, k)
        self.clicks = clicks & M32
        self.e.run('mm_enc', stack=[0x7000, EV])

    def test_knobs_edit_the_playing_kits_track(self):
        self.knob(1, SNARE.index('NOISE MIX'))               # DATA: to NOISE MIX
        self.assertEqual(self.edits(), [0] * 32)
        self.knob(9, -5)                                     # another knob: its value
        want = [L.SNARE + 1] + PRE[L.SNARE]
        want[1 + P['MIX']] -= 5
        self.assertEqual(self.edits(), want)
        self.assertEqual(self.redraws, [0x7000, 0x7000])

    def test_data_press(self):
        EV = SCRATCH + 0x9000
        self.e.w32(EV + 16, 0x11)
        self.knob(1, 100)                                    # the last row: put back
        self.knob(9, 1)
        self.knob(1, -1)
        self.knob(9, 1)                                      # VOLUME + 1
        self.assertNotEqual(self.edits(), [0] * 32)
        self.knob(1, 1)
        self.e.run('mm_key', stack=[0x7000, EV])             # DATA's press there
        self.assertEqual(self.e.d(0), 1)                     # used
        self.assertEqual(self.edits(), [0] * 32)

    def test_render(self):
        self.e.run('mm_render', stack=[0x7000, 0x5555])
        self.assertEqual(self.texts[:2], ['LXR SNARE', 'PRESET'])
        self.assertIn('NOISE FREQ', self.texts)


if __name__ == '__main__':
    unittest.main()
