#!/usr/bin/env python3
"""Host tests for PLAITS (machine 11, src/plaits.inc): its eight engines -
pitch, what each knob does, bounds, CPU - and the plumbing it shares with the
drums: the routing through sampler_pre and the render, the labels (per
engine), the defaults, the level stage. HOST verification only: how it sounds
needs a Model:Cycles.

    python3 -m unittest discover -s tests -v
"""
import os, sys, struct, unittest, random

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
import test_va as V

MI = 11
WSHP, FM, GRAN, PD, CHIP, NOIS, PART, STRG = range(8)
FS = 96000
STEP1 = 0x10000                                   # note 60
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


def dials(e, t, eng, h, tb, m):
    for i, v in enumerate((h, tb, m, eng * 4096 + 2048)):
        e.w32(e.sym['mi_p'] + 16 * t + 4 * i, v)


def render(e, eng, h, tb, m, blocks, t=0, step=STEP1, trig=True):
    dials(e, t, eng, h, tb, m)
    if trig:
        e.w8(e.sym['dr_trig'] + t, 1)
    out = []
    for _ in range(blocks):
        e.run('mi_fill', stack=[t, step, MI])
        out += [e.rs32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
    for v in out:
        assert v & 0x7fff == 0
    return np.array([v >> 15 for v in out], dtype=float)


def spectrum(x):
    x = (x - x.mean()) * np.hanning(len(x))
    s = np.abs(np.fft.rfft(x, 1 << 18))
    return np.fft.rfftfreq(1 << 18, 1 / FS), s


def peak_hz(x, lo, hi):
    f, s = spectrum(x)
    m = (f > lo) & (f < hi)
    return float(f[m][np.argmax(s[m])])


def cents(a, b):
    return 1200 * np.log2(a / b)


def centroid(x):
    f, s = spectrum(x)
    return float((s * f).sum() / s.sum())


def band(x, lo, hi):
    f, s = spectrum(x)
    return float((s[(f >= lo) & (f < hi)] ** 2).sum())


class TEngines(unittest.TestCase):
    def test_waveshaping(self):
        e = emu()
        x = render(e, WSHP, 16384, 0, 0, 60)[1000:]
        self.assertLess(abs(cents(peak_hz(x, 150, 400), 261.63)), 3)
        dull = centroid(render(e, WSHP, 16384, 0, 0, 40)[1000:])
        mid = centroid(render(e, WSHP, 16384, 16384, 0, 40)[1000:])
        bright = centroid(render(e, WSHP, 16384, 32767, 0, 40)[1000:])
        self.assertLess(dull, mid)                              # TIMB folds
        self.assertLess(mid, bright)
        a = render(e, WSHP, 0, 8000, 0, 40)[1000:]              # HARM: another shaper
        b = render(e, WSHP, 32767, 8000, 0, 40)[1000:]
        self.assertGreater(np.abs(a - b).max(), 3000)
        c = render(e, WSHP, 16384, 8000, 32767, 40)[1000:]      # MORP: the slope
        d = render(e, WSHP, 16384, 8000, 0, 40)[1000:]
        self.assertGreater(band(c, 400, 1000), 2 * band(d, 400, 1000))

    def test_fm(self):
        e = emu()
        x = render(e, FM, 16384, 0, 16384, 60)[1000:]           # index 0: a sine
        f, s = spectrum(x)
        k = np.argmax(s)
        self.assertLess(abs(cents(f[k], 261.63)), 3)
        self.assertLess(s[(f > 700) & (f < 20000)].max(), s[k] * 1e-3)   # nothing else, -60 dB
        self.assertGreater(np.abs(x).max(), 30000)
        lo = centroid(render(e, FM, 16384, 8000, 16384, 40)[1000:])
        hi = centroid(render(e, FM, 16384, 24000, 16384, 40)[1000:])
        self.assertLess(lo, hi)                                 # TIMB: the index
        # HARM at ratio 2 (quantizer entry 60..62): partials on the harmonics
        x = render(e, FM, 61 * 256 + 128, 16000, 16384, 60)[1000:]
        f, s = spectrum(x)
        pk = (s[1:-1] > s[:-2]) & (s[1:-1] >= s[2:]) & (s[1:-1] > s.max() * 0.05)
        strong = f[1:-1][pk & (f[1:-1] > 100)]
        h = strong / 261.63
        self.assertTrue(np.all(np.abs(h - np.round(h)) < 0.02), strong)
        # MORP: feedback either way changes the sound
        a = render(e, FM, 16384, 16000, 0, 30)[1000:]
        b = render(e, FM, 16384, 16000, 32767, 30)[1000:]
        self.assertGreater(np.abs(a - b).max(), 5000)

    def test_grain(self):
        e = emu()
        x = render(e, GRAN, 16384, 8000, 8000, 80)[1000:]
        self.assertLess(abs(cents(peak_hz(x, 150, 400), 261.63)), 5)
        lo = centroid(render(e, GRAN, 20000, 4000, 8000, 40)[1000:])
        hi = centroid(render(e, GRAN, 20000, 24000, 8000, 40)[1000:])
        self.assertLess(2 * lo, hi)                             # TIMB: the formant
        a = render(e, GRAN, 20000, 12000, 0, 40)[1000:]
        b = render(e, GRAN, 20000, 12000, 32767, 40)[1000:]
        self.assertGreater(np.abs(a - b).max(), 2000)           # MORP: the window
        self.assertLess(abs(x.mean()), 300)                     # DC blocked

    def test_phase_distortion(self):
        e = emu()
        x = render(e, PD, 16384, 0, 0, 60)[1000:]               # amount 0: a cosine
        f, s = spectrum(x)
        self.assertLess(abs(cents(f[np.argmax(s)], 261.63)), 3)
        self.assertLess(s[(f > 700) & (f < 20000)].max(), s.max() * 1e-3)
        lo = centroid(render(e, PD, 16384, 8000, 0, 40)[1000:])
        hi = centroid(render(e, PD, 16384, 24000, 0, 40)[1000:])
        self.assertLess(lo, hi)
        x = render(e, PD, 16384, 16000, 20000, 60)[1000:]       # still the note
        self.assertLess(abs(cents(peak_hz(x, 150, 400), 261.63)), 3)

    def test_chiptune_arpeggio(self):
        e = emu()
        st = e.sym['mi_st']
        # chord 10 (M: 0 4 7 12, three notes walked), pattern 0 (up, 1 octave)
        h = 10 * 32768 // 11 + 100
        seq = []
        for _ in range(7):
            render(e, CHIP, h, 0, 16384, 1)
            seq.append(e.r32(st + 8))
        self.assertEqual(seq, [1, 2, 0, 1, 2, 0, 1])
        ratios = [e.r16(e.sym['mi_chord'] + 2 * (40 + i)) for i in range(4)]
        self.assertEqual(ratios[:3], [4096, round(4096 * 2 ** (4 / 12)), round(4096 * 2 ** (7 / 12))])
        # pattern 4 (down, 2 octaves): octaves 1 then 0, notes descending
        e = emu()
        tb = 4 * 32768 // 12 + 100
        seq = []
        for _ in range(7):
            render(e, CHIP, h, tb, 16384, 1)
            seq.append((e.r32(st + 12), e.r32(st + 8)))
        self.assertEqual(seq, [(1, 2), (1, 1), (1, 0), (0, 2), (0, 1), (0, 0), (1, 2)])
        # the square follows the note: MORP 0.5 = unsynced, the master's pitch
        e = emu()
        x = render(e, CHIP, 0, 0, 16465, 60, trig=False)[500:]
        self.assertLess(abs(cents(peak_hz(x, 150, 400), 261.63)), 3)
        self.assertEqual(set(np.abs(x).astype(int)), {24000})

    def test_chiptune_up_down_and_random(self):
        e = emu()
        st = e.sym['mi_st']
        h = 4 * 32768 // 11 + 100                               # m7: four notes
        tb = 6 * 32768 // 12 + 100                              # up-down, 1 octave
        seq = []
        for _ in range(9):
            render(e, CHIP, h, tb, 16384, 1)
            seq.append(e.r32(st + 8))
        self.assertEqual(seq, [1, 2, 3, 2, 1, 0, 1, 2, 3])
        tb = 11 * 32768 // 12 + 100                             # random, 4 octaves
        prev = None
        for _ in range(20):
            render(e, CHIP, h, tb, 16384, 1)
            cur = (e.r32(st + 12), e.r32(st + 8))
            self.assertTrue(0 <= cur[0] < 4 and 0 <= cur[1] < 4, cur)
            self.assertNotEqual(cur, prev)
            prev = cur

    def test_noise(self):
        e = emu()
        lp = render(e, NOIS, 0, 28000, 8000, 60)[1000:]
        hp = render(e, NOIS, 32767, 28000, 8000, 60)[1000:]
        self.assertGreater(band(lp, 20, 500) / band(lp, 4000, 20000),
                           10 * band(hp, 20, 500) / band(hp, 4000, 20000))
        q = render(e, NOIS, 16384, 28000, 30000, 80)[1000:]     # resonant band-pass
        self.assertLess(abs(cents(peak_hz(q, 100, 2000), 261.63)), 30)
        slow = render(e, NOIS, 0, 4000, 0, 30)
        self.assertLess(np.abs(np.diff(slow)).mean(), np.abs(np.diff(lp)).mean())

    def test_particles(self):
        e = emu()
        sparse = render(e, PART, 8000, 4000, 24000, 80)
        dense = render(e, PART, 8000, 28000, 24000, 80)
        self.assertGreater(np.abs(dense).max(), 2000)
        self.assertGreater((dense ** 2).sum(), (sparse ** 2).sum())
        self.assertLess(abs(dense[1000:].mean()), 0.05 * np.abs(dense).max() + 50)
        # resonant: the energy sits about the note with no spread
        x = render(e, PART, 0, 28000, 30000, 120)[1000:]
        self.assertLess(abs(cents(peak_hz(x, 100, 2000), 261.63)), 50)

    def test_string_in_tune(self):
        for h, tb, m in ((8200, 0, 0), (8200, 10000, 20000), (8200, 30000, 30000),
                         (30000, 16000, 25000), (0, 16000, 25000)):
            for step in (STEP1 // 4, STEP1 // 2, STEP1, 2 * STEP1):
                e = emu()
                x = render(e, STRG, h, tb, m, 120, step=step)
                hz = 261.63 * step / STEP1
                y = x[int(FS / hz * 2):]
                self.assertLess(abs(cents(peak_hz(y, hz * 0.7, hz * 1.4), hz)), 20,
                                (h, tb, m, step))

    def test_string_decay_and_brightness(self):
        def tail(m, tb=16000):
            e = emu()
            x = render(e, STRG, 8200, tb, m, 120)
            return np.sqrt((x[-2000:] ** 2).mean()) / np.sqrt((x[1000:3000] ** 2).mean())
        self.assertLess(tail(4000), tail(20000))
        self.assertGreater(tail(32767), 0.8)                    # MORP at the top: rings
        e = emu()
        dull = centroid(render(e, STRG, 8200, 4000, 20000, 60)[500:])
        e = emu()
        bright = centroid(render(e, STRG, 8200, 30000, 20000, 60)[500:])
        self.assertLess(2 * dull, bright)

    def test_string_owns_its_line(self):
        e = emu()
        s = e.sym
        line = s.get('DLY_BASE', 0x4e7f0000) + 2 * 0x2000
        for i in range(0, 0x2000, 4):
            e.w32(line + i, 0x12345)                          # Pluck's leftovers
        e.w8(s['pk_init'] + 2, 1)
        render(e, STRG, 8200, 16000, 20000, 1, t=2, trig=False)
        self.assertEqual(e.r8(s['pk_init'] + 2), 0)             # Pluck re-zeroes later
        self.assertEqual(e.r8(s['mi_dz'] + 2), 1)
        words = [e.rs32(line + 4 * i) for i in range(2048)]
        self.assertTrue(all(abs(w) < 2 for w in words))

    def test_engine_change_starts_from_rest(self):
        e = emu()
        render(e, FM, 16384, 20000, 20000, 10, t=4)
        st = e.sym['mi_st'] + 128 * 4
        self.assertNotEqual(e.r32(st), 0)
        render(e, WSHP, 16384, 0, 0, 1, t=4, trig=False)
        self.assertEqual(e.r32(e.sym['mi_eng'] + 16), WSHP)

    def test_bounds_everywhere(self):
        e = emu(); rnd = random.Random(7)
        for _ in range(48):
            eng = rnd.randrange(8)
            p = [rnd.choice((0, 32767, rnd.randrange(32768))) for _ in range(3)]
            step = rnd.choice((STEP1 // 8, STEP1, 4 * STEP1, 12 * STEP1))
            x = render(e, eng, *p, 6, t=rnd.randrange(6), step=step, trig=rnd.random() < 0.5)
            self.assertLessEqual(np.abs(x).max(), 32767)

    def test_cpu(self):
        worst = {}
        for eng in range(8):
            for p in ((32767, 32767, 32767), (16384, 16384, 16384), (0, 0, 0), (9000, 32767, 20000)):
                e = emu()
                dials(e, 0, eng, *p)
                e.w8(e.sym['dr_trig'], 1)
                e.count_instructions('mi_fill', RET, stack=[0, STEP1, MI])   # the first block
                n = max(e.count_instructions('mi_fill', RET, stack=[0, STEP1, MI])
                        for _ in range(3))
                worst[eng] = max(worst.get(eng, 0), n)
        if os.environ.get('VA_TEST_VERBOSE'):
            print("\n  mi_fill instructions a block:", worst)
        for eng, n in worst.items():
            self.assertLess(n, 7200, eng)


class TPlumbing(unittest.TestCase):
    def test_pre_routes_plaits(self):
        e = emu()
        e.watch('mi_params', 'dr_params', 'va_params')
        words = (20 << 8, 40 << 8, 60 << 8, 80 << 8)
        ex = V.run_pre(e, 3, MI, trig=True, words=words)
        self.assertEqual(ex, 0x400a7dcc)
        self.assertEqual(e.r32(V.voice(3)), 6)                          # a Sampler voice
        self.assertEqual([e.r32(e.sym['mi_p'] + 48 + 4 * i) for i in range(4)],
                         [w for w in words])
        self.assertEqual(e.r8(e.sym['dr_trig'] + 3), 1)
        self.assertEqual((e.visits['mi_params'], e.visits['dr_params'], e.visits['va_params']),
                         (1, 0, 0))

    def test_render_calls_mi_fill(self):
        e = emu()
        e.watch('mi_fill', 'dr_fill', 'va_fill', 'amp_hook')
        args = []
        e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: args.append(
            struct.unpack('>3I', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 12))),
            begin=e.sym['mi_fill'], end=e.sym['mi_fill'])
        e.w32(e.arr('trk_mach', 1), MI)
        V.T5PreDispatch()._dispatch(e, 1)
        self.assertEqual((args[-1][0], args[-1][2]), (1, MI))
        self.assertEqual((e.visits['mi_fill'], e.visits['dr_fill'], e.visits['va_fill'],
                          e.visits['amp_hook']), (1, 0, 0, 1))

    def test_labels_per_engine(self):
        e = emu(); s = e.sym
        pt = 0x4010dce0
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        shorts = []
        for eng in range(8):
            e.w32(s['mi_lab'], eng)
            e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[MI])
            names = [rd(e.r32(pt + off + 44)) for off in (2576, 3472, 4088, 4144)]
            self.assertTrue(all(1 <= len(x) <= 9 for x in names), names)   # fit their box
            cats = {rd(e.r32(pt + off + 48)) for off in (2576, 3472, 4088, 4144)}
            shorts.append([rd(e.r32(pt + off + 52)) for off in (2576, 3472, 4088, 4144)])
            self.assertEqual(names[3], 'Engine')
            self.assertEqual(cats, {'Plaits'})
        self.assertEqual({x[3] for x in shorts}, {'ENG'})       # the value names it
        self.assertEqual(shorts[1][:3], ['RATI', 'INDX', 'FDBK'])
        self.assertEqual(shorts[7][:3], ['STIF', 'BRIG', 'DAMP'])
        for row in shorts:
            self.assertTrue(all(1 <= len(x) <= 4 for x in row), row)
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[2])
        self.assertEqual(e.r32(pt + 2576 + 44), 0x4012999c)            # stock again

    def test_watch_follows_the_engine(self):
        e = emu(); s = e.sym
        pt = 0x4010dce0
        e.w32(e.arr('trk_mach', 2), MI)
        dials(e, 2, PD, 0, 0, 0)
        # the selected-track lookup (stock, stubbed by rts) returns d0 as left:
        # stand in for it with "moveq #2,d0 ; rts"
        e.uc.mem_write(0x40012412, bytes.fromhex('70024e75'))
        e.run('mi_watch')
        self.assertEqual(e.r32(s['mi_lab']), PD)
        rd = lambda a: bytes(e.uc.mem_read(a, 8)).split(b'\0')[0].decode()
        self.assertEqual(rd(e.r32(pt + 3472 + 52)), 'DIST')
        dials(e, 2, STRG, 0, 0, 0)
        e.run('mi_watch')
        self.assertEqual(rd(e.r32(pt + 3472 + 52)), 'BRIG')
        e.w32(e.arr('trk_mach', 2), 3)                          # not a PLAITS
        e.run('mi_watch')
        self.assertEqual(e.r32(s['mi_lab']), M32)

    def test_contour_value_names_the_engine(self):
        e = emu(); s = e.sym
        REC = 0x40a7345c                                        # id 0x4a's formatter
        e.w32(REC, 0x400456c8)
        e.w32(s['mi_lab'], FM)
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[MI])
        self.assertEqual(e.r32(REC), s['mi_eng_fmt'])
        rd = lambda a: bytes(e.uc.mem_read(a, 8)).split(b'\0')[0].decode()
        names = []
        for v in (0, 2048, 4095, 4096, 12288, 20480, 30720, 32512, 32767):
            e.run('mi_eng_fmt', 0x40000e6e, stack=[0x1000, v, 0x2000])
            sp = e.sp()
            self.assertEqual((e.r32(sp + 4), e.r32(sp + 8)), (0x2000, 0x40124b58), v)  # buf, "%s"
            names.append(rd(e.r32(sp + 12)))
        self.assertEqual(names, ['WSHP', 'WSHP', 'WSHP', 'FM', 'PD', 'NOIS', 'STRG', 'STRG', 'STRG'])
        for m in (2, 6, 7):                                     # any other machine: stock
            e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[m])
            self.assertEqual(e.r32(REC), 0x400456c8, m)
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[MI])
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[MI])
        self.assertEqual(e.r32(s['mi_fmt0']), 0x400456c8)       # never saves its own

    def test_contour_steps_one_engine_a_click(self):
        e = emu()
        HANDLE, TRK, VT, SNDO = SCRATCH + 0xb000, SCRATCH + 0xb100, SCRATCH + 0xb200, SCRATCH + 0xb400
        STUB = SCRATCH + 0xb300
        # 0x4000c264 stand-in: d0 = current + delta, clamped 0..32512
        e.uc.mem_write(0x4000c264, bytes.fromhex(
            '202f000c d0af0010 6c027000 0c8000007f00 6f06 203c00007f00 4e75'.replace(' ', '')))
        e.w32(HANDLE + 4, TRK)
        e.w32(TRK, VT)
        e.w32(VT + 40, STUB)
        e.uc.mem_write(STUB, bytes.fromhex('203c' + '%08x' % SNDO + '4e75'))
        def step(mach, cur, delta):
            e.w8(SNDO + 38, mach)
            e.run('fine_hook', stack=[HANDLE, 0x4a, cur, delta])
            return e.d(0)
        mid = lambda k: 4096 * k + 2048
        self.assertEqual(step(MI, mid(0), 256), mid(1))
        self.assertEqual(step(MI, mid(1), 4096), mid(2))        # a fast turn: still one
        self.assertEqual(step(MI, 5000, -256), mid(0))          # 5000 is FM: back to WSHP
        self.assertEqual(step(MI, mid(7), 256), mid(7))         # held at the ends
        self.assertEqual(step(MI, mid(0), -2048), mid(0))
        self.assertEqual(step(MI, 0, 256), mid(1))
        self.assertEqual(step(MI, 32512, -256), mid(6))
        self.assertEqual(step(4, 1000, 256), 1256)              # Chord: the stock step
        self.assertEqual(step(6, 1000, -256), 744)              # the Sampler too

    def test_machine_page_picture_follows_the_engine(self):
        e = emu(); s = e.sym
        TB = SCRATCH + 0x70000                                  # table B, rebuilt: 12 entries
        e.w32(0x40fe384c, TB); e.w32(0x40fe3850, TB + 336)
        e.w32(e.arr('trk_mach', 2), MI)
        e.uc.mem_write(0x40012412, bytes.fromhex('70024e75'))   # the selected track: 2
        names = ('wshp', 'fm', 'gran', 'pd', 'chip', 'nois', 'part', 'strg')
        for eng in (FM, STRG, WSHP, NOIS, PD, CHIP, PART, GRAN):
            dials(e, 2, eng, 0, 0, 0)
            e.run('mi_watch')
            self.assertEqual(e.r32(TB + 308 + 16), s[f'mi_ic_{names[eng]}'], eng)
        e.w32(e.arr('trk_mach', 2), 7)                          # not a PLAITS: its own
        e.run('mi_watch')
        self.assertEqual(e.r32(TB + 308 + 16), s['plaits_icon_b_pixels'])
        e.w32(0x40fe3850, TB + 168)                             # stock six: untouched
        e.w32(TB + 308 + 16, 0x1234)
        e.w32(e.arr('trk_mach', 2), MI)
        e.run('mi_watch')
        self.assertEqual(e.r32(TB + 308 + 16), 0x1234)
        for n in names:                                         # 34 x 34, as the others
            self.assertEqual(s[f'mi_ic_{n}'] % 2, 0)

    def test_commit_defaults(self):
        e = emu()
        T = V.T6Commit()
        T._commit(e, MI)
        self.assertEqual([e.r16(T.SOUND + 42 + 2 * i) >> 8 for i in range(4)], [64, 64, 64, 0])

    def test_level_x2(self):
        e = emu()
        BUF = SCRATCH + 0x60000
        vals = [0, 1 << 16, -(1 << 16), 0x20000000, -0x20000000, 0x3fffffff, 0x7fffffff,
                -0x80000000] + [(i * 0x1234567) - 0x10000000 for i in range(24)]
        e.w32(e.arr('trk_mach', 2), MI)
        for i, v in enumerate(vals):
            e.w32(BUF + 4 * i, v)
        e.run('dr_punch', stack=[BUF, 2])
        out = [e.rs32(BUF + 4 * i) for i in range(32)]
        want = [2 * max(-0x3fffffff, min(0x3fffffff, s32(v))) for v in vals]
        self.assertEqual(out, want)

    def test_gates(self):
        e = emu()
        ids = [e.r32(e.sym['sampler_lfo_ids'] + 4 * i) for i in range(4)]
        for idx in range(11, 15):                               # LFO: the four dials
            e.run('sampler_lfo_gate', 0x4005a6fc, regs={D[2]: MI, A[2]: idx})
            self.assertEqual(e.d(0), ids[idx - 11])
        e.run('sampler_amp_gate', 0x4005a6fc, regs={D[2]: MI})
        self.assertEqual(e.d(0), 0x4b)


if __name__ == '__main__':
    unittest.main(verbosity=2)
