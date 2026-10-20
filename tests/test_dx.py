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
DXOP = 192                                        # the operators in a track's state
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    for t in range(6):                            # as the boot clear leaves it,
        EMU.w32(EMU.sym['dx_tst'] + 512 * t, M32)
        dials(EMU, t)                             # and the defaults row's dials
    return EMU


OWNV = G.BANK - G.OWN                             # INIT VOICE, in the built-in bank


def contour(voice, n=G.BANK):
    """Contour's value that picks voice (from 0) of n (dx_vidx)"""
    return min(k for k in range(128) if k * n >> 7 == voice) << 8


def dials(e, t, color=64, shape=64, sweep=64, voice=None, own=None):
    """the four dials; voice of the built-in bank, or of the track's own
    cartridge (own; by default if it has one loaded, voice 1 of it, else
    INIT VOICE of the bank)"""
    if own is None:
        own = e.r32(e.sym['dx_tst'] + 512 * t + 56) != 0
    if voice is None:
        voice = 0 if own else OWNV
    for i, v in enumerate((color << 8, shape << 8, sweep << 8, contour(voice, 32 if own else G.BANK))):
        e.w32(e.sym['dx_p'] + 16 * t + 4 * i, v)


def load(e, t, *voices):
    """the track's own cartridge: these voices (VMEM bytes) from voice 1"""
    base = e.sym['dx_base'] + 4096 * t
    for i, v in enumerate(voices):
        e.uc.mem_write(base + 128 * i, v)
    st = e.sym['dx_tst'] + 512 * t
    e.w32(st + 56, 1)
    e.w32(st + 60, 1)
    dials(e, t)


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
        """for every algorithm (the voice's own): an operator alone sounds iff
        it is a carrier, and a modulator alone with a carrier changes it iff
        it feeds it directly"""
        for a in range(1, 33):
            edges, carriers, _ = G.ALGOS[a][0], G.ALGOS[a][1], G.ALGOS[a][2]
            for k in range(1, 7):                       # carriers
                e = emu()
                load(e, 0, one(alg=a, **{f'op{k}': op(ol=99)}))
                x = render(e, 40)[1000:]
                if k in carriers:
                    self.assertGreater(np.abs(x).max(), 6000, (a, k))
                else:
                    self.assertLess(np.abs(x).max(), 4, (a, k))
            for k in carriers:                          # what feeds each carrier
                for j in range(1, 7):
                    if j in carriers or j == k:
                        continue
                    e = emu()
                    load(e, 0, one(alg=a, **{f'op{k}': op(ol=99), f'op{j}': op(ol=85, coarse=3)}))
                    x = render(e, 64)[1000:]
                    side = amp_at(x, 261.63 * 4) + amp_at(x, 261.63 * 2)
                    main = amp_at(x, 261.63)
                    if (j, k) in edges:
                        self.assertGreater(side, 0.05 * main, (a, j, k))
                    else:
                        self.assertLess(side, 0.002 * main, (a, j, k))

    def test_feedback_operator_overwrites_its_bus(self):
        """the feedback operator of algorithm 2 (OP2) writes bus one, so OP4's
        output (into OP3) must not reach OP1 - it did when the add-or-write
        test was lost"""
        def out(op4):
            e = emu()
            load(e, 0, one(alg=2, fb=5, op1=op(ol=99), op2=op(ol=80), op4=op(ol=op4)))
            return render(e, 200)[2000:]
        self.assertTrue(np.array_equal(out(0), out(80)))

class TOperators(unittest.TestCase):
    def test_sine_at_the_note(self):
        for step in (STEP1 // 4, STEP1, STEP1 * 4):
            e = emu()
            x = render(e, 400, step=step)[2000:]            # INIT VOICE
            hz = 261.63 * step / STEP1
            self.assertLess(abs(cents(peak_hz(x, hz * 0.8, hz * 1.25), hz)), 1.5, step)
            f, s = spectrum(x)
            self.assertLess(s[(f > hz * 1.5) & (f < 20000)].max(), s.max() * 1e-3)   # -60 dB
            self.assertGreater(np.abs(x).max(), 6000)       # a carrier at -12 dB

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
        for kw, hz in ((dict(coarse=0, fine=50), 3.1623), (dict(coarse=1, fine=37), 23.442),
                       (dict(coarse=3, fine=99), 9772.4)):  # 10^(c + f / 100), even
            e = emu()                                       # below 8 Hz
            load(e, 0, one(op1=op(ol=99, mode=1, **kw)))
            st = e.sym['dx_tst'] + DXOP + 48 * 5            # OP1
            render(e, 2)
            inc = e.r32(st + 44)
            self.assertTrue(inc & 0x80000000)
            got = (inc & 0x7fffffff) * 48000 / 2 ** 32
            self.assertLess(abs(cents(got, hz)), 2, kw)

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
        load(e, 0, one(alg=32, op6=op(ol=99)))              # FB 0: a sine
        x = render(e, 200)[2000:]
        self.assertLess(amp_at(x, 523.25), 1e-3 * amp_at(x, 261.63))
        r = []
        for fb in range(1, 8):
            e = emu()
            load(e, 0, one(alg=32, fb=fb, op6=op(ol=99)))
            x = render(e, 200)[2000:]
            r.append([amp_at(x, 261.63 * n) / amp_at(x, 261.63) for n in (2, 3)])
        h2 = [v[0] for v in r]
        self.assertEqual(h2[:6], sorted(h2[:6]))             # brighter step by step
        self.assertTrue(0.4 < r[5][0] < 0.75 and 0.2 < r[5][1] < 0.45, r[5])   # FB6: a saw's

    def test_macros_at_64_and_on_carriers_alone(self):
        """64 is the voice as it is; Bright leaves a voice of carriers alone,
        and Sweep a voice without feedback below the middle; Shape leaves
        silent modulators silent"""
        v = one(alg=1, op1=op(ol=99, r=(60, 50, 99, 60)), op2=op(ol=80, r=(50, 40, 99, 60)))
        def out(voice=v, **d):
            e = emu()
            load(e, 0, voice)
            dials(e, 0, **d)
            return render(e, 300)
        ref = out()
        self.assertTrue(np.array_equal(ref, out(color=64, shape=64, sweep=64)))
        c = one(alg=32, op6=op(ol=99, r=(60, 50, 99, 60)))
        self.assertTrue(np.array_equal(out(c), out(c, color=0, sweep=0)))
        m = one(alg=1, op2=op(ol=80, r=(50, 40, 99, 60)))   # a modulator alone: silent
        self.assertEqual(np.abs(out(m, shape=0)).max(), 0)

    def test_bright(self):
        def bright(color):
            e = emu()
            load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=60)))
            dials(e, 0, color=color)
            x = render(e, 200)[2000:]
            return sum(amp_at(x, 261.63 * n) for n in (2, 3, 4)) / amp_at(x, 261.63)
        b = [bright(c) for c in (0, 32, 64, 96, 127)]
        self.assertEqual(b, sorted(b))
        self.assertLess(b[0], 0.3 * b[2])
        self.assertGreater(b[4], 3 * b[2])
        e = emu()                                            # Bright 127 = the
        load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=60)))   # modulator 504 / 32
        dials(e, 0, color=127)                               # output levels up
        a = render(e, 200)[2000:]
        e = emu()
        load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=76)))
        b = render(e, 200)[2000:]
        self.assertLess(abs(20 * np.log10(amp_at(a, 523.25) / amp_at(b, 523.25))), 1.0)

    def test_env_time(self):
        """Shape moves every stage of every operator's own envelope: a decay
        is shorter below the middle, longer above"""
        def level(shape, at):
            e = emu()
            load(e, 0, one(alg=1, op1=op(ol=99, r=(99, 50, 99, 60), l=(99, 0, 0, 0)),
                           op2=op(ol=80, r=(99, 50, 99, 60), l=(99, 0, 0, 0))))
            dials(e, 0, shape=shape)
            x = render(e, int(at * 1500) + 100)
            return rms(x[int(at * FS):int(at * FS) + 4096])
        a = [level(k, 0.4) for k in (0, 32, 64, 96)]
        self.assertEqual(a, sorted(a))                         # longer and longer
        self.assertLess(a[0], 0.01 * a[2])
        self.assertGreater(a[3], 3 * a[2])
        self.assertGreater(level(127, 2.0), 10 * level(64, 2.0))   # (its attack slower too)

    def test_feedback_macro(self):
        """Sweep sets the feedback: the voice's at 64, none at 0, 7 at 127,
        in steps between"""
        e = emu()
        def fb(sweep, voice_fb):
            load(e, 0, one(alg=32, fb=voice_fb, op6=op(ol=99)))
            dials(e, 0, sweep=sweep)
            render(e, 1)
            return e.r32(e.sym['dx_tst'] + 0)          # (the shift is a local:
        def h2(sweep, voice_fb):                        # measured by its sound)
            e2 = emu()
            load(e2, 0, one(alg=32, fb=voice_fb, op6=op(ol=99)))
            dials(e2, 0, sweep=sweep)
            x = render(e2, 200)[2000:]
            return amp_at(x, 523.25) / amp_at(x, 261.63)
        def same(a, b):
            return np.array_equal(a, b)
        def out(sweep, voice_fb):
            e2 = emu()
            load(e2, 0, one(alg=32, fb=voice_fb, op6=op(ol=99)))
            dials(e2, 0, sweep=sweep)
            return render(e2, 100)
        self.assertTrue(same(out(64, 4), out(127, 4)) is False)
        self.assertTrue(same(out(0, 4), out(64, 0)))           # 0: none
        self.assertTrue(same(out(127, 0), out(64, 7)))         # 127: 7
        self.assertTrue(same(out(32, 4), out(64, 2)))          # halfway down: half
        self.assertTrue(same(out(96, 4), out(64, 6)))          # 4 + 3 x 32/63 = 5.5 -> 6
        r = [h2(k, 3) for k in (0, 40, 64, 90, 127)]
        self.assertEqual(r, sorted(r))

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

    def test_retrig_starts_from_silence(self):
        """a new trig is a new note from level 0, as in Dexed (msfa's
        Env::init): a held pad does not come back at its sustain under the
        track's restarting Amp Decay - but its gain still ramps down"""
        e = emu()
        load(e, 0, one(op1=op(ol=99, r=(30, 99, 99, 99))))
        a = render(e, 600)
        b = render(e, 40)                                    # a new note
        top = np.abs(a[-200:]).max()
        self.assertLess(np.abs(b[64 * 6:64 * 7]).max(), 0.1 * top)   # back to silence...
        self.assertGreater(np.abs(b[:64]).max(), 0.5 * top)          # ...not at once

    def test_dx_always_fades_in(self):
        """amp_hook: Attack 0 on a DX track still ramps over 96 samples (2 ms);
        on other machines it is full from the first sample"""
        e = emu()
        BUF = SCRATCH + 0x60000
        for mach, want in ((DX, True), (1, False)):
            e.w32(e.arr('trk_mach', 2), mach)
            e.w32(e.arr('atk_step', 2), 0)
            e.w32(e.arr('atk_gain', 2), 0)
            for i in range(32):
                e.w32(BUF + 4 * i, 0x40000000)
            e.run('ah_found', e.sym['ah_flt'], regs={D[3]: 2, A[2]: BUF})
            x = [e.rs32(BUF + 4 * i) for i in range(32)]
            if want:
                self.assertEqual(x[0], 0)
                self.assertEqual(x, sorted(x))
                self.assertAlmostEqual(x[31] / 0x40000000, 31 / 96, delta=0.01)
                self.assertEqual(e.r32(e.arr('atk_gain', 2)), 32 * 174763)
            else:
                self.assertEqual(x, [0x40000000] * 32)

class TPitchEg(unittest.TestCase):
    """msfa's PitchEnv: pitchenv_tab[L] << 19 octaves (Q24), from L4 at the
    note, stage 3 after the key lifts"""
    def hz(self, x):
        z = np.flatnonzero((x[:-1] < 0) & (x[1:] >= 0))
        return FS / np.diff(z).mean()

    def test_held_level(self):
        e = emu()
        load(e, 0, G.voice('PEG', [op(ol=99)], alg=32, peg_l=(62, 62, 62, 62)))
        x = render(e, 300)[4000:]
        want = 261.63 * 2 ** (G.PEG_TAB[62] / 32)          # 12 / 32 octave up
        self.assertLess(abs(cents(self.hz(x), want)), 3)

    def test_glide_from_l4_and_release(self):
        e = emu()
        load(e, 0, G.voice('PEG', [op(ol=99, l=(99, 99, 99, 99))], alg=32,
                           peg_r=(50, 99, 99, 50), peg_l=(50, 50, 50, 70)))
        a = render(e, 20)
        b = render(e, 1500, trig=False)
        hi, lo = 261.63 * 2 ** (G.PEG_TAB[70] / 32), 261.63
        self.assertLess(abs(cents(self.hz(a), hi)), 40)     # it starts at L4...
        self.assertLess(abs(cents(self.hz(b[-9600:]), lo)), 3)   # ...and lands on L1
        c = render(e, 1500, trig=False, gate=0)              # released: back to L4
        self.assertLess(abs(cents(self.hz(c[-9600:]), hi)), 3)


class TGate(unittest.TestCase):
    """a trig's gate seen for under three blocks is a pulse: the note holds on
    (the track's Amp Decay shapes it); a longer one is the note's length"""
    def tail(self, held):
        e = emu()
        load(e, 0, one(op1=op(ol=99, r=(99, 99, 99, 60), l=(99, 99, 99, 0))))
        render(e, held)
        return np.abs(render(e, 600, trig=False, gate=0)[-4800:]).max()

    def test_pulse_holds_and_length_releases(self):
        self.assertGreater(self.tail(1), 6000)
        self.assertGreater(self.tail(2), 6000)
        self.assertLess(self.tail(3), 100)
        self.assertLess(self.tail(200), 100)


class TVelocity(unittest.TestCase):
    def level(self, vel, kvs, **k):
        e = emu()
        load(e, 0, one(op1=op(ol=99, kvs=kvs), **k))
        dials(e, 0)
        e.w8(e.sym['dx_vel'], vel)
        return rms(render(e, 100)[2000:])

    def test_carrier_level_follows_msfa(self):
        ref = self.level(100, 7)
        self.assertLess(self.level(1, 7), ref * 10 ** (-70 / 20))   # -79 dB: silent, or near
        for vel in (40, 64, 100, 127):
            got = 20 * np.log10(self.level(vel, 7) / ref)
            want = (G.scale_velocity(vel, 7) - G.scale_velocity(100, 7)) / 256 * 6.0206
            self.assertAlmostEqual(got, want, delta=0.3, msg=vel)
        self.assertAlmostEqual(self.level(1, 0), self.level(127, 0), delta=1)   # KVS 0: none
        self.assertGreater(20 * np.log10(self.level(127, 7) / self.level(100, 7)), 4.5)  # above OL 99

    def test_modulator_velocity_brightens(self):
        def bright(vel):
            e = emu()
            load(e, 0, one(alg=1, op1=op(ol=99), op2=op(ol=70, kvs=7)))   # index < 2
            dials(e, 0)
            e.w8(e.sym['dx_vel'], vel)
            x = render(e, 100)[2000:]
            return sum(amp_at(x, 261.63 * n) for n in (2, 3, 4)) / amp_at(x, 261.63)
        self.assertLess(bright(30), bright(80))
        self.assertLess(bright(80), bright(127))

    def test_taken_at_the_note(self):
        e = emu()
        load(e, 0, one(op1=op(ol=99, kvs=7)))
        dials(e, 0)
        e.w8(e.sym['dx_vel'], 127)
        a = rms(render(e, 60)[2000:])
        e.w8(e.sym['dx_vel'], 20)                       # no new note: unchanged
        b = rms(render(e, 60, trig=False)[1000:])
        self.assertAlmostEqual(a, b, delta=a * 0.02)
        c = rms(render(e, 60)[2000:])                    # the next note takes it
        self.assertLess(c, a * 0.5)


class TBuiltIn(unittest.TestCase):
    def test_voices_equally_loud_and_no_instant_onset(self):
        louds = {}
        for v in range(11):
            e = emu()
            dials(e, 0, voice=OWNV + v)
            e.w8(e.sym['dx_vel'], 100)
            x = render(e, 1500)
            louds[v] = max(rms(x[i:i + 9600]) for i in range(0, len(x) - 9600, 4800))
            first = np.abs(x[:48]).max()                     # the first half millisecond
            self.assertLess(first, 0.5 * np.abs(x[:4800]).max(), v)
        db = 20 * np.log10(np.array(list(louds.values())) / louds[0])
        if os.environ.get('VA_TEST_VERBOSE'):
            print('\n  built-in voices, dB:', np.round(db, 2))
        self.assertLess(np.abs(db).max(), 1.5, db)

    def test_shape_reaches_pads_and_plucks(self):
        def attack(shape, r1=99):
            e = emu()
            load(e, 0, one(op1=op(ol=99, r=(r1, 99, 99, 99))))
            dials(e, 0, shape=shape)
            x = render(e, 2000)
            env = np.array([np.abs(x[i:i + 960]).max() for i in range(0, len(x) - 960, 960)])
            return int(np.argmax(env > 0.7 * env.max()))     # 10 ms steps
        self.assertLessEqual(attack(64), 1)                  # R1 99: at once
        self.assertGreater(attack(127), 40)                  # Amp Env up: a pad, > 0.4 s
        self.assertLess(attack(0, r1=60), attack(64, r1=60))  # down: faster
        self.assertLess(attack(64, r1=60), attack(96, r1=60))

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
        cart = bytes(e.uc.mem_read(e.sym['dx_cart'], 128 * G.BANK))
        self.assertEqual(cart, G.bank())
        self.assertEqual(e.sym['dx_own'], e.sym['dx_cart'] + 128 * OWNV)
        names = [cart[128 * i + 118:128 * i + 128].decode().rstrip() for i in range(G.BANK)]
        self.assertEqual((names[0], names[31], names[32], names[51], names[OWNV], names[-1]),
                         ('Tooropulse', 'Swissynth', 'DX9. 1', 'DX9.20', 'INIT VOICE', 'SINE BASS'))
        self.assertEqual(cart[:4096], open(os.path.join(G.REPO, 'carts', 'Legowelt_magic_DX7.vmem'),
                                             'rb').read())
        self.assertEqual([k * G.BANK >> 7 for k in (0, 127)], [0, G.BANK - 1])   # all reached
        self.assertEqual(len({k * G.BANK >> 7 for k in range(128)}), G.BANK)
        outs = []
        for v in (0, 1, OWNV + 5):
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
        self.assertEqual([fmt('dx_offmt', v << 8) for v in (0, 1, 63, 64, 65, 127)],
                         ['-64', '-63', '-1', '0', '+1', '+63'])
        self.assertEqual(fmt('dx_offmt', 0xffff8000), '-64')
        e.uc.mem_write(0x40012412, bytes.fromhex('70014e75'))   # the selected track: 1
        self.assertEqual([fmt('dx_vfmt', v << 8) for v in (0, 64, 127)], ['1', '32', '63'])
        self.assertEqual(fmt('dx_vfmt', contour(OWNV)), str(OWNV + 1))
        load(e, 1, G.voice('MY VOICE', [op(ol=99)]))             # its own cartridge: 32
        self.assertEqual([fmt('dx_vfmt', v << 8) for v in (0, 64, 127)], ['1', '17', '32'])

    def test_knob_screen_names_the_voice(self):
        e = emu(); s = e.sym
        calls = []
        def hook(fn, kind, n):
            e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: calls.append(
                (kind,) + struct.unpack('>%di' % n, uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 4 * n))),
                begin=fn, end=fn)
        hook(0x40070efc, 'fill', 6); hook(0x40071c10, 'text', 9); hook(0x400720f8, 'font', 1)
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        e.uc.mem_write(0x40012412, bytes.fromhex('70014e75'))   # the selected track: 1
        def draw(width, v):
            calls.clear()
            e.w32(SCRATCH + 0x9000, width)                       # the width, from memory:
            e.uc.mem_write(0x40072102, bytes.fromhex('2039%08x4e75' % (SCRATCH + 0x9000)))
            e.run('dx_draw', stack=[0x1000, v, 0x5555, 0x60, 0x22, 0])
            return calls
        c = draw(40, contour(OWNV + 5))                          # fits the big font
        fills = [x for x in c if x[0] == 'fill']
        self.assertEqual(fills[0][1:], (0x5555, 66, 22, 122, 28, 0))   # the stock pips off
        text = [x for x in c if x[0] == 'text'][0]
        self.assertEqual(rd(text[9] & M32), 'DRAWBARS 1')
        self.assertEqual(text[3:5], (96 - 20, 0x22))
        self.assertEqual([x[1] & M32 for x in c if x[0] == 'font'][-1], 0x40ea14dc)   # big
        c = draw(70, contour(OWNV + 5))                          # too wide: small, at 66
        text = [x for x in c if x[0] == 'text'][0]
        self.assertEqual([x[1] & M32 for x in c if x[0] == 'font'][-1], 0x40ea14cc)
        self.assertEqual(text[3], 66)
        load(e, 1, G.voice('MY VOICE', [op(ol=99)]))             # the track's own cartridge
        text = [x for x in draw(40, 0) if x[0] == 'text'][0]
        self.assertEqual(rd(text[9] & M32), 'MY VOICE')

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
        for c in (0, 64, 127):
            for sw in (0, 64, 127):
                e = emu()
                load(e, 0, G.voice('CPU', [op(ol=99, ams=3)] * 6, alg=1, fb=7,
                                   lfo=(60, 0, 50, 50, 1, 4), pms=5))
                dials(e, 0, color=c, shape=sw, sweep=sw)
                e.w8(e.sym['dr_trig'], 1)
                e.w32(e.sym['dx_gate'], 1)
                e.count_instructions('dx_fill', RET, stack=[0, STEP1, DX])
                n = max(e.count_instructions('dx_fill', RET, stack=[0, STEP1, DX])
                        for _ in range(3))
                worst = max(worst, n)
        if os.environ.get('VA_TEST_VERBOSE'):
            print("\n  dx_fill instructions a block:", worst)
        self.assertLess(worst, 7200)

# ---- a drive, as the sample loader reads it (model_tg.s: load_at) ------------
TABLE_BLOCK, DATA_BASE = 1835088, 1851472
SOUNDS = SCRATCH + 0x70000                      # stand-ins for the tracks' sounds


class Drive:
    """readBlocks (0x4008b24a) and sound_obj answered from Python: files by
    hash in the inode table's first page, their data in 16 KiB chunks"""
    def __init__(self, e):
        self.e, self.blocks, self.files, self.reads = e, {}, [], 0
        e.uc.hook_add(UC_HOOK_CODE, self._read, begin=0x4008b24a, end=0x4008b24a)
        so = e.sym['sound_obj']
        e.uc.hook_add(UC_HOOK_CODE, self._sound, begin=so, end=so)

    def add(self, hsh, data):
        chunk = 5 + 4 * len(self.files)
        self.files.append((hsh, len(data), chunk))
        for i in range(0, len(data) + 511, 512):
            self.blocks[DATA_BASE + chunk * 32 + i // 512] = data[i:i + 512].ljust(512, b'\0')

    def _read(self, uc, addr, size, _):
        sp = uc.reg_read(UC_M68K_REG_A7)
        block, n, dest = struct.unpack('>3I', uc.mem_read(sp + 4, 12))
        self.reads += 1
        if TABLE_BLOCK <= block < TABLE_BLOCK + 128 * 128:
            page = bytearray(n)
            if block == TABLE_BLOCK:
                for i, (h, size_, chunk) in enumerate(self.files):
                    r = 128 * (i + 3)
                    page[r:r + 4] = struct.pack('>I', 0x00020001)
                    page[r + 4:r + 8] = struct.pack('>I', size_)
                    page[r + 12:r + 16] = struct.pack('>I', h)
                    page[r + 36:r + 44] = struct.pack('>II', (size_ + 16383) // 16384, chunk)
            uc.mem_write(dest, bytes(page))
        else:
            uc.mem_write(dest, b''.join(self.blocks.get(block + i, bytes(512))
                                        for i in range(n // 512)))
        uc.reg_write(UC_M68K_REG_D0, 0)

    def _sound(self, uc, addr, size, _):
        t = uc.reg_read(UC_M68K_REG_D0)
        sp = uc.reg_read(UC_M68K_REG_A7)
        uc.reg_write(UC_M68K_REG_D0, SOUNDS + 0x100 * t if t < 6 else 0)
        uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', uc.mem_read(sp, 4))[0])
        uc.reg_write(UC_M68K_REG_A7, sp + 4)


def bulk(voices, ch=0):
    data = b''.join(voices)
    return bytes([0xf0, 0x43, ch, 0x09, 0x20, 0x00]) + data + \
        bytes([(-sum(data)) & 0x7f, 0xf7])


def vced(name, ops, alg=1, fb=0, oks=1, peg_r=(99,) * 4, peg_l=(50,) * 4,
         lfo=(35, 0, 0, 0, 1, 0), pms=3, tr=24):
    """one voice, unpacked (155 bytes), from the generator's op() dicts"""
    ops = list(ops) + [op()] * (6 - len(ops))
    b = bytearray()
    for o in reversed(ops):
        b += bytes(o['r']) + bytes(o['l']) + bytes([o['bp'], o['ld'], o['rd'], o['lc'], o['rc'],
                                                    o['rs'], o['ams'], o['kvs'], o['ol'], o['mode'],
                                                    o['coarse'], o['fine'], o['det']])
    b += bytes(peg_r) + bytes(peg_l) + bytes([alg - 1, fb, oks]) + bytes(lfo[:4]) + \
        bytes([lfo[4], lfo[5], pms, tr]) + name.ljust(10)[:10].encode()
    assert len(b) == 155
    return bytes(b)


class TCartridges(unittest.TestCase):
    H = 0xABCD1234

    def cart(self, e, t):
        return bytes(e.uc.mem_read(e.sym['dx_base'] + 4096 * t, 4096))

    def st(self, e, t, off):
        return e.r32(e.sym['dx_tst'] + 512 * t + off)

    def load(self, data, t=2):
        e = emu()
        d = Drive(e)
        d.add(0x11111111, b'not this one')
        d.add(self.H, data)
        e.run('dx_cload', regs={D[0]: t, D[1]: self.H})
        return e, e.d(0)

    def test_bulk_dump(self):
        vs = [G.voice('V%d' % i, [op(ol=60 + i)]) for i in range(32)]
        e, r = self.load(b'\xf8\x00' + bulk(vs, ch=3) + b'\xf0\x7e\xf7')  # junk around it
        self.assertEqual(r, 0)
        self.assertEqual(self.cart(e, 2), b''.join(vs))
        self.assertEqual((self.st(e, 2, 56), self.st(e, 2, 60)), (1, 1))
        dials(e, 2, voice=7, own=True)                  # it plays: voice 8 is V7
        render(e, 2, t=2)
        self.assertEqual(bytes(e.uc.mem_read(e.sym['dx_tst'] + 512 * 2 + 64 + 118, 10)),
                         b'V7'.ljust(10))

    def test_single_voice_and_bare_bank(self):
        ops = [op(ol=99, coarse=3, fine=21, det=11, r=(90, 80, 70, 60), l=(99, 88, 77, 0),
                  bp=41, ld=12, rd=34, lc=1, rc=2, rs=5, ams=2, kvs=6, mode=0),
               op(ol=77, mode=1, coarse=2, fine=50)]
        kw = dict(alg=17, fb=6, oks=0, peg_r=(10, 20, 30, 40), peg_l=(45, 55, 65, 50),
                  lfo=(33, 44, 55, 66, 1, 3), pms=5, tr=30)
        one_v = vced('VCED TEST', ops, **kw)
        msg = bytes([0xf0, 0x43, 0x00, 0x00, 0x01, 0x1b]) + one_v + bytes([0, 0xf7])
        e, r = self.load(msg)
        self.assertEqual(r, 0)
        c = self.cart(e, 2)
        self.assertEqual(c[:128], G.voice('VCED TEST', ops, **kw))   # packed exactly
        self.assertEqual(c[128:256], G.voices()[:128])                # then INIT VOICE
        self.assertEqual(c[-128:], G.voices()[:128])
        vs = [G.voice('BANK%d' % i, [op(ol=99)]) for i in range(32)]
        e, r = self.load(b''.join(vs))
        self.assertEqual((r, self.cart(e, 2)), (0, b''.join(vs)))

    def test_not_a_cartridge_or_missing(self):
        e, r = self.load(b'RIFF' + bytes(5000))
        self.assertEqual((r, self.st(e, 2, 56)), (2, 0))
        e, r = self.load(bytes([0xf0, 0x41, 0, 9, 0x20, 0]) + bytes(4098))   # Roland's
        self.assertEqual(r, 2)
        e = emu()
        Drive(e).add(0x22222222, bulk([G.voice('X', [op()])] * 32))
        e.run('dx_cload', regs={D[0]: 1, D[1]: self.H})
        self.assertEqual(e.d(0), 1)
        e.run('dx_cload', regs={D[0]: 1, D[1]: 0})
        self.assertEqual(e.d(0), 1)
        e, r = self.load(bulk([G.voice('Y', [op()])] * 32)[:3000])        # cut short
        self.assertEqual(r, 2)

    def test_behind_a_sample_header(self):
        """a cartridge uploaded as a WAV through Transfer (flasher/dx.html): the
        Cycles keeps it behind its 64-byte sample header"""
        vs = [G.voice('WAV%d' % i, [op(ol=99)]) for i in range(32)]
        hdr = b'\x00\x00\x00\x01' + struct.pack('>II', 4104, 48000) + bytes(52)
        e, r = self.load(hdr + bulk(vs))
        self.assertEqual((r, self.cart(e, 2)), (0, b''.join(vs)))

    def test_7bit_data(self):
        vs = [bytes(range(128))] * 32
        raw = bytearray(b''.join(vs))
        raw[5] |= 0x80                                   # a stray top bit: dropped
        e, r = self.load(bytes(raw))
        self.assertEqual(r, 0)
        self.assertEqual(self.cart(e, 2)[5], 5)

    def test_token_and_probe(self):
        e = emu(); s = e.sym
        d = Drive(e)
        vs = [G.voice('PROBE%d' % i, [op(ol=99)]) for i in range(32)]
        d.add(self.H, bulk(vs))
        for t in range(6):                               # DX on tracks 1 and 4
            e.w8(SOUNDS + 0x100 * t + 38, DX if t in (1, 4) else 6)
        e.run('dx_nstore', regs={D[0]: 1, D[1]: self.H})
        self.assertEqual(bytes(e.uc.mem_read(SOUNDS + 0x100 + 4, 12)), b'DX7ABCD1234\0')
        self.assertEqual(e.r32(s['dx_hash'] + 4), self.H)
        e.uc.mem_write(SOUNDS + 0x400 + 4, b'DX7ABCD1234\0')   # track 4: as a pattern
        e.uc.mem_write(SOUNDS + 0x300 + 4, b'DX7ABCD1234\0')   # track 3 is no DX
        for _ in range(16):
            e.run('dx_probe')
        self.assertEqual(self.cart(e, 4), b''.join(vs))
        self.assertEqual((self.st(e, 4, 56), e.r32(s['dx_hash'] + 16)), (1, self.H))
        self.assertEqual(self.st(e, 3, 56), 0)
        n = d.reads
        for _ in range(32):                              # nothing new: no reads
            e.run('dx_probe')
        self.assertEqual(d.reads, n)
        e.uc.mem_write(SOUNDS + 0x400 + 4, b'Init\0')     # a pattern without one:
        for _ in range(16):                              # the built-in cartridge
            e.run('dx_probe')
        self.assertEqual((self.st(e, 4, 56), self.st(e, 4, 60)), (0, 1))
        e.uc.mem_write(SOUNDS + 0x400 + 4, b'DX7DEADBEEF\0')  # gone from the drive:
        for _ in range(16):                              # built-in, and no retries
            e.run('dx_probe')
        self.assertEqual(self.st(e, 4, 56), 0)
        n = d.reads
        for _ in range(32):
            e.run('dx_probe')
        self.assertEqual(d.reads, n)

    def test_picking_a_voice_plays_the_preset(self):
        """Contour turned by hand (the sound's Contour the only word that
        changed): macros back to the middle, algorithm to VCE, in the sound
        and in the audio's words. Anything else changing with it (a pattern
        loaded) leaves them."""
        e = emu(); s = e.sym
        Drive(e)
        TDW = SCRATCH + 0x9800
        S1 = SOUNDS + 0x100
        e.w8(S1 + 38, DX)                                # track 1 is a DX
        e.w32(s['dx_tdp'] + 4, TDW)
        def setm(w=(10 << 8, 20 << 8, 30 << 8), alg=0x1000):
            for k, v in enumerate(w):
                e.w16(S1 + 42 + 2 * k, v); e.w16(TDW + 22 + 2 * k, v)
            e.w16(S1 + 72, alg); e.w16(TDW + 52, alg)
        def probe():
            for _ in range(16):
                e.run('dx_probe')
        macros = lambda: ([e.r16(S1 + 42 + 2 * k) for k in range(3)] + [e.r16(S1 + 72)],
                          [e.r16(TDW + 22 + 2 * k) for k in range(3)] + [e.r16(TDW + 52)])
        setm(); probe()                                   # the first look: kept
        self.assertEqual(macros()[0], [10 << 8, 20 << 8, 30 << 8, 0x1000])
        e.w16(S1 + 48, 9 << 10); probe()                 # Contour alone: the preset
        self.assertEqual(macros(), ([64 << 8] * 3 + [0], [64 << 8] * 3 + [0]))
        setm(); probe()                                   # macros turned: kept
        self.assertEqual(macros()[0], [10 << 8, 20 << 8, 30 << 8, 0x1000])
        e.w16(S1 + 48, 3 << 10); e.w16(S1 + 20, 77); probe()   # with another word:
        self.assertEqual(macros()[0], [10 << 8, 20 << 8, 30 << 8, 0x1000])   # a pattern
        e.w8(S1 + 38, 6); probe()                         # not DX: forgotten...
        e.w8(S1 + 38, DX); e.w16(S1 + 48, 5 << 10); probe()     # ...so no reset on return
        self.assertEqual(macros()[0], [10 << 8, 20 << 8, 30 << 8, 0x1000])

    def test_preset_browser_loads_onto_a_dx_track(self):
        e = emu(); s = e.sym
        d = Drive(e)
        vs = [G.voice('BROWSE%d' % i, [op(ol=99)]) for i in range(32)]
        d.add(self.H, bulk(vs))
        e.w8(SOUNDS + 0x200 + 38, DX)                    # track 2 is a DX
        e.w32(s['sel_hash'], self.H)
        rd = lambda: bytes(e.uc.mem_read(s['msg_buf'], 32)).split(b'\0')[0].decode()
        for off, w in ((42, 10 << 8), (44, 20 << 8), (46, 30 << 8), (72, 0x12340000)):
            e.w32(SOUNDS + 0x200 + off, w) if off == 72 else e.w16(SOUNDS + 0x200 + off, w)
        e.run('preset_fail_hook', regs={D[0]: 1}, stack=[0, 0, 2])
        self.assertEqual(rd(), 'Cartridge\nloaded')
        self.assertEqual([e.r16(SOUNDS + 0x200 + o) for o in (42, 44, 46)], [64 << 8] * 3)
        self.assertEqual(e.r32(SOUNDS + 0x200 + 72), 0)          # as the cartridge has it
        self.assertEqual(self.cart(e, 2), b''.join(vs))
        self.assertEqual(bytes(e.uc.mem_read(SOUNDS + 0x200 + 4, 11)), b'DX7ABCD1234')
        e.w32(s['sel_hash'], 0x11111111)                 # a file that is no cartridge
        d.add(0x11111111, b'hello' * 10)
        e.run('preset_fail_hook', regs={D[0]: 1}, stack=[0, 0, 2])
        self.assertEqual(rd(), 'Not a DX7\ncartridge')


def fnv(data):
    h = 0x811c9dc5
    for c in data:
        h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h


class TSysexReceive(unittest.TestCase):
    MSG = SCRATCH + 0x80000

    def send(self, e, msg, port=0x10):
        e.uc.mem_write(self.MSG, msg)
        posts = []
        e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: posts.append(1),
                      begin=0x40001fba, end=0x40001fba)
        hit = e.run_to_any('dx_sysex', (0x400812f6, RET), stack=[self.MSG, len(msg), port])
        return hit, len(posts)

    def test_dumps_are_kept_and_the_rest_goes_on(self):
        vs = [G.voice('E.PIANO MT', [op(ol=99)])] + [G.voice('V%d' % i, [op()]) for i in range(31)]
        for port in (0x10, 0x04):                       # MIDI IN, USB
            e = emu(); s = e.sym
            hit, posts = self.send(e, bulk(vs), port)
            self.assertEqual((hit, posts), (RET, 1))
            self.assertEqual(e.r32(s['dx_rxlen']), 4104)
            self.assertEqual(bytes(e.uc.mem_read(s['dx_base'] + 0xC000, 4104)), bulk(vs))
        e = emu(); s = e.sym
        one_v = bytes([0xf0, 0x43, 0x05, 0x00, 0x01, 0x1b]) + vced('LEAD 1', [op(ol=99)]) + b'\x00\xf7'
        self.assertEqual(self.send(e, one_v), (RET, 1))
        self.assertEqual(e.r32(s['dx_rxlen']), 163)
        for msg in (bytes([0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7]),          # identity: stock
                    bytes([0xf0, 0x00, 0x20, 0x3c, 0x10, 0x00, 0x01, 0xf7]),  # Elektron's
                    bytes([0xf0, 0x43, 0x10, 0x09, 0x20, 0x00]) + bytes(4096) + b'\x00\xf7',  # n >= 16
                    bulk(vs)[:-10] + b'\xf7',                            # short
                    bytes([0xf0, 0x43, 0x00, 0x01, 0x20, 0x00]) + bytes(4096) + b'\x00\xf7'):  # format 1
            e = emu()
            self.assertEqual(self.send(e, msg), (0x400812f6, 0), msg[:6].hex())
            self.assertEqual(e.r32(e.sym['dx_rxlen']), 0)
        e = emu(); s = e.sym                            # one at a time
        self.send(e, bulk(vs))
        other = bulk([G.voice('OTHER', [op()])] * 32)
        self.assertEqual(self.send(e, other), (RET, 0))
        self.assertEqual(bytes(e.uc.mem_read(s['dx_base'] + 0xC000, 4104)), bulk(vs))

    def test_file_name_and_write(self):
        vs = [G.voice('E.PIANO/MT', [op(ol=99)])] + [G.voice('V', [op()])] * 31
        e = emu(); s = e.sym
        self.send(e, bulk(vs))
        calls = {}
        for fn in (0x4007bc5e, 0x4007bb0a, 0x4007bc1c):
            e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, z, u: calls.setdefault(a, struct.unpack(
                '>3I', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 12))), begin=fn, end=fn)
        e.uc.mem_write(0x4007bc5e, bytes.fromhex('70004e75'))   # open: ok
        e.run('dx_rx_write', stack=[0, 0])
        path = bytes(e.uc.mem_read(calls[0x4007bc5e][0], 40)).split(b'\0')[0].decode()
        h = fnv(b''.join(vs)) & 0xffff
        self.assertEqual(path, '/DX7/E.PIANO_MT %04X.syx' % h)
        self.assertEqual(calls[0x4007bb0a][:2], (s['dx_base'] + 0xC000, 4104))   # all of it
        self.assertIn(0x4007bc1c, calls)                                         # closed
        self.assertEqual(e.r32(s['dx_rxlen']), 0)
        e = emu(); s = e.sym                            # one voice, a blank name
        one_v = bytes([0xf0, 0x43, 0, 0, 1, 0x1b]) + vced('', [op(ol=99)]) + b'\x00\xf7'
        self.send(e, one_v)
        e.run('dx_rx_path')
        name = bytes(e.uc.mem_read(e.d(0) if False else s['dx_rxpath'], 40)).split(b'\0')[0].decode()
        self.assertEqual(name, '/DX7/CART %04X.syx' % (fnv(one_v[6:161]) & 0xffff))

    def test_hook_installed_in_the_dispatch_table(self):
        b = open(os.path.join(REPO, 'build.py')).read()
        self.assertIn("PHASE1.append((0x40112950,'400812f6',_p('dx_sysex')))", b)


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
        self.assertEqual(e.r8(e.sym['dx_vel'] + 3), 0)           # the velocity word: 0 here
        e.w16(0x40fe4cdc + 6, 93 << 8)                          # the note's velocity, 93
        V.run_pre(e, 3, DX, trig=True, words=words)
        self.assertEqual(e.r8(e.sym['dx_vel'] + 3), 93)
        e.w16(0x40fe4cdc + 6, 0xffff)                           # negative: 0
        V.run_pre(e, 3, DX, trig=True, words=words)
        self.assertEqual(e.r8(e.sym['dx_vel'] + 3), 0)
        self.assertEqual((e.visits['dx_params'], e.visits['mi_params'], e.visits['dr_params'],
                          e.visits['va_params']), (3, 0, 0, 0))          # three runs above

    def test_algorithm_on_settings_shape(self):
        """SETTINGS held on a DX track: Shape's slot (position 3) carries the
        Algorithm parameter (0x0a, k=26); not on other machines, not when
        released. Its word picks the algorithm, and turning it under SETTINGS
        is a chord (no Config Menu on release)."""
        import test_va as V
        e = emu(); s = e.sym
        sd = s['sampler_descr']
        e.uc.mem_write(sd, bytes(range(76)))
        e.w8(s['descr_done'], 1)
        e.w32(s['set_held'], 1)
        e.run_to_any('descr_hook', (s['table_lookup_b_fixed'], RET), stack=[DX + 1])
        self.assertEqual(e.d(0), s['alg_descr'])
        d = bytes(e.uc.mem_read(s['alg_descr'], 76))
        self.assertEqual(d[:20] + d[24:], bytes(range(76))[:20] + bytes(range(76))[24:])
        self.assertEqual(struct.unpack('>I', d[20:24])[0], 0x0a)
        e.run('descr_b_hook', RET, stack=[DX])
        self.assertEqual(e.d(0), s['alg_descr'])
        e.run_to_any('descr_hook', (s['table_lookup_b_fixed'], RET), stack=[12])   # PLAITS
        self.assertEqual(e.d(0), sd)
        e.w32(s['set_held'], 0)
        e.run_to_any('descr_hook', (s['table_lookup_b_fixed'], RET), stack=[DX + 1])
        self.assertEqual(e.d(0), sd)
        e.run('descr_b_hook', RET, stack=[DX])
        self.assertEqual(e.d(0), sd)
        # the word -> dx_algo, a chord only under SETTINGS
        for w, alg, held in ((0, 0, 0), (3 << 8, 0, 1), (4 << 8, 1, 1), (64 << 8, 16, 0),
                             (127 << 8, 32, 1), (0xffff, 0, 0)):
            e.w32(s['mod_used'], 0)
            e.w32(s['set_held'], held)
            e.w16(V.td(3) + 52, w)
            V.run_pre(e, 3, DX, trig=False)
            self.assertEqual(e.r8(s['dx_algo'] + 3), alg, w)
            self.assertEqual(e.r32(s['mod_used']), held if alg else 0, w)
        e.w32(s['set_held'], 0)
        e.w8(s['dx_algo'] + 3, 0)

    def test_algorithm_override_plays(self):
        def out(alg):
            e = emu()
            load(e, 0, one(alg=1, op1=op(ol=99), op3=op(ol=99)))   # alg 1: carriers 1, 3
            e.w8(e.sym['dx_algo'], alg)
            x = render(e, 40)
            e.w8(e.sym['dx_algo'], 0)
            return x
        self.assertTrue(np.array_equal(out(0), out(1)))         # 1 = the voice's own here
        self.assertFalse(np.array_equal(out(0), out(3)))        # alg 3: OP3 modulates
        e = emu()
        load(e, 0, one(alg=1, op4=op(ol=99)))                   # OP4: a modulator in 1...
        e.w8(e.sym['dx_algo'], 32)                              # ...a carrier in 32
        self.assertGreater(np.abs(render(e, 40)[1000:]).max(), 6000)
        e.w8(e.sym['dx_algo'], 0)

    def test_algorithm_values_and_commit(self):
        import test_va as V
        e = emu(); s = e.sym
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        def fmt(v):
            e.run('dx_algfmt', 0x40000e6e, stack=[0x1000, v, 0x2000])
            return rd(e.r32(e.sp() + 12))
        self.assertEqual([fmt(v << 8) for v in (0, 3, 4, 64, 127)], ['VCE', 'VCE', 'A1', 'A16', 'A32'])
        T = V.T6Commit()                                         # a track becomes DX:
        e.uc.mem_write(T.STUB, b'\x20\x3c' + struct.pack('>I', T.SOUND) + b'\x4e\x75')
        vt = SCRATCH + 0x43000
        e.w32(vt + 40, T.STUB)
        e.w32(T.OBJ, vt)
        e.uc.mem_write(T.SOUND, bytes(100))
        e.w8(T.SOUND + 38, DX)
        e.w32(T.SOUND + 72, 0x12345678)                          # a Sampler's state
        e.run('mc_commit_hook', 0x40014144, regs={A[2]: T.OBJ})
        self.assertEqual(e.r32(T.SOUND + 72), 0)                 # the voice's algorithm

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
                         ['BRIG', 'TIME', 'FDBK', 'VOIC'])
        self.assertEqual([rd(e.r32(pt + off + 44)) for off in (2576, 3472, 4088, 4144)],
                         ['Bright', 'Env Time', 'Feedback', 'Voice'])
        self.assertEqual((e.r32(CONTOUR), e.r32(SWEEP), e.r32(COLOR)),
                         (s['dx_vfmt'], s['dx_offmt'], s['dx_offmt']))
        self.assertEqual(e.r32(0x40a73460 + 12), s['dx_draw'])   # the knob screen's
        e.run_to_any('descr_b_hook', (s['table_lookup_b_fixed'], RET), stack=[11])  # PLAITS
        self.assertEqual((e.r32(CONTOUR), e.r32(SWEEP)), (s['mi_eng_fmt'], 0x11112222))
        self.assertEqual(e.r32(0x40a73460 + 12), s['mi_draw'])
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
        self.assertEqual([e.r16(T.SOUND + 42 + 2 * i) >> 8 for i in range(4)], [64, 64, 64, 0])

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
