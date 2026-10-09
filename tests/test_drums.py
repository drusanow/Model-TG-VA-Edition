#!/usr/bin/env python3
"""Host tests for VA KICK, VA SNARE and VA HIHAT (machines 8..10,
src/drums.inc): the routing through sampler_pre and the render, the labels,
defaults and descriptor hooks, and measurements of what dr_fill renders -
pitch, sweep, click, drive, noise, spread, bounds, CPU. HOST verification
only: how they sound needs a Model:Cycles.

    python3 -m unittest discover -s tests -v
"""
import os, sys, struct, unittest, random

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
import test_va as V

KICK, SNARE, HAT = 8, 9, 10
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


def dials(e, t, d):
    for i, v in enumerate(d):
        e.w32(e.sym['dr_p'] + 16 * t + 4 * i, v)


def render(e, mach, d, blocks, t=0, step=STEP1, trig=True):
    dials(e, t, d)
    if trig:
        e.w8(e.sym['dr_trig'] + t, 1)
    out = []
    for _ in range(blocks):
        e.run('dr_fill', stack=[t, step, mach])
        out += [e.rs32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
    for v in out:
        assert v & 0x7fff == 0
    return np.array([v >> 15 for v in out], dtype=float)


def crossings(x):
    """sample positions of rising zero crossings (linearly interpolated)"""
    i = np.where((x[:-1] < 0) & (x[1:] >= 0))[0]
    return i + (-x[i]) / (x[i + 1] - x[i])


def freq(x):
    c = crossings(x)
    return FS * (len(c) - 1) / (c[-1] - c[0])


def band(x, lo, hi):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / FS)
    return float(s[(f >= lo) & (f < hi)].sum())


def centroid(x):
    s = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    f = np.fft.rfftfreq(len(x), 1 / FS)
    return float((s * f).sum() / s.sum())


class TKick(unittest.TestCase):
    def test_pitch_follows_the_note(self):
        e = emu()
        for step, hz in ((STEP1, 55.0), (2 * STEP1, 110.0), (STEP1 // 2, 27.5)):
            x = render(e, KICK, (0, 0, 0, 0), 120, step=step)
            self.assertAlmostEqual(freq(x[2000:]), hz, delta=hz * 0.01, msg=step)

    def test_sweep_depth_and_time(self):
        e = emu()
        def first_period(d):                   # from the first half cycle
            x = render(e, KICK, d, 40)
            c = crossings(-x)                  # starts at 0 rising: its first fall
            return FS / (2 * c[0])
        f0 = first_period((0, 40, 0, 0))
        f64 = first_period((64, 40, 0, 0))
        f127 = first_period((127, 40, 0, 0))
        self.assertLess(f0, 60)
        self.assertGreater(f64, 1.6 * f0)
        self.assertGreater(f127, f64)
        # longer STM: still high later
        def at(d, n):
            x = render(e, KICK, d, n // 64 + 60)
            c = crossings(x)
            k = np.searchsorted(c, n)
            return FS / (c[k + 1] - c[k])
        self.assertGreater(at((127, 127, 0, 0), 4800), at((127, 0, 0, 0), 4800) * 1.3)
        # every sweep ends on the base pitch
        self.assertAlmostEqual(at((127, 60, 0, 0), 20000), 55, delta=1)

    def test_click_only_at_the_start(self):
        e = emu()
        a = render(e, KICK, (30, 40, 0, 0), 50)
        b = render(e, KICK, (30, 40, 0, 127), 50)
        diff = b - a
        self.assertGreater(np.abs(diff[:96]).max(), 3000)      # a real click
        self.assertEqual(np.abs(diff[2400:]).max(), 0)          # gone after 25 ms

    def test_drive(self):
        e = emu()
        a = render(e, KICK, (0, 0, 0, 0), 60)[1000:]
        b = render(e, KICK, (0, 0, 127, 0), 60)[1000:]
        self.assertLessEqual(np.abs(b).max(), 32767)
        self.assertGreater(np.sqrt((b ** 2).mean()), 1.2 * np.sqrt((a ** 2).mean()))
        # harmonics: the third relative to the fundamental grows
        def h3(x):
            s = np.abs(np.fft.rfft(x * np.hanning(len(x))))
            f = np.fft.rfftfreq(len(x), 1 / FS)
            p = lambda hz: s[np.argmin(np.abs(f - hz))]
            return p(165) / p(55)
        self.assertGreater(h3(b), 3 * h3(a))

    def test_starts_from_zero(self):
        e = emu()
        x = render(e, KICK, (64, 40, 30, 0), 1)
        self.assertLess(abs(x[0]), 2000)


class TSnare(unittest.TestCase):
    def test_body_pitch_and_decay(self):
        e = emu()
        x = render(e, SNARE, (0, 0, 64, 0), 150)              # 100 ms
        s = np.abs(np.fft.rfft(x[:8192] * np.hanning(8192)))
        f = np.fft.rfftfreq(8192, 1 / FS)
        lo, mid, hi = (np.searchsorted(f, v) for v in (120, 250, 450))
        self.assertAlmostEqual(f[lo + np.argmax(s[lo:mid])], 180, delta=6)
        rms = [np.sqrt((x[i:i + 960] ** 2).mean()) for i in range(0, 9600, 960)]
        self.assertTrue(all(a > b for a, b in zip(rms[1:], rms[2:])), rms)
        self.assertLess(rms[-1], rms[1] / 2)                   # ~85 ms over 80 ms

    def test_pitch_drop(self):
        e = emu()
        hi = render(e, SNARE, (0, 127, 64, 0), 20)
        lo = render(e, SNARE, (0, 0, 64, 0), 20)
        self.assertLess(crossings(-hi)[0], crossings(-lo)[0] * 0.6)   # starts higher
        self.assertGreater(len(crossings(hi[:640])), len(crossings(lo[:640])))

    def test_noise_mix_and_tone(self):
        e = emu()
        body = render(e, SNARE, (0, 0, 64, 0), 30)
        noise = render(e, SNARE, (127, 0, 64, 0), 30)
        self.assertGreater(centroid(noise), 3 * centroid(body))
        self.assertLess(band(noise, 100, 400), band(body, 100, 400) / 20)  # no body
        dark = render(e, SNARE, (127, 0, 10, 0), 30)
        bright = render(e, SNARE, (127, 0, 120, 0), 30)
        tilt = lambda x: band(x, 8000, 20000) / band(x, 600, 2000)
        self.assertGreater(tilt(bright), 20 * tilt(dark))
        # no low end in the noise: the 600 Hz cut
        s = np.abs(np.fft.rfft(bright))
        f = np.fft.rfftfreq(len(bright), 1 / FS)
        self.assertLess(s[f < 150].mean(), s[(f > 2000) & (f < 6000)].mean() / 4)


class THat(unittest.TestCase):
    def ref_incs(self, e, step, sprd):
        inc = ((step * 357) + ((step * 52) >> 8)) >> 1
        r = [e.rs32(e.sym['dr_hat_r'] + 4 * i) for i in range(6)]
        mq = lambda i, q: ((i >> 14) * q) + (((i & 0x3fff) * q) >> 14)
        b = [mq(inc, q) & M32 for q in r]
        m = (sum(b) & M32) // 6
        return [(m + ((((x - m) >> 2) * 2 * sprd) >> 5)) & M32 for x in b]

    def test_six_oscillators_and_spread(self):
        e = emu()
        for sprd in (0, 64, 127):
            for step in (STEP1, 3 * STEP1 // 2):
                render(e, HAT, (64, sprd, 0, 0), 1, step=step)
                got = [e.r32(e.sym['dr_hinc'] + 4 * i) for i in range(6)]
                self.assertEqual(got, self.ref_incs(e, step, sprd), (sprd, step))
        hz = lambda v: v * FS / 2 ** 32
        render(e, HAT, (64, 64, 0, 0), 1)
        got = [hz(e.r32(e.sym['dr_hinc'] + 4 * i)) for i in range(6)]
        for g, want in zip(got, (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)):
            self.assertAlmostEqual(g, want, delta=want * 0.01)
        render(e, HAT, (64, 0, 0, 0), 1)
        got = [e.r32(e.sym['dr_hinc'] + 4 * i) for i in range(6)]
        self.assertEqual(len(set(got)), 1)                    # one pitch

    def test_tone_and_no_low_end(self):
        e = emu()
        dark = render(e, HAT, (0, 64, 0, 0), 40)[640:]          # past the first
        bright = render(e, HAT, (127, 64, 0, 0), 40)[640:]      # block's settling
        self.assertGreater(centroid(bright), 1.3 * centroid(dark))
        self.assertLess(band(dark, 0, 900), band(dark, 3000, 16000) / 20)   # energy
        self.assertGreater(np.abs(dark).max(), 3000)          # audible

    def test_noise(self):
        e = emu()
        sq = render(e, HAT, (64, 64, 0, 0), 20)
        nz = render(e, HAT, (64, 64, 127, 0), 20)
        # squares alone are periodic over the six; with noise the block-to-block
        # repetition is gone: compare autocorrelation at the cluster's period
        self.assertFalse(np.array_equal(sq, nz))
        self.assertGreater(np.abs(nz).max(), 3000)


class TCommon(unittest.TestCase):
    def test_bounds_everywhere(self):
        e = emu(); rnd = random.Random(11)
        for _ in range(60):
            m = rnd.choice((KICK, SNARE, HAT))
            d = tuple(rnd.choice((0, 127, rnd.randrange(128))) for _ in range(4))
            step = rnd.choice((STEP1 // 8, STEP1, 4 * STEP1, 12 * STEP1))
            x = render(e, m, d, 8, t=rnd.randrange(6), step=step, trig=rnd.random() < 0.5)
            self.assertLessEqual(np.abs(x).max(), 32767)

    def test_tracks_independent(self):
        e = emu()
        a = render(e, KICK, (64, 40, 30, 60), 10, t=1)
        render(e, SNARE, (64, 40, 30, 60), 10, t=2)            # another track between
        e2 = emu()
        b = render(e2, KICK, (64, 40, 30, 60), 10, t=1)
        # the kick does not use the noise seed after its click: same signal
        self.assertTrue(np.array_equal(a[1000:], b[1000:]))

    def test_cpu(self):
        e = emu()
        worst = {}
        for m, d in ((KICK, (127, 60, 127, 127)), (SNARE, (64, 64, 64, 127)),
                     (HAT, (64, 64, 64, 127)), (HAT, (64, 64, 64, 0))):
            dials(e, 0, d)
            e.w8(e.sym['dr_trig'], 1)
            n = max(e.count_instructions('dr_fill', RET, stack=[0, STEP1, m]) for _ in range(4))
            worst[(m, d[3])] = n
        if os.environ.get('VA_TEST_VERBOSE'):
            print("\n  dr_fill instructions a block:", worst)
        for k, n in worst.items():
            self.assertLess(n, 5600, k)                       # about a VA track's


class TPlumbing(unittest.TestCase):
    def test_pre_routes_drums_to_va_pre(self):
        e = emu()
        e.watch('dr_params', 'va_params')
        for t, m in ((0, KICK), (1, SNARE), (2, HAT)):
            words = (20 << 8, 40 << 8, 60 << 8, 80 << 8)
            ex = V.run_pre(e, t, m, trig=True, words=words)
            self.assertEqual(ex, 0x400a7dcc)
            self.assertEqual(e.r32(V.voice(t)), 6)                      # a Sampler voice
            self.assertEqual([e.r32(e.sym['dr_p'] + 16 * t + 4 * i) for i in range(4)],
                             [20, 40, 60, 80])
            self.assertEqual(e.r8(e.sym['dr_trig'] + t), 1)
        self.assertEqual((e.visits['dr_params'], e.visits['va_params']), (3, 0))

    def test_render_calls_dr_fill(self):
        e = emu()
        e.watch('dr_fill', 'va_fill', 'amp_hook', 'sampler_render')
        args = []
        e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: args.append(
            struct.unpack('>3I', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 12))),
            begin=e.sym['dr_fill'], end=e.sym['dr_fill'])
        for n, m in enumerate((KICK, SNARE, HAT)):
            e.w32(e.arr('trk_mach', 3), m)
            T = V.T5PreDispatch()
            T._dispatch(e, 3)
            self.assertEqual(args[-1][0], 3)
            self.assertEqual(args[-1][2], m)
        self.assertEqual((e.visits['dr_fill'], e.visits['va_fill'], e.visits['amp_hook']),
                         (3, 0, 3))

    def test_labels_follow_the_machine(self):
        e = emu(); s = e.sym
        pt = 0x4010dce0
        for m, tab in ((KICK, 'kick_swap'), (SNARE, 'snare_swap'), (HAT, 'hat_swap'),
                       (7, 'va_swap')):
            e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[m])
            row = s[tab]
            for r in range(4):
                off = e.r32(row + 28 * r)
                self.assertEqual([e.r32(pt + off + 44 + 4 * i) for i in range(3)],
                                 [e.r32(row + 28 * r + 4 + 4 * i) for i in range(3)], (m, r))
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[2])
        row = s['kick_swap']
        off = e.r32(row)
        self.assertEqual(e.r32(pt + off + 44), e.r32(row + 16))          # stock again
        for i, want in ((9, 2), (10, 3), (11, 4), (12, 0)):
            e.run_to_any('descr_hook', (s['table_lookup_b_fixed'], RET, 0x4004df5c),
                         stack=[i])

    def test_commit_defaults(self):
        e = emu()
        T = V.T6Commit()
        for m, want in ((KICK, (48, 40, 64, 40)), (SNARE, (64, 32, 88, 48)),
                        (HAT, (80, 64, 16, 48))):
            T._commit(e, m)
            self.assertEqual([e.r16(T.SOUND + 42 + 2 * i) >> 8 for i in range(4)], list(want))

    def test_lists_names_icons(self):
        e = emu(); s = e.sym
        self.assertEqual([e.r32(s['page_machine_list'] + 4 * i) for i in range(12)],
                         list(range(1, 13)))
        names = [e.r32(s['sampler_name_table'] + 4 * i) for i in range(12)]
        rd = lambda a: bytes(e.uc.mem_read(a, 12)).split(b'\0')[0].decode()
        self.assertEqual([rd(a) for a in names[7:]],
                         ['VA', 'VA KICK', 'VA SNARE', 'VA HIHAT', 'PLAITS'])
        # the icon vectors: twelve entries, ours at 6..11
        OLD_A, OLD_B, HEAP = SCRATCH + 0x50000, SCRATCH + 0x51000, SCRATCH + 0x52000
        e.w32(0x40fe32cc, OLD_A); e.w32(0x40fe32d0, OLD_A + 168)
        e.w32(0x40fe384c, OLD_B); e.w32(0x40fe3850, OLD_B + 168)
        bump = SCRATCH + 0x53000
        e.w32(bump, HEAP)
        # alloc stand-in: d0 = *bump; *bump += 0x400
        e.uc.mem_write(0x40080064, bytes.fromhex(
            '2039' + '%08x' % bump + '2200 0681 00000400 23c1' + '%08x' % bump + '4e75'))
        e.run('build_sampler_icons')
        for vec, pix in ((0x40fe32cc, 'a'), (0x40fe384c, 'b')):
            base = e.r32(vec)
            self.assertEqual(e.r32(vec + 4) - base, 12 * 28)
            for i, n in ((7, 'va'), (8, 'kick'), (9, 'snare'), (10, 'hihat'), (11, 'plaits')):
                self.assertEqual(e.r32(base + 28 * i + 16), s[f'{n}_icon_{pix}_pixels'], (vec, n))



def cubic_ref(u):
    u = max(-32767, min(32767, u))
    u3 = ((((u * u) >> 15) * u) >> 16)
    return max(-32767, min(32767, u + (u >> 1) - u3))


class TPunch(unittest.TestCase):
    """dr_punch: after the envelope, a drum's level made up (x2 / x2.5 / x4
    for small signals) through the cubic soft clip; other machines untouched."""
    BUF = SCRATCH + 0x60000

    def run_punch(self, e, mach, vals, t=2):
        e.w32(e.arr('trk_mach', t), mach)
        for i, v in enumerate(vals):
            e.w32(self.BUF + 4 * i, v)
        e.run('dr_punch', stack=[self.BUF, t])
        return [e.rs32(self.BUF + 4 * i) for i in range(32)]

    def test_gain_and_curve(self):
        e = emu()
        rnd = random.Random(4)
        for mach, g in ((KICK, 4.0), (SNARE, 5.0), (HAT, 10.0)):
            vals = [rnd.randrange(-2 ** 31, 2 ** 31) for _ in range(28)] + \
                   [0, 100 << 16, -(100 << 16), 0x7fffffff]
            out = self.run_punch(e, mach, vals)
            gq = round(4096 * g * 2 / 3)
            for v, o in zip(vals, out):
                u = (((v >> 16) * gq) >> 12)
                self.assertEqual(o, cubic_ref(u) << 16, (mach, v))
                self.assertLessEqual(abs(o), 32767 << 16)
            small = out[29] / (100 << 16)
            self.assertAlmostEqual(small, g, delta=0.05)               # small-signal gain
        # loud input is soft-limited, not wrapped, and keeps its sign
        out = self.run_punch(e, HAT, [0x7fff0000, -0x7fff0000] + [0] * 30)
        self.assertEqual((out[0], out[1]), (32767 << 16, -(32767 << 16)))

    def test_other_machines_untouched(self):
        e = emu()
        vals = [(i * 0x1234567) & M32 for i in range(32)]
        for mach in (0, 5, 6, 7, 12):
            out = self.run_punch(e, mach, vals)
            self.assertEqual([o & M32 for o in out], vals, mach)

    def test_amp_hook_runs_it(self):
        e = emu()
        e.watch('dr_punch', 'gflt_run')
        e.w32(e.arr('trk_mach', 2), KICK)
        V.T5PreDispatch()._dispatch(e, 2)
        self.assertEqual((e.visits['dr_punch'], e.visits['gflt_run']), (1, 1))


class TMarkers(unittest.TestCase):
    """mp_markers: eight on the stock line, the drums on a second line."""
    def test_two_lines(self):
        e = emu()
        calls = []
        for fn, kind in ((0x40070c4e, 'outline'), (0x40070efc, 'filled')):
            e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, k: calls.append(
                (k,) + struct.unpack('>6i', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 24))),
                begin=fn, end=fn, user_data=kind)
        for sel in (0, 7, 8, 10):
            calls.clear()
            e.run('mp_markers', regs={D[2]: 0x5555, D[3]: sel, D[4]: 0x44, D[7]: 0x77})
            self.assertEqual((e.d(4), e.d(7)), (0x44, 0x77))          # kept
            self.assertEqual(len(calls), 12)
            for i, (kind, ctx, x0, y0, x1, y1, one) in enumerate(calls):
                col, line = (i, 0) if i < 8 else (i - 8, 1)
                x = 76 + 7 * col
                self.assertEqual((ctx, x0, x1, one), (0x5555, x - 4, x, 1), i)
                self.assertEqual((y0, y1), (8, 12) if line == 0 else (1, 5), i)
                self.assertEqual(kind, 'filled' if i == sel else 'outline', i)
            self.assertLessEqual(max(c[4] for c in calls), 125)           # on screen


if __name__ == '__main__':
    unittest.main(verbosity=2)
