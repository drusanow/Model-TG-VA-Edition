#!/usr/bin/env python3
"""Host tests for LXR (machine 13, src/lxr/lxr.c, GPL-3.0-only; glue src/lxr_glue.inc): the Erica Synths LXR-02's
voices, ported. The firmware's own code runs (Unicorn) and is checked against the LXR's own code: tests/data/
lxr_ref.json holds the original's envelope, spectrum and pitch for each voice of its init kit (tools/lxr_reference.py
renders them from the LXR firmware's sources). Also: the presets, the macros, the clap's repeats, the hats' decays,
bounds, the 48 kHz pairs, the page's labels and names, cost. How it sounds on a Model:Cycles needs one.

    python3 -m unittest tests.test_lxr -v
"""
import json, os, sys, random, struct, unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

LX = 13
STEP1 = 0x10000                                   # note 60
KICK, TOM, PERC, SNARE, CLAP, CHAT, OHAT, CYMB = range(8)
SNAR = SNARE
NAMES = ['KICK', 'TOM', 'PERC', 'SNAR', 'CLAP', 'CHAT', 'OHAT', 'CYMB']   # 4 letters, as PLAITS'
TD = SCRATCH + 0x60000                            # a stand-in trackData
REF = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data', 'lxr_ref.json')))
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


def contour(eng):
    return (eng * 16 + 8) << 8                    # the middle of its sixteen values


def dials(e, t, eng, color=64, shape=64, sweep=64):
    for i, v in enumerate((color << 8, shape << 8, sweep << 8, contour(eng))):
        e.w16(TD + 22 + 2 * i, v)
    e.run('lxr_params', stack=[t, TD])


def play(e, blocks, t=0, step=STEP1, trig=True, eng=None, **kw):
    if eng is not None:
        dials(e, t, eng, **kw)
    if trig:
        e.w8(e.sym['dr_trig'] + t, 1)
    out = []
    for _ in range(blocks):
        e.run('lxr_fill', stack=[t, step, LX])
        blk = [e.rs32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
        assert all(v & 0x7fff == 0 for v in blk)
        assert blk[0::2] == blk[1::2]             # 48 kHz: each sample twice
        out += blk[0::2]
    return np.array([v >> 15 for v in out], dtype=float)


def rms(x):
    return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0


def peak_hz(x, lo, hi):
    y = (x - x.mean()) * np.hanning(len(x))
    s = np.abs(np.fft.rfft(y, 1 << 18))
    f = np.fft.rfftfreq(1 << 18, 1 / 48000)
    m = (f > lo) & (f < hi)
    return float(f[np.flatnonzero(m)[np.argmax(s[m])]])


def zc_rate(x):
    """zero crossings a second (48 kHz)"""
    s = np.signbit(x[:-1]) != np.signbit(x[1:])
    return s.sum() * 48000 / 2 / len(x)


class TReference(unittest.TestCase):
    """the machine's voices against the LXR's own (its init kit, every macro at 64: the preset as it is)"""
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
        from lxr_reference import features
        cls.got = {}
        for name in REF['voices']:
            e = emu()
            x = play(e, 1500, eng=NAMES.index(name))
            cls.got[name] = features(x, 48000)

    def test_envelopes(self):
        """within a fraction of a dB; the hats within 2 dB: their energy is mostly 16..22 kHz, the top of the LXR's
        band, which going from its 44.14 kHz to our 48 kHz trims by a few dB (to 20 kHz: lxr_reference.features)"""
        for name, ref in REF['voices'].items():
            g = np.array(self.got[name]['env']); r = np.array(ref['env'])
            live = r > r.max() - 50
            d = np.abs(g - r)[live]
            self.assertLess(np.median(d), 2.0 if name in ('CHAT', 'OHAT') else 0.8, name)
            self.assertLess(d.max(), 3.0, name)

    def test_spectra(self):
        """every band holding more than 1/1000 of the voice's energy, within 3 dB"""
        for name, ref in REF['voices'].items():
            for i, (g, r) in enumerate(zip(self.got[name]['bands'], ref['bands'])):
                if r > -30:
                    self.assertLess(abs(g - r), 3.0, (name, REF['bands'][i], g, r))

    def test_pitch(self):
        for name, ref in REF['voices'].items():
            for g, r in zip(self.got[name].get('f0', []), ref.get('f0', [])):
                if r:
                    self.assertAlmostEqual(g / r, 1.0, delta=0.03, msg=name)


class TSound(unittest.TestCase):
    def test_silent_until_triggered(self):
        e = emu()
        x = play(e, 20, trig=False, eng=KICK)
        self.assertFalse(x.any())

    def test_every_preset_sounds_and_differs(self):
        outs = []
        for eng in range(8):
            e = emu()
            x = play(e, 300, eng=eng)
            self.assertGreater(rms(x[:9600]), 300, NAMES[eng])
            self.assertLessEqual(np.abs(x).max(), 32767)
            outs.append(x)
        for a in range(8):
            for b in range(a + 1, 8):
                if {a, b} != {CHAT, OHAT}:          # the same hat; their decays differ (test_hats_...)
                    self.assertLess(abs(np.corrcoef(outs[a], outs[b])[0, 1]), 0.9, (NAMES[a], NAMES[b]))

    def test_pitch_follows_the_note(self):
        rates = []
        for step in (STEP1, STEP1 * 2):           # note 60, note 72
            e = emu()
            x = play(e, 200, step=step, eng=TOM)
            rates.append(peak_hz(x[2400:7200], 40, 1000))
        self.assertAlmostEqual(rates[1] / rates[0], 2.0, delta=0.04)

    def test_kick_macros(self):
        def zc(color, shape, lo, hi):
            e = emu()
            x = play(e, 60, eng=KICK, color=color, shape=shape)
            return zc_rate(x[lo:hi])
        self.assertGreater(zc(127, 64, 0, 480), 1.3 * zc(0, 64, 0, 480))      # Color: more pitch sweep
        self.assertGreater(zc(64, 127, 960, 1920), zc(64, 0, 960, 1920))       # Shape: a longer sweep

    def test_every_macro_changes_every_preset(self):
        for eng in range(8):
            for knob in ('color', 'shape', 'sweep'):
                xs = []
                for v in (0, 127):
                    e = emu()
                    xs.append(play(e, 200, eng=eng, **{knob: v}))
                self.assertGreater(rms(xs[0] - xs[1]), 100, (NAMES[eng], knob))

    def test_clap_repeats(self):
        def onsets(n_knob):
            e = emu()
            x = np.abs(play(e, 400, eng=CLAP, sweep=n_knob))
            env = np.array([np.sqrt(np.mean(x[i:i + 96] ** 2)) for i in range(0, len(x) - 96, 96)])
            return sum(1 for i in range(1, len(env)) if env[i] > 1.6 * env[i - 1] + 50)
        self.assertGreaterEqual(onsets(127) - onsets(0), 3)

    def test_hats_closed_shorter_than_open(self):
        tails = []
        for eng in (CHAT, OHAT, CYMB):
            e = emu()
            x = play(e, 1500, eng=eng)
            tails.append(rms(x[9600:12000]))      # 200..250 ms
        self.assertLess(tails[0] * 10, tails[1])
        self.assertLess(tails[1], tails[2])

    def test_engine_change_starts_from_rest(self):
        e = emu()
        play(e, 30, eng=OHAT)
        x = play(e, 5, trig=False, eng=KICK)      # no new trig: the new preset is silent
        self.assertFalse(x.any())

    def test_bounds_random(self):
        rnd = random.Random(3)
        e = emu()
        for _ in range(60):
            eng = rnd.randrange(8)
            x = play(e, 8, t=rnd.randrange(6), step=rnd.randrange(0x2000, 0x80000), eng=eng,
                     color=rnd.randrange(128), shape=rnd.randrange(128), sweep=rnd.randrange(128),
                     trig=rnd.random() < 0.5)
            self.assertLessEqual(np.abs(x).max(), 32767)

    def test_tracks_independent(self):
        e = emu()
        a = play(e, 60, t=0, eng=SNARE)
        e = emu()
        play(e, 1, t=3, eng=CYMB)
        b = play(e, 60, t=0, eng=SNARE)
        self.assertTrue(np.array_equal(a, b))

    def test_goes_idle(self):
        e = emu()
        play(e, 1500, eng=CHAT)                   # long after the closed hat
        n = e.count_instructions('lxr_fill', RET, stack=[0, STEP1, LX])
        self.assertLess(n, 300)


class TPage(unittest.TestCase):
    def _names(self, e):
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        return [rd(e.r32(0x4010dce0 + off + 44)) for off in (2576, 3472, 4088, 4144)]

    def test_labels_follow_the_engine_group(self):
        e = emu()
        e.uc.mem_write(0x400cf866, bytes.fromhex('70004e75'))     # stand-ins: the selected track is 2
        e.uc.mem_write(0x4000eb90, bytes.fromhex('70004e75'))
        e.uc.mem_write(0x40012412, bytes.fromhex('70024e75'))
        e.w32(e.arr('trk_mach', 2), LX)
        want = {KICK: ['Pitch Amt', 'Pitch Dec', 'FM Amount', 'Engine'],
                SNARE: ['Noise Mix', 'NoiseTone', 'Snap', 'Engine'],
                CLAP: ['Spread', 'Tone', 'Claps', 'Engine'],
                OHAT: ['Tune', 'Metal', 'Bright', 'Engine'],
                CYMB: ['Tune', 'Ring', 'Bright', 'Engine']}
        for eng, names in want.items():
            e.w16(TD + 28, contour(eng))
            e.run('lx_params', regs={D[2]: 2, A[2]: TD})
            e.run('lx_watch')
            self.assertEqual(self._names(e), names, NAMES[eng])

    def test_engine_names(self):
        e = emu()
        buf = SCRATCH + 0x61000
        for eng in range(8):
            for v in (contour(eng) - (7 << 8), contour(eng) + (7 << 8)):
                e.run('lx_eng_fmt', 0x40000e6e, stack=[0, v, buf])
                sp = e.uc.reg_read(UC_M68K_REG_A7)
                name = e.r32(sp + 12)
                self.assertEqual(bytes(e.uc.mem_read(name, 8)).split(b'\0')[0].decode(), NAMES[eng])


    def test_knob_screen_is_plaits_own(self):
        """Contour's screen: the engine's picture on the left, eight pips centred on x 96 (the
        engine's filled), its 4-letter name in the big font centred on 96 - as PLAITS'."""
        e = emu(); s = e.sym
        calls = []
        def hook(fn, kind, n):
            e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: calls.append(
                (kind,) + struct.unpack('>%di' % n, uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 4 * n))),
                begin=fn, end=fn)
        hook(0x40070efc, 'fill', 6); hook(0x40070c4e, 'box', 6)
        hook(0x40071da4, 'icon', 5); hook(0x40071c10, 'text', 9)
        e.uc.mem_write(0x40072102, bytes.fromhex('70284e75'))   # width: 40
        e.uc.mem_write(0x400720f8, bytes.fromhex('70074e75'))   # the font's handle: 7
        rd = lambda a: bytes(e.uc.mem_read(a, 8)).split(b'\0')[0].decode()
        for eng in range(8):
            calls.clear()
            e.run('lx_draw', stack=[0x1000, contour(eng), 0x5555, 0x60, 0x22, 0])
            fills = [c for c in calls if c[0] == 'fill']
            self.assertEqual(fills[0][1:], (0x5555, 14, 1, 50, 37, 0))      # black behind
            self.assertEqual(fills[1][1:], (0x5555, 70, 22, 122, 28, 0))    # stock pips off
            icon = [c for c in calls if c[0] == 'icon'][0]
            self.assertEqual(icon[3:], (32, 19, 1))
            self.assertEqual(e.r32(icon[2] + 16), s[f'lx_ic_{NAMES[eng].lower()}'])
            pips = [c for c in calls if c[0] in ('box', 'fill')][2:]
            self.assertEqual(len(pips), 8)
            for i, p in enumerate(pips):                 # 77 .. 115: centred on 96
                self.assertEqual(p[1:], (0x5555, 77 + 5 * i, 0x17, 80 + 5 * i, 0x1b, 1))
                self.assertEqual(p[0], 'fill' if i == eng else 'box', (eng, i))
            text = [c for c in calls if c[0] == 'text'][0]
            self.assertEqual(text[1:5], (0x5555, 7, 96 - 20, 0x22))          # big font, centred on 96
            self.assertEqual(rd(text[9] & M32), NAMES[eng])
        for n in NAMES:                                  # the names fit as PLAITS' do
            self.assertLessEqual(len(n), 4)

    def test_knob_screen_installed_for_lxr(self):
        e = emu(); s = e.sym
        DRW = 0x40a73460
        stock = (0x11111111, 0x22222222, 0, 0x44444444)
        for i, v in enumerate(stock):
            e.w32(DRW + 4 * i, v)
        for i, v in enumerate((0xa1, 0xa2, 0xa3)):
            e.w32(0x40a727e0 + 4 * i, v)                 # Pitch's words
        e.run('mi_fmtswap', regs={D[1]: 8})
        self.assertEqual([e.r32(DRW + 4 * i) for i in range(4)], [0xa1, 0xa2, 0xa3, s['lx_draw']])
        self.assertEqual(e.r32(0x40a7345c), s['lx_eng_fmt'])
        e.run('mi_fmtswap', regs={D[1]: 6})              # PLAITS' after it
        self.assertEqual(e.r32(DRW + 12), s['mi_draw'])
        e.run('mi_fmtswap', regs={D[1]: 0})              # anything else: stock again
        self.assertEqual([e.r32(DRW + 4 * i) for i in range(4)], list(stock))

    def test_machine_page_picture_follows_the_engine(self):
        e = emu(); s = e.sym
        TB = SCRATCH + 0x70000                           # table B: 14 entries
        e.w32(0x40fe384c, TB); e.w32(0x40fe3850, TB + 392)
        e.uc.mem_write(0x400cf866, bytes.fromhex('70004e75'))
        e.uc.mem_write(0x4000eb90, bytes.fromhex('70004e75'))
        e.uc.mem_write(0x40012412, bytes.fromhex('70024e75'))   # the selected track: 2
        e.w32(e.arr('trk_mach', 2), LX)
        for eng in (CYMB, KICK, SNARE, CHAT, CLAP, TOM, OHAT, PERC):
            e.w16(TD + 28, contour(eng))
            e.run('lx_params', regs={D[2]: 2, A[2]: TD})
            e.run('lx_watch')
            self.assertEqual(e.r32(TB + 364 + 16), s[f'lx_ic_{NAMES[eng].lower()}'], eng)
        e.w32(e.arr('trk_mach', 2), 7)                   # not an LXR: its own picture
        e.run('lx_watch')
        self.assertEqual(e.r32(TB + 364 + 16), s['lxr_icon_b_pixels'])


class TCost(unittest.TestCase):
    def test_cpu(self):
        worst = {}
        for eng in range(8):
            e = emu()
            dials(e, 0, eng, 127, 127, 127)
            e.w8(e.sym['dr_trig'], 1)
            first = e.count_instructions('lxr_fill', RET, stack=[0, STEP1, LX])     # its curves made
            n = max(e.count_instructions('lxr_fill', RET, stack=[0, STEP1, LX]) for _ in range(6))
            worst[NAMES[eng]] = (first, n)
        if os.environ.get('VA_TEST_VERBOSE'):
            print("\n  lxr_fill instructions a block (first, then):", worst)
        self.assertLess(max(w[1] for w in worst.values()), 11000, worst)     # the hats are the heaviest
        self.assertLess(max(w[0] for w in worst.values()), 12500, worst)


if __name__ == '__main__':
    unittest.main(verbosity=2)
