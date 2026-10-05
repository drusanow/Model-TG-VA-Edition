#!/usr/bin/env python3
"""Host tests for the VA machine (machine 7) and the machine-index hooks it
touches. They run the ASSEMBLED blob on Unicorn's ColdFire V4e (tests/va_emu.py)
and compare it with a bit-exact Python model. HOST/STATIC verification only:
none of this replaces a test on a Model:Cycles.

    pip install unicorn numpy
    python3 -m unittest discover -s tests -v

Set MODEL_CYCLES_STOCK=path/to/model-cycles_OS1.13.syx (and ELEKTRON_FIRMWARE_TOOL
if it is not on PATH) to also run the full-build checks against the stock OS.
"""
import os, re, struct, subprocess, sys, unittest, random, hashlib, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa: F401,F403

EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


# scratch layout inside SCRATCH
TD = SCRATCH + 0x1000          # trackData, 0x100 per track
VOICE = SCRATCH + 0x10000      # voices, 0x1000 per track
OUT = SCRATCH + 0x20000        # outBufs, 0x100 per track
FLAGS = SCRATCH + 0x30000      # a3 (last block) / a5 (this block) trigger flags
DEC = SCRATCH + 0x31000        # decimator state the voices point at


def td(t): return TD + 0x100 * t
def voice(t): return VOICE + 0x1000 * t


def set_va_words(e, t, w1, w2, det, mix):
    for i, v in enumerate((w1, w2, det, mix)):
        e.w16(td(t) + 22 + 2 * i, v)


# regions va_fill may write; everything else in the blob must be unchanged
def va_writable(e):
    s = e.sym
    return [(s['gran_acc'], s['gran_acc'] + 256),
            (s['sampler_buf'] + 32, s['sampler_buf'] + 32 + 256),
            (s['wv_ph'], s['wv_ph'] + 72), (s['wv_trig'], s['wv_trig'] + 6),
            (s['va_drift'], s['va_drift'] + 48), (s['va_seed'], s['va_seed'] + 4)]


def changed_outside(e, before, allowed):
    after = e.blob_now()
    bad = []
    for i in range(len(before)):
        if before[i] != after[i]:
            a = BLOB + i
            if not any(lo <= a < hi for lo, hi in allowed):
                bad.append(a)
    return bad


class T1Params(unittest.TestCase):
    """va_params: the four dials -> waveform codes, ratio, mix."""
    def test_mapping_edges(self):
        e = emu(); ref = VARef()
        cases = [(0, 0, 0, 0), (42 << 8, 43 << 8, 64 << 8, 64 << 8),
                 (85 << 8, 86 << 8, 127 << 8, 127 << 8), (32512, 32767, 32512, 32767),
                 (0xffff, 0x8000, 0xff00, 0xfffe),          # negative words -> 0
                 (100 << 8, 10 << 8, 68 << 8, 1 << 8)]
        for t, words in enumerate(cases[:6]):
            set_va_words(e, t, *words)
            e.run('va_params', regs={D[2]: t, A[2]: td(t)})
            ref.params(t, words)
            got = [e.r32(e.arr(n, t)) for n in ('va_w1', 'va_w2', 'va_rat', 'va_mix')]
            self.assertEqual(got, [ref.w1[t], ref.w2[t], ref.rat[t], ref.mix[t]], words)
        self.assertEqual(ref.rat[1], 65536)        # DTUN 64: unison
        self.assertEqual(ref.mix[1], 128)          # MIX 64: even
        self.assertEqual(ref.mix[2], 256)          # MIX 127: OSC 2 alone
        self.assertEqual((ref.w1[1], ref.w2[1]), (0, 1))
        self.assertEqual((ref.w1[2], ref.w2[2]), (1, 2))

    def test_detune_table(self):
        t = det_table()
        self.assertEqual(len(t), 128)
        self.assertEqual((t[0], t[64], t[127]), (32768, 65536, 131072))
        self.assertTrue(all(a < b for a, b in zip(t, t[1:])))


class T2Fill(unittest.TestCase):
    """va_fill is bit-exact with the model, for every waveform pair, many
    pitches, across blocks, with triggers - and writes only its own state."""
    def test_bit_exact_and_contained(self):
        e = emu(); ref = VARef(); rnd = random.Random(7)
        before = e.blob_now()
        steps = [0, 0x800, 0x4000, 0x10000, 0x2d413, 0x80000, 0x200000, 0x300000]
        n = 0
        for blk in range(60):
            t = blk % 6
            words = (rnd.randrange(0, 32768), rnd.randrange(0, 32768),
                     rnd.randrange(0, 32768), rnd.randrange(0, 32768))
            set_va_words(e, t, *words)
            e.run('va_params', regs={D[2]: t, A[2]: td(t)})
            ref.params(t, words)
            if rnd.random() < 0.25:
                e.w8(e.arr('wv_trig', t, 1), 1); ref.trig[t] = 1
            step = steps[blk % len(steps)]
            e.run('va_fill', stack=[t, step])
            exp, _, _ = ref.fill(t, step)
            got = [e.r32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
            self.assertEqual(got, exp, f"block {blk} track {t} step {step:#x} words {words}")
            self.assertEqual([e.r32(e.arr('wv_ph', 3 * t + k)) for k in (0, 1)], ref.ph[t][:2])
            n += 1
        allowed = va_writable(e) + [(e.sym['va_w1'], e.sym['va_mix'] + 24)]
        self.assertEqual(changed_outside(e, before, allowed), [])

    def test_every_waveform_pair_and_mix(self):
        e = emu(); ref = VARef()
        for w1 in (0, 50, 100):
            for w2 in (0, 50, 100):
                for mix in (0, 64, 127):
                    words = (w1 << 8, w2 << 8, 70 << 8, mix << 8)
                    set_va_words(e, 0, *words); ref.params(0, words)
                    e.run('va_params', regs={D[2]: 0, A[2]: td(0)})
                    for step in (0x10000, 0x180000):
                        e.run('va_fill', stack=[0, step])
                        exp, _, _ = ref.fill(0, step)
                        got = [e.r32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
                        self.assertEqual(got, exp, (w1, w2, mix, step))

    def test_output_range(self):
        ref = VARef(); worst = 0
        for w1 in (0, 1, 2):
            for w2 in (0, 1, 2):
                ref.w1[0], ref.w2[0], ref.mix[0], ref.rat[0] = w1, w2, 128, 65536
                ref.drift[0] = [0, 0]; ref.ph[0] = [0, 0, 0]
                for step in (0x1000, 0x10000, 0x200000):
                    for _ in range(40):
                        out, _, _ = ref.fill(0, step)
                        worst = max(worst, max(abs(s32(o)) for o in out))
        self.assertLessEqual(worst, 32767 << 15)

    def test_pitch(self):
        """Note 60 (step 1.0) plays at 261.63 Hz."""
        ref = VARef(); ref.mix[0] = 0; ref.drift[0] = [0, 0]
        ref.trig[0] = 1
        out = []
        for _ in range(1500):                        # 1500 blocks = 1 s
            ref.drift[0] = [0, 0]
            o, _, _ = ref.fill(0, 0x10000)
            out += [s32(x) >> 15 for x in o]
        # one rising zero crossing a cycle, mid-ramp (the reset falls)
        rises = sum(1 for a, b in zip(out, out[1:]) if a < 0 <= b)
        self.assertTrue(261 <= rises <= 262, rises)  # 96000 samples, 261.6 cycles


class T3Aliasing(unittest.TestCase):
    """PolyBLEP: a high saw has much less energy at non-harmonic frequencies
    than the naive one. (Quality, not safety - skipped without numpy.)"""
    def test_blep_reduces_aliasing(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("numpy")
        ref = VARef(); ref.mix[0] = 0
        step = 0x10000 * 12                            # ~3.1 kHz at 96 kHz
        x = []
        for _ in range(200):
            ref.drift[0] = [0, 0]
            o, inc, _ = ref.fill(0, step)
            x += [s32(v) >> 15 for v in o]
        x = np.array(x[:8192], float)
        f0 = inc / 2 ** 32 * 96000
        naive = np.array([((i * inc + 0x80000000) % 2 ** 32) / 2 ** 16 - 32768
                          for i in range(8192)], float)

        def alias_ratio(sig):
            w = np.hanning(len(sig)); S = np.abs(np.fft.rfft(sig * w)) ** 2
            f = np.fft.rfftfreq(len(sig), 1 / 96000)
            h = np.zeros_like(f, bool)
            for k in range(1, int(48000 / f0) + 1):
                h |= np.abs(f - k * f0) < 40
            band = f < 20000
            return S[band & ~h].sum() / S[band & h].sum()
        a_naive, a_blep = alias_ratio(naive), alias_ratio(x)
        self.assertLess(a_blep, a_naive / 10, (a_naive, a_blep))


class T4Gates(unittest.TestCase):
    """The LFO and Amp Decay gates and the descriptor lookups for machine 7."""
    def test_lfo_gate(self):
        e = emu()
        ids = [e.r32(e.sym['sampler_lfo_ids'] + 4 * i) for i in range(4)]
        for grp in (6, 7, 8, 9, 10):                # the Sampler, the VA, the drums
            for idx in range(11, 15):
                e.run('sampler_lfo_gate', 0x4005a6fc, regs={D[2]: grp, A[2]: idx})
                self.assertEqual(e.d(0), ids[idx - 11])
            for idx in (10, 15, 18):
                e.run('sampler_lfo_gate', 0x4005a6ac, regs={D[2]: grp, A[2]: idx})
        for grp in (11, 100, -1):
            e.run('sampler_lfo_gate', 0x4005a6ac, regs={D[2]: grp, A[2]: 12})
        for grp in range(6):
            e.run('sampler_lfo_gate', 0x4005a6dc, regs={D[2]: grp, A[2]: 12})

    def test_amp_gate(self):
        e = emu()
        for grp in (6, 7, 8, 9, 10):
            e.run('sampler_amp_gate', 0x4005a6fc, regs={D[2]: grp})
            self.assertEqual(e.d(0), 0x4b)
        for grp in range(6):
            e.w32(0x40a79418 + grp * 32, 0x100 + grp)   # lsll #5: 32 bytes a row
            e.run('sampler_amp_gate', 0x4005a6fc, regs={D[2]: grp})
            self.assertEqual(e.d(0), 0x100 + grp)

    def test_com_gate_gives_va_the_common_list(self):
        e = emu()
        for idx in (23, 24, 25):                        # Attack, Filter, Resonance
            e.w32(0x40a79394 + 4 * idx, 0x0d + idx)
            e.run('sampler_com_gate', 0x4005a6fc, regs={D[2]: 7, A[2]: idx})
            self.assertEqual(e.d(0), 0x0d + idx)
        e.run('sampler_com_gate', 0x4005a6ac, regs={D[2]: 6, A[2]: 24})  # Sampler: none

    def test_table_lookups(self):
        e = emu()
        exp_a = {i: 0x40a71540 + 76 * i for i in range(7)}
        for i in range(7, 12):                      # Sampler, VA, drums: machine+1
            exp_a[i] = 0x40a71540 + 76
        for i, v in exp_a.items():
            e.run('table_lookup_a_fixed', stack=[i])
            self.assertEqual(e.d(0), v, i)
        e.run('table_lookup_a_fixed', stack=[12])
        self.assertEqual(e.d(0), (0x40a71540 - 76) & M32)       # stock: out of range
        for i in range(6):
            e.w8(0x401091b4 + i, i)
        for i in range(11):
            e.run('table_lookup_b_fixed', stack=[i])
            self.assertEqual(e.d(0), 0x40a71540 + 76 * (i if i < 6 else 0), i)

    def _names(self, e):
        return [e.r32(0x4010dce0 + off + 44) for off in (2576, 3472, 4088, 4144)]

    def test_descr_hooks_and_labels(self):
        e = emu(); s = e.sym
        va = [s['str_vo1'], s['str_vo2'], s['str_vdet'], s['str_vmix']]
        smp = [s['str_start'], s['str_end'], s['str_flt'], s['str_res']]
        stock = [0x4012999c, 0x40129a4d, 0x40129ab6, 0x40129ac2]
        e.w8(s['descr_done'], 1)
        e.w32(s['mod_held'], 0)
        e.run('descr_hook', stack=[8])
        self.assertEqual(e.d(0), s['sampler_descr'])
        self.assertEqual(self._names(e), va)
        self.assertEqual(e.r32(0x40a7296c), 0x400456c8)           # plain 0..127
        self.assertEqual(e.r32(0x40a72978), 0)                    # no fine_draw
        e.run('descr_hook', stack=[7])
        self.assertEqual(e.d(0), s['sampler_descr'])
        self.assertEqual(self._names(e), smp)
        self.assertEqual(e.r32(0x40a7296c), 0x4004a440)
        e.run('descr_hook', stack=[3])
        self.assertEqual(e.d(0), 0x40a71540 + 3 * 76)
        self.assertEqual(self._names(e), stock)
        e.w32(s['mod_held'], 1)
        e.run('descr_hook', stack=[8])
        self.assertEqual(e.d(0), s['va_descr_atk'])
        e.run('descr_hook', stack=[7])
        self.assertEqual(e.d(0), s['sampler_descr_atk'])
        # raw-machine lookup (LED painter, LFO list)
        e.w32(s['mod_held'], 0)
        e.run('descr_b_hook', stack=[7])
        self.assertEqual(e.d(0), s['sampler_descr'])
        self.assertEqual(self._names(e), va)
        e.w32(s['mod_held'], 1)
        e.run('descr_b_hook', stack=[7])
        self.assertEqual(e.d(0), s['va_descr_atk'])
        e.run('descr_b_hook', stack=[6])
        self.assertEqual(e.d(0), s['sampler_descr_atk'])
        self.assertEqual(self._names(e), smp)
        # before the audio thread has built them: the Kick alias, never garbage
        e.w8(s['descr_done'], 0); e.w32(s['mod_held'], 0)
        e.run('descr_hook', stack=[8])
        self.assertEqual(e.d(0), 0x40a71540 + 76)

    def test_va_descriptors_built(self):
        """sampler_pre's one-time build: VA variant = Sampler's with Attack at
        position 1 and Filter/Resonance at 4/5."""
        e = emu(); s = e.sym
        chord = [0x1000 + i for i in range(19)]
        for i, v in enumerate(chord):
            e.w32(0x40a71708 + 4 * i, v)
        run_pre(e, 0, 7, trig=False)
        d = lambda n: [e.r32(s[n] + 4 * i) for i in range(19)]
        smp, atk, va = d('sampler_descr'), d('sampler_descr_atk'), d('va_descr_atk')
        self.assertEqual(smp[2:8], [0x2a, 0x4b, 0x2e, 0x3e, 0x49, 0x4a])
        self.assertEqual(atk[2:8], [0x2a, 0x0d, 0x2e, 0x3e, 0x49, 0x4a])
        self.assertEqual(va[2:8], [0x2a, 0x0d, 0x2e, 0x3e, 0x0c, 0x0b])
        self.assertEqual(va[:2] + va[8:], chord[:2] + chord[8:])


def run_pre(e, t, machine, trig, last=False, words=None):
    """sampler_pre for track t: returns the address it handed on to."""
    e.w8(td(t) + 18, machine)
    if words:
        set_va_words(e, t, *words)
    e.w32(FLAGS + 4 * t, 1 if last else 0)
    e.w32(FLAGS + 0x100 + 4 * t, 1 if trig else 0)
    return e.run_to_any('sampler_pre', (0x400a7dcc, 0x400a7db0, 0x400a7de0),
                        regs={D[1]: 0, D[2]: t, A[2]: td(t), A[3]: FLAGS + 4 * t,
                              A[5]: FLAGS + 0x100 + 4 * t, A[6]: voice(t)}, limit=200000)


class T5PreDispatch(unittest.TestCase):
    """sampler_pre and sampler_dispatch route machine 7 to the VA, 6 to the
    Sampler and 0..5 to stock, and the VA reaches amp_hook."""
    def test_pre_routes(self):
        for m, exit_ in ((7, 0x400a7dcc), (6, 0x400a7dcc), (0, 0x400a7de0), (5, 0x400a7de0)):
            e = emu()
            e.watch('va_pre', 'va_params')
            self.assertEqual(run_pre(e, 1, m, trig=False), exit_, m)
            self.assertEqual(e.r32(e.arr('trk_mach', 1)), m)
            self.assertEqual(e.visits['va_pre'] > 0, m == 7, m)
            if m in (6, 7):
                self.assertEqual(e.r32(voice(1)), 6)      # the stock dispatch sees 6

    def test_pre_trigger_edge(self):
        e = emu()
        words = (90 << 8, 10 << 8, 80 << 8, 30 << 8)
        run_pre(e, 4, 7, trig=True, last=False, words=words)
        self.assertEqual(e.r8(e.arr('wv_trig', 4, 1)), 1)
        ref = VARef(); ref.params(4, words)
        self.assertEqual([e.r32(e.arr(n, 4)) for n in ('va_w1', 'va_w2', 'va_rat', 'va_mix')],
                         [ref.w1[4], ref.w2[4], ref.rat[4], ref.mix[4]])
        e.w8(e.arr('wv_trig', 4, 1), 0)
        run_pre(e, 4, 7, trig=True, last=True)            # held: no new edge
        self.assertEqual(e.r8(e.arr('wv_trig', 4, 1)), 0)
        run_pre(e, 4, 7, trig=False)
        self.assertEqual(e.r8(e.arr('wv_trig', 4, 1)), 0)

    def _dispatch(self, e, t, trig=True):
        v = voice(t)
        e.w32(v + 0x34, 1 if trig else 0)
        e.w32(v + 232, 45710)                              # note 60
        e.w32(v + 0x318, DEC + 0x40 * t)
        e.w32(e.arr('voice_ptr', t), v)
        e.run('sampler_dispatch', 0x400a7e24,
              regs={D[1]: 0, D[2]: t, D[3]: OUT + 0x100 * t, D[4]: 6, D[6]: 0x40118628,
                    A[6]: v, A[2]: td(t)})

    def test_dispatch_reaches_amp_hook(self):
        e = emu()
        e.watch('va_fill', 'amp_hook', 0x400a967a, 0x400a9f58, 0x400a9252, 0x400a9430,
                'gflt_run', 'sil_note', 'ladder_run', 'wave_fill')
        e.w32(e.arr('trk_mach', 2), 7)
        self._dispatch(e, 2)
        v = e.visits
        for n in ('va_fill', 'amp_hook', 0x400a967a, 0x400a9f58, 0x400a9252, 0x400a9430,
                  'gflt_run', 'sil_note'):
            self.assertEqual(v[n], 1, n)
        self.assertEqual(v['ladder_run'], 0)               # the Sampler's own filter: no
        self.assertEqual(v['wave_fill'], 0)

    def test_sampler_still_renders_as_sampler(self):
        e = emu()
        e.watch('va_fill', 'amp_hook', 'sampler_render')
        e.w32(e.arr('trk_mach', 2), 6)
        self._dispatch(e, 2)
        self.assertEqual((e.visits['va_fill'], e.visits['sampler_render'], e.visits['amp_hook']),
                         (0, 1, 1))

    def test_six_va_tracks(self):
        """Six VA tracks, every block, params changing: each track's output is
        the model's, and nothing outside the VA's state and the render's
        shared scratch changes."""
        e = emu(); ref = VARef(); rnd = random.Random(3); s = e.sym
        for t in range(6):
            e.w32(e.arr('trk_mach', t), 7)
        before = e.blob_now()
        pitches = [45710, 22855, 91420, 30000, 120000, 45710 * 3]
        for blk in range(24):
            for t in range(6):
                words = tuple(rnd.randrange(0, 32768) for _ in range(4))
                trig = blk == 0 or rnd.random() < 0.1
                run_pre(e, t, 7, trig=trig, last=False, words=words)
                ref.params(t, words)
                if trig:
                    ref.trig[t] = 1
                v = voice(t)
                e.w32(v + 0x34, 1); e.w32(v + 232, pitches[t])
                e.w32(v + 0x318, DEC + 0x40 * t); e.w32(e.arr('voice_ptr', t), v)
                e.run('sampler_dispatch', 0x400a7e24,
                      regs={D[1]: 0, D[2]: t, D[3]: OUT + 0x100 * t, D[4]: 6,
                            D[6]: 0x40118628, A[6]: v, A[2]: td(t)})
                step = ((pitches[t] >> 4) * 93957 & M32) >> 12
                exp, _, _ = ref.fill(t, step)
                got = [e.r32(s['sampler_buf'] + 32 + 4 * i) for i in range(64)]
                self.assertEqual(got, exp, (blk, t))
        # the render's and the hooks' own per-block state may change; VA code
        # and every other machine's data may not
        allowed = va_writable(e) + [(s['va_w1'], s['va_mix'] + 24),
                                    (s['dr_trig'], s['dr_trig'] + 8)]
        for n, size in (('trk_mach', 28), ('voice_ptr', 28), ('atk_key', 28), ('atk_step', 28),
                        ('atk_gain', 28), ('sil_cnt', 28), ('sil_pk', 28), ('trig_seen', 8),
                        ('sv_idle', 8), ('sx_on', 28), ('prof_trk', 28), ('prof_ta', 4),
                        ('sx_ok', 4), ('gflt_f', 28), ('gflt_q', 28), ('gflt_mk', 28),
                        ('gflt_y', 6 * 4 * 4 * 2), ('sldg_on', 64), ('descr_done', 1)):
            if n in s:
                allowed.append((s[n], s[n] + size))
        bad = changed_outside(e, before, allowed)
        names = sorted({max((v, k) for k, v in s.items() if v <= a)[1] for a in bad})
        # report by symbol: anything not obviously per-block render state fails
        unexpected = [n for n in names if not n.startswith(('gflt', 'gf_', 'sld', 'gc_', 'rs_',
                                                            'ah_', 'sil', 'vq', 'prof'))]
        if os.environ.get("VA_TEST_VERBOSE"):
            print("\n  changed outside the VA state:", names)
        self.assertEqual(unexpected, [], names)


class T6Commit(unittest.TestCase):
    """mc_commit_hook: a track switched to the VA gets the VA's defaults, to
    the Sampler the Sampler's, and other machines nothing."""
    STUB = SCRATCH + 0x40000
    SOUND = SCRATCH + 0x41000
    OBJ = SCRATCH + 0x42000

    def _commit(self, e, machine):
        e.uc.mem_write(self.STUB, b'\x20\x3c' + struct.pack('>I', self.SOUND) + b'\x4e\x75')
        vt = SCRATCH + 0x43000
        e.w32(vt + 40, self.STUB)
        e.w32(self.OBJ, vt)
        e.uc.mem_write(self.SOUND, bytes(100))
        e.w8(self.SOUND + 38, machine)
        for k in range(4):
            e.w16(self.SOUND + 42 + 2 * k, 0x1111)
        e.run('mc_commit_hook', 0x40014144, regs={A[2]: self.OBJ})
        return [e.r16(self.SOUND + 42 + 2 * k) for k in range(4)], e.r32(self.SOUND + 72)

    def test_defaults(self):
        e = emu()
        w, st = self._commit(e, 7)
        self.assertEqual(w, [0, 0, 68 << 8, 64 << 8])
        self.assertEqual(st, 0)                              # the Sampler's state word
        w, st = self._commit(e, 6)
        self.assertEqual(w, [0, 32512, 32512, 0])
        self.assertNotEqual(st, 0)
        w, st = self._commit(e, 3)
        self.assertEqual(w, [0x1111] * 4)


class T7Static(unittest.TestCase):
    """What the blob and build.py hold for machine 7."""
    def test_tables(self):
        e = emu(); s = e.sym
        self.assertEqual([e.r32(s['page_machine_list'] + 4 * i) for i in range(8)],
                         list(range(1, 9)))
        names = [e.r32(s['sampler_name_table'] + 4 * i) for i in range(8)]
        self.assertEqual(names[7], s['va_name_string'])
        self.assertEqual(bytes(e.uc.mem_read(s['va_name_string'], 3)), b'VA\0')
        for n, f in (('va_icon_a_pixels', 'va_icon_A_48x33.bin'),
                     ('va_icon_b_pixels', 'va_icon_B_34x34.bin')):
            want = open(os.path.join(REPO, 'src', 'va_icons', f), 'rb').read()
            got = bytes(e.uc.mem_read(s[n], len(want)))
            self.assertEqual(got, want)

    def test_build_patch_list(self):
        b = open(os.path.join(REPO, 'build.py')).read()
        self.assertIn('MACH_MAX=10', b)
        for site in ('0x400147a5', '0x400148ab', '0x400148b3', '0x4005a79d', '0x400a25e1',
                     '0x4010e5e6'):
            self.assertRegex(b, re.escape(f"({site},'05',_M)"))
        for site in ('0x4001bbd3', '0x4001bbe5', '0x4001bbf3'):
            self.assertIn(f"({site},'18',_L)", b)
        self.assertIn("(0x400a26a3,'50','43')", b)        # eleven markers
        self.assertIn("(0x400a26b4,'5980','5780')", b)
        self.assertIn("(0x400a26e6,'5e84','5c84')", b)
        self.assertIn("(0x400a7df5,'05','06')", b)   # the audio bound stays

    def test_generated_files_current(self):
        icons = [f'src/va_icons/{n}_icon_{k}.bin' for n in ('kick', 'snare', 'hihat')
                 for k in ('A_48x33', 'B_34x34')]
        for tool, outs in (('gen_va_tables.py', ['src/va_tables.inc']),
                           ('gen_drum_tables.py', ['src/drum_tables.inc']),
                           ('gen_drum_icons.py', icons),
                           ('gen_va_icons.py', ['src/va_icons/va_icon_A_48x33.bin',
                                                'src/va_icons/va_icon_B_34x34.bin'])):
            old = [open(os.path.join(REPO, o), 'rb').read() for o in outs]
            subprocess.run([sys.executable, os.path.join(REPO, 'tools', tool)],
                           check=True, capture_output=True)
            self.assertEqual(old, [open(os.path.join(REPO, o), 'rb').read() for o in outs], tool)


@unittest.skipUnless(os.environ.get('MODEL_CYCLES_STOCK'), "set MODEL_CYCLES_STOCK for full-build checks")
class T8FullBuild(unittest.TestCase):
    """A full build from the stock OS: the image's machine-count bytes, the
    marker loop, and the .syx unpacks again to the same section."""
    def test_full_build(self):
        stock = os.environ['MODEL_CYCLES_STOCK']
        tool = os.environ.get('ELEKTRON_FIRMWARE_TOOL', 'elektron-firmware-tool')
        out = os.path.join(BUILD, 'test-va.syx')
        r = subprocess.run([sys.executable, os.path.join(REPO, 'build.py'), '--stock', stock,
                            '--tool', tool, '--out', out], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        img = open(os.path.join(BUILD, 'Model-TG.bin'), 'rb').read()
        B = 0x40000400
        at = lambda a, n=1: img[a - B:a - B + n]
        for a in (0x400147a5, 0x400148ab, 0x400148b3, 0x4005a79d, 0x400a25e1, 0x4010e5e6):
            self.assertEqual(at(a), b'\x0a', hex(a))               # machines 0..10
        for a in (0x4001bbd3, 0x4001bbe5, 0x4001bbf3):
            self.assertEqual(at(a), b'\x2c', hex(a))               # 11 longs
        self.assertEqual(at(0x400a26a2, 2), b'\x78\x43')            # moveq #67,%d4
        self.assertEqual(at(0x400a26b4, 2), b'\x57\x80')            # subql #3: x-3..x
        self.assertEqual(at(0x400a26e6, 2), b'\x5c\x84')            # addql #6
        self.assertEqual(at(0x400a26e8, 2), b'\x70\x0b')            # moveq #11,%d0
        self.assertEqual(at(0x400a7df4, 2), b'\x70\x06')            # audio bound: 6
        # eleven markers inside the right panel: first 64..67, last 124..127
        self.assertEqual((67 - 3, 67 + 6 * 10), (64, 127))
        # the .syx opens again and holds this very section
        d = os.path.join(BUILD, 'test-va-unpack')
        shutil.rmtree(d, ignore_errors=True)
        subprocess.run([tool, '-i', out, '-d', '3', '-o', d], check=True, capture_output=True)
        self.assertEqual(open(os.path.join(d, 'section_3_MAIN_OS.bin'), 'rb').read(), img)


if __name__ == '__main__':
    unittest.main(verbosity=2)
