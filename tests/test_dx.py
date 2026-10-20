#!/usr/bin/env python3
"""Host tests for DX (machine 12, src/dx.inc): a 6-operator FM voice after
the DX7, as Dexed's engine (msfa) models it - the 32 algorithms against the
DX7's diagrams, frequencies, levels, envelopes, feedback, the LFO, the three
macros, the voice names, bounds and CPU. HOST verification only: how it
sounds needs a Model:Cycles.

    python3 -m unittest discover -s tests -v
"""
import os, sys, random, struct, unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
from va_emu import *  # noqa
import gen_dx_tables as G

DX = 12
FS = 96000
STEP1 = 0x10000                                   # note 60
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    for t in range(6):                            # as the boot clear leaves it
        EMU.w32(EMU.sym['dx_tst'] + 512 * t, M32)
    return EMU


def color_for(alg):
    """the Color value (0..127) that picks algorithm alg (1..32)"""
    for c in range(1, 128):
        if (c - 1) * 32 // 127 + 1 == alg:
            return c
    raise ValueError(alg)


def dials(e, t, color=0, shape=64, sweep=64, voice=0):
    for i, v in enumerate((color << 8, shape << 8, sweep << 8, voice << 10)):
        e.w32(e.sym['dx_p'] + 16 * t + 4 * i, v)


def load(e, t, *voices):
    """the track's own cartridge: these voices (VMEM bytes) from voice 1"""
    base = e.sym['dx_base'] + 4096 * t
    for i, v in enumerate(voices):
        e.uc.mem_write(base + 128 * i, v)
    st = e.sym['dx_tst'] + 512 * t
    e.w32(st + 56, 1)
    e.w32(st + 60, 1)


def render(e, blocks, t=0, step=STEP1, trig=True, gate=1):
    if trig:
        e.w8(e.sym['dr_trig'] + t, 1)
    e.w32(e.sym['dx_gate'] + 4 * t, gate)
    out = []
    for _ in range(blocks):
        e.run('dx_fill', stack=[t, step, DX])
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
    k = np.flatnonzero(m)[np.argmax(s[m])]
    a, b, c = np.log(s[k - 1:k + 2] + 1e-9)
    return float(f[k] + (a - c) / (2 * (a - 2 * b + c)) * (f[1] - f[0]))


def cents(a, b):
    return 1200 * np.log2(a / b)


def amp_at(x, hz):
    f, s = spectrum(x)
    m = (f > hz * 0.97) & (f < hz * 1.03)
    return float(s[m].max())


def jv(n, x):
    """Bessel's J_n(x), as (1/pi) int_0^pi cos(n t - x sin t) dt"""
    t = np.linspace(0, np.pi, 20001)
    return float(np.trapezoid(np.cos(n * t - x * np.sin(t)), t) / np.pi)


def rms(x):
    return float(np.sqrt((x ** 2).mean()))


def op(**k):
    return G.op(**k)


def one(alg=32, fb=0, **ops):
    """a voice with the given operators (op1=..., op2=...), the rest silent"""
    o = [ops.get(f'op{i}', op()) for i in range(1, 7)]
    return G.voice('TEST', o, alg=alg, fb=fb)


# ---- the algorithm table, read the way msfa reads it ---------------------------
def structure(flags):
    """direct edges (j -> k) and carriers of one row of dx_alg, by msfa's bus
    rules: ops run OP6 .. OP1, each reads its bus and writes or adds to its
    output (bus one, bus two, or the output)"""
    bus = {1: set(), 2: set()}
    edges, carriers = set(), set()
    for i, fl in enumerate(flags):
        opn = 6 - i
        inb = (fl >> 4) & 3
        if inb:
            for j in bus[inb]:
                edges.add((j, opn))
        out = fl & 3
        if out == 0:
            carriers.add(opn)
        else:
            if fl & 4:
                bus[out] = bus[out] | {opn}
            else:
                bus[out] = {opn}
    fb = [6 - i for i, fl in enumerate(flags) if fl & 0xc0 == 0xc0]
    return edges, carriers, fb


class TAlgorithms(unittest.TestCase):
    def test_table_is_the_dx7s_diagrams(self):
        for a in range(1, 33):
            edges, carriers, fb = structure(G.DX_ALG[a - 1])
            want_e, want_c, want_fb = G.ALGOS[a]
            self.assertEqual(edges, set(want_e), a)
            self.assertEqual(carriers, want_c, a)
            self.assertEqual(fb, [want_fb], a)

    def test_table_assembled(self):
        e = emu()
        got = bytes(e.uc.mem_read(e.sym['dx_alg'], 32 * 6))
        self.assertEqual(got, bytes(b for row in G.DX_ALG for b in row))

    def test_engine_follows_carriers_and_edges(self):
        """for every algorithm (picked by Color): an operator alone sounds iff
        it is a carrier, and a modulator alone with a carrier changes it iff
        it feeds it directly"""
        for a in range(1, 33):
            edges, carriers, _ = G.ALGOS[a][0], G.ALGOS[a][1], G.ALGOS[a][2]
            for k in range(1, 7):                       # carriers
                e = emu()
                load(e, 0, one(**{f'op{k}': op(ol=99)}))
                dials(e, 0, color=color_for(a), sweep=0)
                x = render(e, 40)[1000:]
                if k in carriers:
                    self.assertGreater(np.abs(x).max(), 10000, (a, k))
                else:
                    self.assertLess(np.abs(x).max(), 4, (a, k))
            for k in carriers:                          # what feeds each carrier
                for j in range(1, 7):
                    if j in carriers or j == k:
                        continue
                    e = emu()
                    load(e, 0, one(**{f'op{k}': op(ol=99), f'op{j}': op(ol=85, coarse=3)}))
                    dials(e, 0, color=color_for(a), sweep=0)
                    x = render(e, 64)[1000:]
                    side = amp_at(x, 261.63 * 4) + amp_at(x, 261.63 * 2)
                    main = amp_at(x, 261.63)
                    if (j, k) in edges:
                        self.assertGreater(side, 0.05 * main, (a, j, k))
                    else:
                        self.assertLess(side, 0.002 * main, (a, j, k))

    def test_color_zero_is_the_voices_own(self):
        e = emu()
        load(e, 0, one(alg=1, op1=op(ol=99), op3=op(ol=99)))  # alg 1: carriers 1, 3
        dials(e, 0, color=0)
        a = render(e, 40)[1000:]
        e = emu()
        load(e, 0, one(alg=1, op1=op(ol=99), op3=op(ol=99)))
        dials(e, 0, color=color_for(1))
        b = render(e, 40)[1000:]
        self.assertTrue(np.array_equal(a, b))
        e = emu()
        load(e, 0, one(alg=1, op1=op(ol=99), op3=op(ol=99)))
        dials(e, 0, color=color_for(3))                 # alg 3: OP3 modulates
        c = render(e, 40)[1000:]
        self.assertFalse(np.array_equal(a, c))


class TOperators(unittest.TestCase):
    def test_sine_at_the_note(self):
        for step in (STEP1 // 4, STEP1, STEP1 * 4):
            e = emu()
            x = render(e, 400, step=step)[2000:]            # INIT VOICE
            hz = 261.63 * step / STEP1
            self.assertLess(abs(cents(peak_hz(x, hz * 0.8, hz * 1.25), hz)), 1.5, step)
            f, s = spectrum(x)
            self.assertLess(s[(f > hz * 1.5) & (f < 20000)].max(), s.max() * 1e-3)   # -60 dB
            self.assertGreater(np.abs(x).max(), 12000)

    def test_ratios_and_fixed(self):
        for kw, hz in ((dict(coarse=2), 523.25), (dict(coarse=0), 130.81),
                       (dict(coarse=1, fine=50), 392.44), (dict(coarse=3, det=14), 784.89)):
            e = emu()
            load(e, 0, one(op1=op(ol=99, **kw)))
            x = render(e, 300)[2000:]
            c = cents(peak_hz(x, hz * 0.8, hz * 1.25), hz)
            if 'det' in kw:
                self.assertTrue(3 < c < 10, c)              # detune 14: a few cents up
            else:
                self.assertLess(abs(c), 1.5, kw)
        for step in (STEP1 // 2, STEP1 * 2):                # fixed: 10^2 Hz whatever
            e = emu()                                       # the note
            load(e, 0, one(op1=op(ol=99, mode=1, coarse=2)))
            x = render(e, 400, step=step)[2000:]
            self.assertLess(abs(cents(peak_hz(x, 80, 125), 100)), 3, step)

    def test_output_level(self):
        e = emu()
        load(e, 0, one(op1=op(ol=99)))
        a = rms(render(e, 100)[2000:])
        e = emu()
        load(e, 0, one(op1=op(ol=91)))
        b = rms(render(e, 100)[2000:])
        self.assertAlmostEqual(20 * np.log10(a / b), 6.0, delta=0.3)   # 0.75 dB a step

    def test_fm_index(self):
        """one modulator into one carrier: sidebands as Bessel's, the index at
        OL 99 being 4 pi (msfa: a full modulator is +-2 cycles of phase)"""
        from math import pi
        for ol, beta in ((99, 4 * pi), (91, 2 * pi), (75, pi / 2)):
            e = emu()
            load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=ol)))
            x = render(e, 200, step=STEP1)[2000:]
            f, s = spectrum(x)
            got = [amp_at(x, 261.63 * n) for n in range(1, 6)]
            # harmonic n of a 1:1 pair collects J_(n-1) and J_(n+1) (with sign)
            want = [abs(jv(n - 1, beta) - (-1) ** (n + 1) * jv(n + 1, beta)) for n in range(1, 6)]
            g = np.array(got) / max(got)
            w = np.array(want) / max(want)
            self.assertLess(np.abs(g - w).max(), 0.08, (ol, g, w))

    def test_feedback(self):
        e = emu()
        load(e, 0, one(alg=32, op6=op(ol=99)))
        dials(e, 0, sweep=0)                                 # FB 0: a sine
        x = render(e, 200)[2000:]
        self.assertLess(amp_at(x, 523.25), 1e-3 * amp_at(x, 261.63))
        r = []
        for fb in range(1, 8):
            e = emu()
            load(e, 0, one(alg=32, op6=op(ol=99)))
            dials(e, 0, sweep=fb * 8)
            x = render(e, 200)[2000:]
            r.append([amp_at(x, 261.63 * n) / amp_at(x, 261.63) for n in (2, 3)])
        h2 = [v[0] for v in r]
        self.assertEqual(h2, sorted(h2))                     # brighter step by step
        self.assertTrue(0.4 < r[5][0] < 0.75 and 0.2 < r[5][1] < 0.45, r[5])   # FB6: a saw's
        e = emu()                                            # Sweep 64: the voice's
        load(e, 0, one(alg=32, fb=7, op6=op(ol=99)))
        dials(e, 0)
        x = render(e, 200)[2000:]
        self.assertGreater(amp_at(x, 523.25) / amp_at(x, 261.63), 0.3)

    def test_sweep_shifts_the_modulators(self):
        e = emu()
        load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=80)))
        dials(e, 0, sweep=127)                               # +24: the modulator x4
        x = render(e, 200)[2000:]
        self.assertGreater(amp_at(x, 261.63 * 5), 0.05 * amp_at(x, 261.63))
        self.assertLess(amp_at(x, 261.63 * 2), 0.01 * amp_at(x, 261.63))
        self.assertLess(abs(cents(peak_hz(x, 200, 320), 261.63)), 1.5)   # carrier kept


class TEnvelopes(unittest.TestCase):
    def env(self, r=(99, 99, 99, 99), l=(99, 99, 99, 0), shape=64, blocks=300, hold=None, **k):
        e = emu()
        load(e, 0, one(op1=op(ol=99, r=r, l=l, **k)))
        dials(e, 0, shape=shape)
        if hold is None:
            x = render(e, blocks)
        else:
            x = np.concatenate([render(e, hold), render(e, blocks - hold, trig=False, gate=0)])
        env = np.array([np.abs(x[i:i + 384]).max() for i in range(0, len(x) - 384, 384)])
        return env                                           # one value a 4 ms

    def test_attack_and_shape(self):
        def t_up(env):
            return int(np.argmax(env > 0.7 * env.max()))
        slow = self.env(r=(40, 99, 99, 99))
        fast = self.env(r=(60, 99, 99, 99))
        self.assertLess(t_up(fast), t_up(slow))
        snappy = self.env(r=(40, 99, 99, 99), shape=0)
        pad = self.env(r=(40, 99, 99, 99), shape=127, blocks=1500)
        self.assertLess(t_up(snappy), t_up(slow))
        self.assertLess(t_up(slow), t_up(pad))
        self.assertGreater(t_up(slow), 4 * t_up(snappy))

    def test_decay_to_l2_and_sustain(self):
        env = self.env(r=(99, 50, 99, 99), l=(99, 70, 70, 0), blocks=1500)
        top, held = env[:3].max(), env[-10:].mean()
        self.assertAlmostEqual(20 * np.log10(top / held), 21.0, delta=1.5)   # (63 - 49) 64
        self.assertLess(np.ptp(env[-30:]) / held, 0.02)                     # held there

    def test_release_with_the_gate(self):
        env = self.env(r=(99, 99, 99, 60), l=(99, 99, 99, 0), blocks=600, hold=200)
        self.assertGreater(env[25], 0.9 * env[:20].max())   # held while the gate is
        self.assertLess(env[-1], 0.01 * env[:20].max())      # gone after release
        held = self.env(r=(99, 99, 99, 60), l=(99, 99, 99, 0), blocks=600)
        self.assertGreater(held[-1], 0.9 * held[:40].max())  # no release, no fall

    def test_retrig_keeps_the_level(self):
        e = emu()
        load(e, 0, one(op1=op(ol=99, r=(30, 99, 99, 99))))
        a = render(e, 300)
        b = render(e, 2)                                     # a new note mid-attack
        self.assertLess(abs(np.abs(b[:200]).max() - np.abs(a[-200:]).max()),
                        0.1 * np.abs(a[-200:]).max())        # no jump to silence


class TLfo(unittest.TestCase):
    def test_pitch_and_amp_mod(self):
        e = emu()                                            # vibrato: PMD 99 PMS 7
        v = G.voice('VIB', [op(ol=99)], alg=32, lfo=(60, 0, 99, 0, 1, 4), pms=7)
        load(e, 0, v)
        x = render(e, 1500)
        z = np.flatnonzero((x[:-1] < 0) & (x[1:] >= 0))      # periods
        p = np.diff(z)
        self.assertGreater(p.max() / p.min(), 1.5)           # up to an octave each way
        e = emu()
        load(e, 0, G.voice('NOVIB', [op(ol=99)], alg=32, lfo=(60, 0, 0, 0, 1, 4), pms=7))
        x = render(e, 300)
        p = np.diff(np.flatnonzero((x[:-1] < 0) & (x[1:] >= 0)))
        self.assertLessEqual(p.max() - p.min(), 2)
        e = emu()                                            # tremolo: AMD 99, AMS 3
        load(e, 0, G.voice('TREM', [op(ol=99, ams=3)], alg=32, lfo=(70, 0, 0, 99, 1, 0)))
        x = render(e, 1500)
        env = np.array([np.abs(x[i:i + 960]).max() for i in range(0, len(x) - 960, 960)])
        self.assertGreater(env.max() / env.min(), 4)


class TVoices(unittest.TestCase):
    def test_built_in_cartridge_and_voice_select(self):
        e = emu()
        cart = bytes(e.uc.mem_read(e.sym['dx_cart'], 4096))
        self.assertEqual(cart, G.voices())
        names = [cart[128 * i + 118:128 * i + 128].decode().rstrip() for i in range(32)]
        self.assertEqual(names[0], 'INIT VOICE')
        outs = []
        for v in (0, 1, 5):
            e = emu()
            dials(e, 0, voice=v)
            outs.append(render(e, 40))
            st = e.sym['dx_tst']
            self.assertEqual(bytes(e.uc.mem_read(st + 64, 128)), cart[128 * v:128 * v + 128])
        self.assertFalse(np.array_equal(outs[0], outs[1]))
        self.assertFalse(np.array_equal(outs[1], outs[2]))

    def test_value_names(self):
        e = emu(); s = e.sym
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        def fmt(name, v):
            e.run(name, 0x40000e6e, stack=[0x1000, v, 0x2000])
            sp = e.sp()
            self.assertEqual((e.r32(sp + 4), e.r32(sp + 8)), (0x2000, 0x40124b58))
            return rd(e.r32(sp + 12))
        self.assertEqual(fmt('dx_algfmt', 0), 'VCE')
        self.assertEqual([fmt('dx_algfmt', color_for(a) << 8) for a in (1, 9, 32)],
                         ['A1', 'A9', 'A32'])
        self.assertEqual(fmt('dx_algfmt', 127 << 8), 'A32')
        self.assertEqual([fmt('dx_swfmt', v << 8) for v in (0, 8, 63, 64, 65, 127)],
                         ['FB0', 'FB1', 'FB7', 'VCE', '+0', '+24'])
        e.uc.mem_write(0x40012412, bytes.fromhex('70014e75'))   # the selected track: 1
        self.assertEqual(fmt('dx_vfmt', 0), 'INIT VOICE')
        self.assertEqual(fmt('dx_vfmt', 5 << 10), 'DRAWBARS 1')
        load(e, 1, G.voice('MY VOICE', [op(ol=99)]))
        self.assertEqual(fmt('dx_vfmt', 0), 'MY VOICE')

    def test_bounds_everywhere(self):
        rnd = random.Random(3)
        for _ in range(40):
            e = emu()
            if rnd.random() < 0.5:                          # random voices
                vb = bytearray(rnd.getrandbits(8) for _ in range(128))
                load(e, 0, bytes(vb))
            dials(e, 0, *(rnd.randrange(128) for _ in range(3)), voice=rnd.randrange(32))
            step = rnd.choice((STEP1 // 8, STEP1, 4 * STEP1, 12 * STEP1))
            x = render(e, 6, step=step, trig=True, gate=rnd.randrange(2))
            self.assertLessEqual(np.abs(x).max(), 32767)

    def test_cpu(self):
        worst = 0
        for c in (0, color_for(1), color_for(32), color_for(5)):
            for sw in (0, 64, 127):
                e = emu()
                load(e, 0, G.voice('CPU', [op(ol=99)] * 6, alg=1, fb=7,
                                   lfo=(60, 0, 50, 50, 1, 4), pms=5))
                dials(e, 0, color=c, sweep=sw)
                e.w8(e.sym['dr_trig'], 1)
                e.w32(e.sym['dx_gate'], 1)
                e.count_instructions('dx_fill', RET, stack=[0, STEP1, DX])
                n = max(e.count_instructions('dx_fill', RET, stack=[0, STEP1, DX])
                        for _ in range(3))
                worst = max(worst, n)
        if os.environ.get('VA_TEST_VERBOSE'):
            print("\n  dx_fill instructions a block:", worst)
        self.assertLess(worst, 7200)


class TPlumbing(unittest.TestCase):
    def test_pre_routes_dx(self):
        import test_va as V
        e = emu()
        e.watch('dx_params', 'mi_params', 'dr_params', 'va_params')
        words = (20 << 8, 40 << 8, 60 << 8, 80 << 8)
        ex = V.run_pre(e, 3, DX, trig=True, words=words)
        self.assertEqual(ex, 0x400a7dcc)
        self.assertEqual(e.r32(V.voice(3)), 6)                  # a Sampler voice
        self.assertEqual([e.r32(e.sym['dx_p'] + 48 + 4 * i) for i in range(4)], list(words))
        self.assertEqual(e.r8(e.sym['dr_trig'] + 3), 1)
        self.assertNotEqual(e.r32(e.sym['dx_gate'] + 12), 0)    # the gate, held
        self.assertEqual((e.visits['dx_params'], e.visits['mi_params'], e.visits['dr_params'],
                          e.visits['va_params']), (1, 0, 0, 0))

    def test_render_calls_dx_fill(self):
        import test_va as V
        e = emu()
        e.watch('dx_fill', 'mi_fill', 'dr_fill', 'amp_hook')
        e.w32(e.arr('trk_mach', 1), DX)
        V.T5PreDispatch()._dispatch(e, 1)
        self.assertEqual((e.visits['dx_fill'], e.visits['mi_fill'], e.visits['dr_fill'],
                          e.visits['amp_hook']), (1, 0, 0, 1))

    def test_labels_and_value_formatters(self):
        e = emu(); s = e.sym
        pt = 0x4010dce0
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        CONTOUR, SWEEP, COLOR = 0x40a7345c, 0x40a733f8, 0x40a7296c
        e.w32(CONTOUR, 0x400456c8)
        e.w32(SWEEP, 0x11112222)
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[DX])
        self.assertEqual([rd(e.r32(pt + off + 52)) for off in (2576, 3472, 4088, 4144)],
                         ['ALGO', 'ENV', 'FBHM', 'VOIC'])
        self.assertEqual([rd(e.r32(pt + off + 44)) for off in (2576, 3472, 4088, 4144)],
                         ['Algorithm', 'Envelope', 'FB / Harm', 'Voice'])
        self.assertEqual((e.r32(CONTOUR), e.r32(SWEEP), e.r32(COLOR)),
                         (s['dx_vfmt'], s['dx_swfmt'], s['dx_algfmt']))
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[11])  # PLAITS
        self.assertEqual((e.r32(CONTOUR), e.r32(SWEEP)), (s['mi_eng_fmt'], 0x11112222))
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[DX])
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[4])   # Chord
        self.assertEqual((e.r32(CONTOUR), e.r32(SWEEP), e.r32(COLOR)),
                         (0x400456c8, 0x11112222, 0x400456c8))
        self.assertEqual(e.r32(pt + 2576 + 44), 0x4012999c)    # stock labels again

    def test_commit_defaults(self):
        import test_va as V
        e = emu()
        T = V.T6Commit()
        T._commit(e, DX)
        self.assertEqual([e.r16(T.SOUND + 42 + 2 * i) >> 8 for i in range(4)], [0, 64, 64, 0])

    def test_level_x2(self):
        e = emu()
        BUF = SCRATCH + 0x60000
        e.w32(e.arr('trk_mach', 2), DX)
        for i in range(32):
            e.w32(BUF + 4 * i, (i - 16) << 24)
        e.run('dr_punch', stack=[BUF, 2])
        self.assertEqual([e.rs32(BUF + 4 * i) for i in range(32)],
                         [max(-0x3fffffff, min(0x3fffffff, (i - 16) << 24)) * 2 for i in range(32)])


if __name__ == '__main__':
    unittest.main(verbosity=2)
