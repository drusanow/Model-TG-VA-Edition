#!/usr/bin/env python3
"""Host tests for the CPU guard (src/guard.inc) and the LXR Rate option.

The guard: over GD_HI percent of a block, the new machines' oldest sounding note (at least GD_AGE blocks old) fades
out (gv_gain, amp_hook) and goes idle (voice_quiet) until its next trig; one at most every GD_COOL blocks; never a
stock machine or the Sampler; OFF does nothing. Device Config's two rows turn it and LXR Rate. LXR at 48 kHz: the
same pitches and lengths, cheaper.

    python3 -m unittest tests.test_guard -v
"""
import os, sys, struct, unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
from test_lfocc import Stubs
import test_lxr as L

GD_HI, GD_AGE, GD_COOL = 90, 24, 12
VA, DRUM, PLAITS, DX, LXR = 7, 8, 11, 12, 13
VOICE = SCRATCH + 0x30000                         # six stand-in voices, 0x400 apart
OUT = SCRATCH + 0x38000


class Base(unittest.TestCase):
    def setUp(self):
        self.e = e = L.emu()
        self.s = e.sym
        e.w32(self.s['gd_on'], 1)
        e.w32(self.s['gd_mask'], 0)
        e.w32(self.s['gd_cool'], 0)
        e.w32(self.s['gd_count'], 0)
        for t in range(6):
            e.w32(e.arr('trk_mach', t), 0)
            e.w8(self.s['sv_idle'] + t, 1)
            e.w32(e.arr('gv_gain', t), 32768)
            e.w32(e.arr('gd_age', t), 0)

    def sounding(self, t, mach, age):
        e = self.e
        e.w32(e.arr('trk_mach', t), mach)
        e.w8(self.s['sv_idle'] + t, 0)
        e.w32(e.arr('gd_age', t), age)

    def block(self, load):
        self.e.run('gd_block', regs={D[0]: load})
        self.assertEqual(self.e.d(0), load)           # kept for prof_end

    def mask(self):
        return self.e.r32(self.s['gd_mask'])


class TChoose(Base):
    def test_under_the_line_nothing(self):
        self.sounding(2, LXR, 500)
        for load in (0, 50, GD_HI):
            self.block(load)
        self.assertEqual(self.mask(), 0)
        self.assertEqual(self.e.r32(self.e.arr('gd_age', 2)), 503)   # every track a block older

    def test_the_oldest_new_note_goes(self):
        self.sounding(0, 3, 900)                      # a stock machine: never
        self.sounding(1, 6, 800)                      # the Sampler: never
        self.sounding(2, LXR, 300)
        self.sounding(3, DX, 400)
        self.sounding(4, PLAITS, 100)
        self.e.w32(self.e.arr('trk_mach', 5), VA)     # silent: never
        self.e.w32(self.e.arr('gd_age', 5), 5000)
        self.block(95)
        self.assertEqual(self.mask(), 1 << 3)
        self.assertEqual(self.e.r32(self.s['gd_count']), 1)
        self.assertEqual(self.e.r32(self.s['gd_cool']), GD_COOL)

    def test_one_at_a_time(self):
        for t, age in ((1, 300), (2, 200), (4, 100)):
            self.sounding(t, LXR, age)
        self.block(99)
        for _ in range(GD_COOL):
            self.block(99)                            # measured again first
        self.assertEqual(self.mask(), 1 << 1)
        self.block(99)
        self.assertEqual(self.mask(), (1 << 1) | (1 << 2))
        self.assertEqual(self.e.r32(self.s['gd_count']), 2)

    def test_fresh_notes_stay(self):
        self.sounding(2, LXR, GD_AGE - 2)             # (a block older inside gd_block: GD_AGE - 1)
        self.block(99)
        self.assertEqual(self.mask(), 0)
        self.block(99)                                # now GD_AGE
        self.assertEqual(self.mask(), 1 << 2)

    def test_muted_tracks_not_taken(self):
        self.sounding(2, LXR, 500)
        self.e.w32(self.e.arr('gv_gain', 2), 0)
        self.block(99)
        self.assertEqual(self.mask(), 0)

    def test_off_does_nothing(self):
        self.sounding(2, LXR, 500)
        self.e.w32(self.s['gd_on'], 0)
        self.block(100)
        self.assertEqual(self.mask(), 0)

    def test_every_new_machine_can_go(self):
        for m in range(VA, LXR + 1):
            self.setUp()
            self.sounding(0, m, 100)
            self.block(99)
            self.assertEqual(self.mask(), 1, m)


class TNote(Base):
    """An ended note: faded by amp_hook, idle by voice_quiet, back on its next trig."""
    def voice(self, t):
        v = VOICE + 0x400 * t
        self.e.uc.mem_write(v, bytes(0x400))
        self.e.w32(self.e.arr('voice_ptr', t), v)
        return v

    def quiet(self, t):
        self.e.run('voice_quiet', regs={A[0]: VOICE + 0x400 * t, D[1]: t})
        return self.e.d(0)

    def amp(self, t, x=0x40000000):
        e = self.e
        e.uc.mem_write(OUT, struct.pack('>32i', *[x] * 32))
        e.run('amp_hook', stack=[OUT, VOICE + 0x400 * t])
        return struct.unpack('>32i', e.uc.mem_read(OUT, 128))

    def test_fades_then_idles_then_comes_back(self):
        e = self.e
        T = 2
        v = self.voice(T)
        self.sounding(T, LXR, 500)
        e.w8(self.s['trig_seen'] + T, 1)
        e.w32(v + 0x298, 3)                           # the stock envelope: still sounding
        e.w32(v + 0x294, 0x7fffffff)
        e.w32(self.s['gd_mask'], 1 << T)
        y = self.amp(T)                               # the fade: 64 samples down, over two blocks
        self.assertLess(y[-1], y[0])
        self.assertTrue(all(a >= b for a, b in zip(y, y[1:])))
        y = self.amp(T)
        self.assertEqual(y[-1], 0)
        self.assertEqual(e.r32(e.arr('gv_gain', T)), 0)
        self.assertEqual(self.quiet(T), 1)            # idle: nothing renders
        e.w32(v + 0x34, 1)                            # its next trig
        self.assertEqual(self.quiet(T), 0)
        self.assertEqual(self.mask(), 0)
        self.assertEqual(e.r32(e.arr('gv_gain', T)), 32768)   # full level at once
        self.assertEqual(e.r32(e.arr('gd_age', T)), 0)
        self.assertEqual(self.amp(T)[0], self.amp(T)[31])

    def test_a_trig_keeps_a_hand_mute(self):
        e = self.e
        T = 4
        v = self.voice(T)
        e.w32(self.s['gd_mask'], 1 << T)
        e.w32(self.s['mute_sil'], 1 << T)
        e.w32(e.arr('gv_gain', T), 0)
        e.w32(v + 0x34, 1)
        self.quiet(T)
        self.assertEqual(self.mask(), 0)
        self.assertEqual(e.r32(e.arr('gv_gain', T)), 0)
        e.w32(self.s['mute_sil'], 0)


class TRows(Base):
    """Device Config: CPU Guard (OFF / ON) and LXR Rate (44.1K / 48K)."""
    def turn(self, fn, delta):
        e = self.e
        clo = SCRATCH + 0x3f000                       # closure -> data -> 0 (no menu to redraw)
        e.w32(clo, clo + 8)
        e.w32(clo + 8, 0)
        e.run(fn, stack=[clo, 0, delta & M32])

    def test_turns(self):
        e = self.e
        for fn, var in (('gd_rot', 'gd_on'), ('lr_rot', 'lx_r48')):
            self.turn(fn, -1)
            self.assertEqual(e.r32(self.s[var]), 0, fn)
            self.turn(fn, 3)
            self.assertEqual(e.r32(self.s[var]), 1, fn)
            self.turn(fn, 0)
            self.assertEqual(e.r32(self.s[var]), 1, fn)
            self.turn(fn, -2)
            self.assertEqual(e.r32(self.s[var]), 0, fn)

    def test_defaults(self):
        e = L.emu()
        self.assertEqual((e.r32(e.sym['gd_on']), e.r32(e.sym['lx_r48'])), (1, 0))   # every boot: ON, the LXR's rate

    def test_added_to_device_config(self):
        e = self.e
        rows = []
        st = Stubs(e)
        st.set(e.sym['lm_add_row'], lambda a: rows.append(a[1]))
        e.uc.mem_write(0x40032af4, bytes.fromhex('4e75'))
        e.run_to_any('cfg_hook', (0x40032af4,))
        s = self.s
        self.assertEqual(rows, [s['id_row'], s['sys_row'], s['qm_row'], s['gd_row'], s['lr_row']])

    def test_system_page_shows_it(self):
        e = self.e
        texts = []
        st = Stubs(e)
        st.set(0x40071a04, lambda a: texts.append((a[2], bytes(e.uc.mem_read(a[6], 16)).split(b'\0')[0].decode())))
        st.set(0x40070dea, lambda a: 0)
        e.w32(self.s['sys_pg'], 0)
        for n in (0, 7, 10, 305, 99999, 123456):
            texts.clear()
            e.w32(self.s['gd_count'], n)
            e.run('sys_render', regs={D[2]: 0x5555})
            self.assertIn((36, 'GUARD %d' % min(n, 99999)), texts)
        texts.clear()
        e.w32(self.s['gd_on'], 0)
        e.run('sys_render', regs={D[2]: 0x5555})
        self.assertIn((36, 'GUARD OFF'), texts)


class TRate(unittest.TestCase):
    """LXR at 48 kHz: the same pitches and lengths, no resampling, cheaper."""
    def play(self, eng, r48, n=600):
        e = L.emu()
        e.w32(e.sym['lx_r48'], r48)
        return L.play(e, n, eng=eng)

    def test_same_pitch_and_length(self):
        for eng in (L.KICK, L.TOM, L.SNARE, L.CLAP, L.CYMB):
            a, b = self.play(eng, 0), self.play(eng, 1)
            fa, fb = L.peak_hz(a[:4800], 30, 2000), L.peak_hz(b[:4800], 30, 2000)
            self.assertLess(abs(fb / fa - 1), 0.015, L.NAMES[eng])
            ea = np.array([L.rms(a[i:i + 480]) for i in range(0, len(a) - 480, 480)])
            eb = np.array([L.rms(b[i:i + 480]) for i in range(0, len(b) - 480, 480)])
            m = (ea > ea.max() * 0.01) | (eb > eb.max() * 0.01)
            self.assertLess(np.median(np.abs(20 * np.log10((eb[m] + 1) / (ea[m] + 1)))), 0.6, L.NAMES[eng])

    def test_every_preset_sounds_bounded(self):
        for eng in range(8):
            x = self.play(eng, 1, 200)
            self.assertGreater(L.rms(x), 100, L.NAMES[eng])
            self.assertLessEqual(np.max(np.abs(x)), 32767)

    def test_switching_mid_note(self):
        e = L.emu()
        x = L.play(e, 20, eng=L.CYMB)
        e.w32(e.sym['lx_r48'], 1)
        y = L.play(e, 20, trig=False)
        e.w32(e.sym['lx_r48'], 0)
        z = L.play(e, 20, trig=False)
        self.assertGreater(L.rms(y), 100)
        self.assertGreater(L.rms(z), 100)

    def test_cheaper(self):
        for eng in range(8):
            cost = []
            for r48 in (0, 1):
                e = L.emu()
                e.w32(e.sym['lx_r48'], r48)
                L.dials(e, 0, eng, 127, 127, 127)
                e.w8(e.sym['dr_trig'], 1)
                e.count_instructions('lxr_fill', RET, stack=[0, L.STEP1, L.LX])
                cost.append(max(e.count_instructions('lxr_fill', RET, stack=[0, L.STEP1, L.LX]) for _ in range(6)))
            if os.environ.get('VA_TEST_VERBOSE'):
                print('\n  %s: %d at 44.14 kHz, %d at 48 kHz' % (L.NAMES[eng], *cost))
            self.assertLess(cost[1], 0.88 * cost[0], L.NAMES[eng])


if __name__ == '__main__':
    unittest.main()
