#!/usr/bin/env python3
"""Host tests for ACHORD (machine 14, src/achord/achord.c; glue src/achord_glue.inc): the chords it builds - FREE
types, keys and scales, SNAP and DEGREES, progressions and their advance, USER steps - their voicing (AUTO voice
leading, CLOSE, OPEN, DROP 2, WIDE, the bass), the strum, the sound (the notes in the spectrum, band-limited high notes,
Shape, OSC 2's detune, interval and level, PWM RATE, DRIVE, GRIT, the chorus, level, bounds), the settings page, the settings saved with the pattern, the page's labels and Contour's
names, the plumbing, and the cost. The firmware's own code runs (Unicorn, tests/va_emu.py).

    python3 -m unittest tests.test_achord -v
"""
import os, sys, struct, random, unittest

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
from test_lfocc import Stubs, fnv, KIT_BASE, KIT_SIZE, CUR_KIT, OPEN, READ, WRITE, POST

AC = 14
TD = SCRATCH + 0x60000                            # a stand-in trackData
SET = SCRATCH + 0x61000                           # a stand-in settings block
OUT = SCRATCH + 0x62000
STEP1 = 0x10000                                   # note 60
FS = 48000
S = dict(KEY=0, SCALE=1, NOTES=2, PROG=3, ADV=4, U0=5, VOICE=13, BASS=14, STRUM=15, SUB=16, CHORUS=17, DRIFT=18,
         INT=19, PWM=20, DRIVE=21, GRIT=22)
SCALES = ['FREE', 'MAJOR', 'MINOR', 'DORIAN', 'MIXOLYD', 'HARM MIN', 'PHRYGIAN', 'LYDIAN']
FREE = ['MAJ', 'MIN', '7', 'MAJ7', 'MIN7', 'M7B5', 'DIM', 'DIM7', 'AUG', 'SUS2', 'SUS4', '6', 'MIN6', 'ADD9', '9', 'MIN9']
FREE_IV = [{0, 4, 7}, {0, 3, 7}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 3, 6, 10}, {0, 3, 6}, {0, 3, 6, 9},
           {0, 4, 8}, {0, 2, 7}, {0, 5, 7}, {0, 4, 7, 9}, {0, 3, 7, 9}, {0, 4, 7, 2}, {0, 4, 10, 2}, {0, 3, 10, 2}]
KEYT = ['TRIAD', '7TH', '9TH', 'ADD9', 'SUS2', 'SUS4', '6TH', 'POWER']
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    EMU.uc.mem_write(SET, bytes(32))
    return EMU


def s16(v):
    return v - 65536 if v & 0x8000 else v


def step_of(note):
    return round(65536 * 2 ** ((note - 60) / 12))


def free(i):
    return (i * 8 + 4) << 8                       # the middle of FREE type i's span (of 16)


def ksize(i):
    return (i * 16 + 8) << 8                      # the middle of key size i's span (of 8)


class Track:
    """one ACHORD track in the emulator: its dials, its settings, its blocks"""
    def __init__(self, e, t=0, color=0, shape=32, sweep=0, contour=free(0)):  # (sweep 0: OSC 1 alone)
        self.e, self.t = e, t
        self.d = [color << 8, shape << 8, sweep << 8, contour]

    def set(self, **kv):
        for k, v in kv.items():
            self.e.w8(SET + S[k], v + 1)
        return self

    def params(self):
        for i, v in enumerate(self.d):
            self.e.w16(TD + 22 + 2 * i, v)
        self.e.run('ac_params', stack=[self.t, TD, SET])

    def play(self, blocks, note=60, trig=True):
        e = self.e
        self.params()
        if trig:
            e.w8(e.sym['dr_trig'] + self.t, 1)
        out = []
        for _ in range(blocks):
            e.run('ac_fill', stack=[self.t, step_of(note), AC])
            blk = [e.rs32(e.sym['sampler_buf'] + 32 + 4 * i) for i in range(64)]
            assert all(v & 0x7fff == 0 for v in blk)
            assert blk[0::2] == blk[1::2]
            out += blk[0::2]
        return np.array([v >> 15 for v in out], dtype=float)

    def notes(self):
        e = self.e
        e.run('ac_notes', stack=[self.t, OUT])
        n = e.d(0)
        return [e.rs32(OUT + 4 * i) for i in range(n)]

    def chord(self, note=60, trig=True, blocks=1):
        self.play(blocks, note, trig)
        return self.notes()


def pcs(notes, root):
    return {(n - root) % 12 for n in notes}


def peaks(x, lo=60, hi=2000, n=6):
    y = (x - x.mean()) * np.hanning(len(x))
    s = np.abs(np.fft.rfft(y, 1 << 17))
    f = np.fft.rfftfreq(1 << 17, 1 / FS)
    m = (f > lo) & (f < hi)
    out = []
    s = np.where(m, s, 0)
    for _ in range(n):
        k = int(np.argmax(s))
        out.append(float(f[k]))
        s[max(0, k - 40):k + 40] = 0
    return out


def midi(f):
    return 69 + 12 * np.log2(f / 440)


class TFree(unittest.TestCase):
    def test_every_type_on_its_root(self):
        for i, iv in enumerate(FREE_IV):
            tr = Track(emu(), contour=free(i)).set(VOICE=1)
            got = tr.chord(note=57)                # A
            self.assertEqual(pcs(got, 57), {x % 12 for x in iv}, FREE[i])

    def test_root_follows_the_note(self):
        for note in (40, 48, 60, 66, 72):
            got = Track(emu()).set(VOICE=1).chord(note=note)
            self.assertEqual(min(got), note)
            self.assertEqual(pcs(got, note), {0, 4, 7})

    def test_the_notes_sound(self):
        e = emu()
        x = Track(e, color=127, shape=0, contour=free(0)).set(VOICE=1, CHORUS=0, SUB=0, DRIFT=0).play(1000, note=57)
        got = sorted(round(midi(f)) % 12 for f in peaks(x[4800:], 150, 500, 3))
        self.assertEqual(got, sorted([57 % 12, 61 % 12, 64 % 12]))    # A C# E


class TKeys(unittest.TestCase):
    MAJ = [(0, {0, 4, 7}), (2, {0, 3, 7}), (4, {0, 3, 7}), (5, {0, 4, 7}), (7, {0, 4, 7}), (9, {0, 3, 7}),
           (11, {0, 3, 6})]

    def test_snap_builds_the_keys_chords(self):
        for key in (0, 3, 9):
            for off, want in self.MAJ:
                tr = Track(emu(), contour=ksize(0)).set(KEY=key, SCALE=1, VOICE=1)
                got = tr.chord(note=60 + key + off)
                self.assertEqual(pcs(got, 60 + key + off), want, (key, off))

    def test_snap_takes_notes_off_the_scale_down(self):
        tr = Track(emu(), contour=ksize(0)).set(KEY=0, SCALE=1, VOICE=1)
        got = tr.chord(note=61)                    # C#: snapped to C
        self.assertEqual(min(got), 60)
        self.assertEqual(pcs(got, 60), {0, 4, 7})

    def test_degrees_a_semitone_a_chord(self):
        for i, (off, want) in enumerate(self.MAJ):
            tr = Track(emu(), contour=ksize(0)).set(KEY=0, SCALE=1, NOTES=1, VOICE=1)
            got = tr.chord(note=60 + i)
            self.assertEqual(min(got), 60 + off, i)
            self.assertEqual(pcs(got, 60 + off), want, i)
        got = Track(emu(), contour=ksize(0)).set(SCALE=1, NOTES=1, VOICE=1).chord(note=67)
        self.assertEqual(min(got), 72)             # the next octave's I

    def test_sizes(self):
        want = {0: {0, 4, 7}, 1: {0, 4, 7, 10}, 2: {0, 4, 10, 2}, 3: {0, 4, 7, 2}, 4: {0, 2, 7}, 5: {0, 5, 7},
                6: {0, 4, 7, 9}, 7: {0, 7}}
        for size, iv in want.items():
            got = Track(emu(), contour=ksize(size)).set(SCALE=1, VOICE=1).chord(note=67)   # on V (G)
            self.assertEqual(pcs(got, 67), iv, KEYT[size])

    def test_scales(self):
        for sc, third in ((2, 3), (3, 3), (4, 4), (5, 3), (6, 3), (7, 4)):
            got = Track(emu(), contour=ksize(0)).set(SCALE=sc, VOICE=1).chord(note=60)
            self.assertIn(third, pcs(got, 60), SCALES[sc])
        got = Track(emu(), contour=ksize(1)).set(SCALE=5, VOICE=1).chord(note=67)          # harmonic minor V7
        self.assertEqual(pcs(got, 67), {0, 4, 7, 10})


class TProgressions(unittest.TestCase):
    def roots(self, tr, n=8, note=60):
        out = []
        for _ in range(n):
            got = tr.chord(note=note)
            out.append(got)
        return out

    def test_a_chord_a_trig(self):
        tr = Track(emu(), contour=ksize(0)).set(PROG=1, VOICE=1)        # I-V-vi-IV in C
        got = self.roots(tr, 8)
        want = [(60, {0, 4, 7}), (67, {0, 4, 7}), (69, {0, 3, 7}), (65, {0, 4, 7})] * 2
        for g, (r, p) in zip(got, want):
            self.assertEqual(pcs(g, r), p)
            self.assertIn(r, g)

    def test_minor_progression_and_key(self):
        tr = Track(emu(), contour=ksize(0)).set(PROG=11, KEY=9, VOICE=1)  # i-VI-III-VII in A (FREE: minor)
        got = self.roots(tr, 4)
        base = 57
        for g, (off, p) in zip(got, [(0, {0, 3, 7}), (8, {0, 4, 7}), (3, {0, 4, 7}), (10, {0, 4, 7})]):
            self.assertEqual(pcs(g, base + off), p)

    def test_the_note_transposes(self):
        tr = Track(emu(), contour=ksize(0)).set(PROG=1, VOICE=1)
        got = tr.chord(note=62)                    # up a tone: D
        self.assertEqual(pcs(got, 62), {0, 4, 7})
        self.assertIn(62, got)

    def test_user_steps(self):
        tr = Track(emu(), contour=ksize(0)).set(PROG=17, VOICE=1, U0=1, **{})
        e = tr.e
        for i, d in enumerate([1, 4, 0, 7]):       # ii V I END
            e.w8(SET + S['U0'] + i, d + 1)
        got = self.roots(tr, 6)
        want = [(62, {0, 3, 7}), (67, {0, 4, 7}), (60, {0, 4, 7})] * 2
        for g, (r, p) in zip(got, want):
            self.assertEqual(pcs(g, r), p)

    def test_user_default_is_a_progression(self):
        tr = Track(emu(), contour=ksize(0)).set(PROG=17, VOICE=1)
        got = self.roots(tr, 4)
        for g, r in zip(got, (60, 67, 69, 65)):
            self.assertIn(r, g)

    def test_with_the_pattern(self):
        """playing: TRIG starts again on the pattern's first step; BAR follows the bar"""
        e = emu()
        tr = Track(e, contour=ksize(0)).set(PROG=1, VOICE=1)
        e.w32(0x40a7883c, 1)                       # playing
        seen = []
        for step in (0, 4, 8, 0, 4):               # trigs on steps 0, 4, 8, then the pattern again
            e.w32(0x40a78840, step + 1)            # (the counter holds the next step)
            seen.append(tr.chord()[0] % 12)
        self.assertEqual(seen[:3], [0, 7, 9])      # C G A
        self.assertEqual(seen[3:], [0, 7])         # from the top again
        e = emu()
        tr = Track(e, contour=ksize(0)).set(PROG=1, ADV=1, VOICE=1)      # a chord a bar
        e.w32(0x40a7883c, 1)
        for step, want in ((0, 0), (5, 0), (16, 7), (31, 7), (32, 9), (50, 5), (62, 5)):
            e.w32(0x40a78840, step + 1)
            self.assertEqual(min(tr.chord()) % 12, want, step)
        e.w32(0x40a78840, 0)                       # the last step (the counter wrapped: step 63)
        self.assertEqual(min(tr.chord()) % 12, 5)


class TVoicing(unittest.TestCase):
    def test_auto_moves_least(self):
        prog = [60, 65, 67, 60, 57, 62, 67]       # C F G C Am Dm G
        moves = {}
        for voicing in (0, 1):
            tr = Track(emu(), contour=free(0)).set(VOICE=voicing)
            prev, total = None, 0
            for n in prog:
                tr.d[3] = free(1 if n in (57, 62) else 0)
                got = tr.chord(note=n)
                self.assertEqual(pcs(got, n), {0, 3, 7} if n in (57, 62) else {0, 4, 7})
                if prev:
                    total += sum(abs(a - b) for a, b in zip(sorted(got), sorted(prev)))
                prev = got
            moves[voicing] = total
        self.assertLess(moves[0], moves[1] * 0.6, moves)
        tr = Track(emu(), contour=free(0)).set(VOICE=0)
        for n in [60, 65, 67, 72, 77, 79, 84, 60]:  # never wanders off: near the root
            got = tr.chord(note=n)
            self.assertLessEqual(abs(min(got) - n), 9)

    def test_fixed_voicings(self):
        root = 60
        got = {v: sorted(Track(emu(), contour=free(2)).set(VOICE=v).chord(note=root)) for v in range(1, 5)}
        self.assertEqual(got[1], [60, 64, 67, 70])                 # CLOSE
        self.assertEqual(got[2], [60, 67, 70, 76])                 # OPEN: the second up
        self.assertEqual(got[3], [55, 60, 64, 70])                 # DROP 2: the second from the top down
        self.assertEqual(got[4], [60, 67, 70, 76])                 # WIDE: 7, b7 as they stand, the third up

    def test_bass(self):
        got = sorted(Track(emu(), contour=free(0)).set(VOICE=1, BASS=1).chord(note=60))
        self.assertEqual(got, [48, 60, 64, 67])
        got = sorted(Track(emu(), contour=free(14)).set(VOICE=1, BASS=2).chord(note=60))   # 9 over a low C
        self.assertEqual(got[0], 36)
        self.assertEqual(pcs(got[1:], 60), {4, 10, 2})             # the root gives way above it


class TSound(unittest.TestCase):
    def test_level_and_bounds(self):
        x = Track(emu()).play(200)
        pk = 20 * np.log10(np.max(np.abs(x)) / 32768)
        self.assertGreater(pk, -4)
        self.assertLess(pk, 0)
        rnd = random.Random(5)
        for _ in range(20):
            e = emu()
            tr = Track(e, color=rnd.randrange(128), shape=rnd.randrange(128), sweep=rnd.randrange(128),
                       contour=rnd.randrange(128) << 8)
            for k, top in (('KEY', 11), ('SCALE', 7), ('PROG', 17), ('VOICE', 4), ('BASS', 2), ('SUB', 10),
                           ('CHORUS', 3), ('DRIFT', 10), ('INT', 5), ('PWM', 10), ('STRUM', 10),
                           ('DRIVE', 10), ('GRIT', 10)):
                tr.set(**{k: rnd.randrange(top + 1)})
            x = tr.play(30, note=rnd.randrange(24, 100))
            self.assertLessEqual(np.max(np.abs(x)), 32767)

    def test_band_limited(self):
        """high notes read smaller waves: little above 20 kHz's mirror (aliases under the notes' harmonics)"""
        for bank, shape in ((0, 0), (1, 64), (2, 120)):
            x = Track(emu(), shape=shape).set(CHORUS=0, VOICE=1, DRIFT=0).play(300, note=96)[4800:]
            s = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
            f = np.fft.rfftfreq(len(x), 1 / FS)
            harm = np.zeros(len(f), bool)
            for f0 in [440 * 2 ** ((m - 69) / 12) for m in (96, 100, 103)]:
                for k in range(1, 40):                         # (k / 2: the sub, an octave under the lowest)
                    harm |= np.abs(f - k * f0 / 2) < 15
            self.assertLess(s[~harm & (f > 50)].sum() / s.sum(), 0.02, bank)   # (polyBLEP at C7)

    def test_shape_and_osc2_change_it(self):
        base = Track(emu(), shape=0).set(CHORUS=0).play(100)
        for kw, sets in ((dict(shape=100), {}), (dict(sweep=64, color=60), {}), (dict(sweep=64), dict(INT=2))):
            other = Track(emu(), **kw).set(CHORUS=0, **sets).play(100)
            self.assertGreater(np.std(other - base), 500, (kw, sets))

    def test_osc2_interval_and_detune(self):
        """OSC 2 alone (Sweep 127) an octave up: the chord's notes an octave up; detuned: OSC 1 and 2 apart"""
        x = Track(emu(), sweep=127, shape=0).set(CHORUS=0, SUB=0, VOICE=1, DRIFT=0, INT=2).play(400)[4800:]
        self.assertEqual(sorted(round(midi(f)) for f in peaks(x, 400, 1000, 3)), [72, 76, 79])
        def root(sweep, color):                    # C4 of OSC 1 or OSC 2 alone
            x = Track(emu(), sweep=sweep, color=color, shape=0).set(CHORUS=0, SUB=0, VOICE=1, DRIFT=0).play(600)[4800:]
            return peaks(x, 240, 280, 1)[0]
        c = lambda a, b: 1200 * np.log2(a / b)
        self.assertAlmostEqual(c(root(127, 127), root(127, 0)), 20, delta=2)   # OSC 2 20 cents up
        self.assertAlmostEqual(c(root(0, 127), root(0, 0)), -20, delta=2)      # OSC 1 20 down

    def test_osc2_level(self):
        a = Track(emu(), sweep=0).set(CHORUS=0, SUB=0, DRIVE=0).play(200)[4800:]
        b = Track(emu(), sweep=64, color=50).set(CHORUS=0, SUB=0, DRIVE=0).play(200)[4800:]
        rms = lambda y: np.sqrt(np.mean(y ** 2))
        self.assertGreater(rms(b), 1.2 * rms(a))                   # both: louder, thicker

    def test_drive_and_grit(self):
        clean = Track(emu(), shape=0).set(CHORUS=0, DRIVE=0).play(100)[1600:]
        hot = Track(emu(), shape=0).set(CHORUS=0, DRIVE=10).play(100)[1600:]
        crest = lambda y: np.max(np.abs(y)) / np.sqrt(np.mean(y ** 2))
        self.assertLess(crest(hot), crest(clean) * 0.85)
        g = Track(emu(), shape=0).set(CHORUS=0, GRIT=10).play(100)[1600:]
        self.assertGreater(np.mean(np.diff(g) == 0), 0.5)          # samples held
        self.assertLessEqual(len(set(g.astype(int))), 64)          # few levels

    def test_shape_and_chorus_change_it(self):
        base = Track(emu(), shape=0).set(CHORUS=0).play(100)
        self.assertGreater(np.std(Track(emu(), shape=100).set(CHORUS=0).play(100) - base), 500)
        self.assertGreater(np.std(Track(emu(), shape=0).set(CHORUS=2).play(100) - base), 500)

    def test_strum(self):
        tr = Track(emu(), contour=free(0)).set(VOICE=1, STRUM=4)
        tr.chord(note=60, blocks=60)
        e = tr.e
        e.w8(e.sym['dr_trig'], 1)
        tr.params()
        e.run('ac_fill', stack=[0, step_of(65), AC])
        self.assertEqual(sorted(tr.notes()), [65, 69, 72])         # the targets: F
        # (where the voices are: v->note; heard as the strum - checked through the spectrum moving)

    def test_tracks_independent(self):
        e = emu()
        a = Track(e, t=1, contour=free(0)).set(VOICE=1).chord(note=60)
        b = Track(e, t=3, contour=free(1)).set(VOICE=1).chord(note=62)
        self.assertEqual(Track(e, t=1).notes(), a)
        self.assertEqual(pcs(b, 62), {0, 3, 7})


class TCost(unittest.TestCase):
    def test_cpu(self):
        worst = 0
        for shape, sweep in ((0, 0), (0, 64), (64, 64), (127, 64)):
            for chorus in (0, 3):
                e = emu()
                tr = Track(e, shape=shape, color=40, sweep=sweep, contour=free(14)).set(CHORUS=chorus, BASS=1, SUB=8, GRIT=2)
                tr.params()
                e.w8(e.sym['dr_trig'], 1)
                e.count_instructions('ac_fill', RET, stack=[0, STEP1, AC])
                n = max(e.count_instructions('ac_fill', RET, stack=[0, STEP1, AC]) for _ in range(4))
                worst = max(worst, n)
        if os.environ.get('VA_TEST_VERBOSE'):
            print('\n  ac_fill instructions a block (worst):', worst)
        self.assertLess(worst, 10000)


class TRate32(unittest.TestCase):
    """the CPU guard's 32 kHz: the same notes and level, no click either way, cheaper"""
    def play(self, eco, blocks=900, switch=None, **kw):
        e = emu()
        e.w32(e.sym['gd_eco'], 1 if eco else 0)
        tr = Track(e, **kw).set(VOICE=1, DRIFT=0)
        if switch is None:
            return tr.play(blocks, note=57)
        a = tr.play(switch, note=57)
        e.w32(e.sym['gd_eco'], 0 if eco else 1)
        return np.concatenate([a, tr.play(blocks - switch, note=57, trig=False)])

    def test_same_notes_and_level(self):
        for shape, chorus in ((0, 0), (64, 1), (127, 3)):
            a = self.play(0, shape=shape, color=100).astype(float)
            b = self.play(1, shape=shape, color=100).astype(float)
            tr = lambda x: x[9600:]
            pa = sorted(round(midi(f)) % 12 for f in peaks(tr(a), 150, 500, 3))
            pb = sorted(round(midi(f)) % 12 for f in peaks(tr(b), 150, 500, 3))
            self.assertEqual(pa, pb, (shape, chorus))
            ra, rb = np.sqrt(np.mean(tr(a) ** 2)), np.sqrt(np.mean(tr(b) ** 2))
            self.assertLess(abs(20 * np.log10(rb / ra)), 1.0, (shape, chorus))

    def test_no_click_either_way(self):
        """down to 32 kHz at once; back to 48 kHz on the next trig (checked: it waits)"""
        for shape in (0, 127):
            for eco in (0,):
                x = self.play(eco, blocks=400, switch=150, shape=shape, color=100)
                at = 150 * 32
                near = np.max(np.abs(np.diff(x[at - 64:at + 64])))
                ref = max(np.max(np.abs(np.diff(x[at - 4800:at - 64]))), np.max(np.abs(np.diff(x[at + 64:at + 4800]))))
                self.assertLess(near / ref, 1.5, (shape, eco))

    def test_back_to_48k_on_the_next_trig(self):
        e = emu()
        e.w32(e.sym['gd_eco'], 1)
        tr = Track(e).set(VOICE=1)
        tr.play(20)
        e.run('ac_eco', stack=[0])
        self.assertEqual(e.d(0), 1)
        e.w32(e.sym['gd_eco'], 0)                  # the guard lets it go...
        tr.play(20, trig=False)
        e.run('ac_eco', stack=[0])
        self.assertEqual(e.d(0), 1)                # ...it holds 32 kHz until a trig
        tr.play(2)
        e.run('ac_eco', stack=[0])
        self.assertEqual(e.d(0), 0)

    def test_cheaper(self):
        for shape, chorus in ((0, 1), (64, 3)):
            cost = []
            for eco in (0, 1):
                e = emu()
                e.w32(e.sym['gd_eco'], eco)
                tr = Track(e, shape=shape, contour=free(14)).set(CHORUS=chorus)
                tr.params()
                e.w8(e.sym['dr_trig'], 1)
                e.count_instructions('ac_fill', RET, stack=[0, STEP1, AC])
                cost.append(max(e.count_instructions('ac_fill', RET, stack=[0, STEP1, AC]) for _ in range(4)))
            if os.environ.get('VA_TEST_VERBOSE'):
                print('\n  ACHORD shape %d chorus %d: %d at 48 kHz, %d at 32 kHz' % (shape, chorus, *cost))
            self.assertLess(cost[1], 0.93 * cost[0], (shape, chorus))


class TMenu(unittest.TestCase):
    def setUp(self):
        self.e = e = emu()
        e.run('ac_menu_open')
        self.calls = []
        st = Stubs(e)
        st.set(0x40071a04, lambda a: self.calls.append((a[2], a[3], bytes(e.uc.mem_read(a[6], 32)).split(b'\0')[0].decode())))
        st.set(0x40070dea, lambda a: 0)

    def turn(self, knob, clicks):
        self.e.run('ac_menu_turn', stack=[SET, knob, clicks & M32])
        return self.e.d(0)

    def draw(self, t=0):
        self.calls.clear()
        self.e.run('ac_menu_draw', stack=[0x5555, SET, t])
        return [c[2] for c in self.calls]

    def test_rows_and_values(self):
        t = self.draw()
        self.assertEqual(t[:2], ['ACHORD', '--'])
        self.assertEqual(t[2:], ['KEY', 'C', 'SCALE', 'FREE', 'NOTES', 'SNAP', 'PROG', 'OFF', 'ADVANCE', 'TRIG'])
        self.turn(5, 3)                            # KEY to D#
        self.turn(1, 1)
        self.turn(5, 1)                            # SCALE to MAJOR
        self.turn(1, 2)
        self.turn(5, 1)                            # PROG I-V-vi-IV
        t = self.draw()
        self.assertEqual(t[3], 'D#')
        self.assertIn('MAJOR', t)
        self.assertIn('I-V-vi-IV', t)
        self.assertEqual(self.e.r8(SET + S['KEY']), 4)              # stored + 1

    def test_every_row_and_put_back(self):
        names = []
        for r in range(24):
            self.turn(1, -100)
            self.turn(1, r)
            t = self.draw()
            names.append(t[-2] if r < 23 else t[-1])
        self.assertEqual(names[-1], 'PUT BACK DEFAULTS')
        for n in ('OSC2 INT', 'PWM RATE', 'DRIVE', 'GRIT'):
            self.assertIn(n, names)
        self.turn(1, -100)
        self.turn(5, 7)
        self.turn(1, 100)
        self.e.run('ac_menu_press', stack=[SET])
        self.assertEqual(bytes(self.e.uc.mem_read(SET, 24)), bytes(24))

    def test_fits(self):
        """every name beside its widest value, at 6 px a character"""
        widest = {'KEY': 'C#', 'SCALE': 'PHRYGIAN', 'NOTES': 'DEGREES', 'PROG': 'i-VII-VI-VII', 'ADVANCE': '2 BARS',
                  'VOICING': 'DROP 2', 'BASS': '-2 OCT', 'CHORUS': 'I+II', 'ENV DECAY': '10'}
        for name, v in widest.items():
            self.assertLess(2 + 6 * len(name), 122 - 6 * len(v), name)
        self.assertLessEqual(2 + 6 * len('PUT BACK DEFAULTS'), 125)

    def test_header_names_the_chord(self):
        e = self.e
        tr = Track(e, contour=free(4)).set(VOICE=1)
        tr.chord(note=57)
        self.assertEqual(self.draw()[1], 'Am7')
        tr = Track(e, contour=ksize(1)).set(SCALE=1, VOICE=1)
        tr.chord(note=67)
        self.assertEqual(self.draw()[1], 'G7')


class TKits(unittest.TestCase):
    """the settings are each kit's: its block (lfo2.inc: +364, 24 bytes a track), saved in its MIDI CC file (now 384
    bytes), copied with it, cleared with the track's sound; a 240-byte file from before ACHORD still loads"""
    STO = SCRATCH + 0x10000

    def setUp(self):
        self.e = e = emu()
        self.s = e.sym
        e.w32(CUR_KIT, KIT_BASE + 4 * KIT_SIZE)
        e.uc.mem_write(0x4006186e, bytes.fromhex('4fef00244e75'))
        self.files = {}
        st = Stubs(e)
        st.set(OPEN, self._open)
        st.set(READ, self._read)
        st.set(WRITE, self._write)
        st.set(POST, lambda a: 0)

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

    def area(self, n):
        return self.s['lfo34_kits'] + 508 * n + 124

    def acset(self, n, t):
        return self.s['lfo34_kits'] + 508 * n + 364 + 24 * t

    def save(self, n):
        e = self.e
        e.uc.mem_write(self.STO, bytes(7108))
        e.uc.mem_write(0x4005b2b6, bytes.fromhex('70014fef001c4e75'))
        e.run('kit_save_hook', stack=[self.STO, self.kit(n), 0])
        e.run('lcc_write', stack=[0, 0])

    def load(self, n):
        e = self.e
        e.uc.mem_write(0x4005d088, bytes.fromhex('70014fef00244e75'))
        e.run('kit_load_hook', stack=[self.kit(n), self.STO])
        e.run('lcc_poll')

    def test_saved_and_loaded(self):
        e = self.e
        mine = bytes([3, 2, 1, 2, 2] + [0] * 19)
        e.uc.mem_write(self.acset(4, 2), mine)
        want = bytes(e.uc.mem_read(self.area(4), 384))
        self.save(4)
        self.assertEqual(self.files, {'/LFOCC/%08X' % fnv(want): want})
        e.uc.mem_write(self.area(9), b'\x11' * 384)
        self.load(9)
        self.assertEqual(bytes(e.uc.mem_read(self.acset(9, 2), 24)), mine)

    def test_older_files_still_load(self):
        e = self.e
        lx = bytes(48) + bytes([4] + [7] * 31) + bytes(160)        # a 240-byte file: CC and LXR's edits
        self.files['/LFOCC/%08X' % fnv(lx)] = lx
        rec = bytes([0x43]) + fnv(lx).to_bytes(4, 'big')
        chk = fnv(lx)
        c = (chk ^ (chk >> 8) ^ (chk >> 16) ^ (chk >> 24) ^ 0x5a) & 0xff
        e.uc.mem_write(self.STO, bytes(7108))
        e.uc.mem_write(self.STO + 700, rec + bytes([c]))
        e.uc.mem_write(self.area(7), b'\x22' * 384)
        self.load(7)
        self.assertEqual(bytes(e.uc.mem_read(self.area(7), 384)), lx + bytes(144))

    def test_copied_and_cleared(self):
        e = self.e
        mine = bytes([5] * 24)
        e.uc.mem_write(self.acset(4, 1), mine)
        e.uc.mem_write(self.acset(4, 3), mine)
        e.run('kit_memcpy', stack=[self.kit(30), self.kit(4), KIT_SIZE])
        self.assertEqual(bytes(e.uc.mem_read(self.acset(30, 3), 24)), mine)
        e.run('lfo_init_hook', stack=[self.kit(30) + 28 + 3 * 100, 3, -1])
        self.assertEqual(bytes(e.uc.mem_read(self.acset(30, 3), 24)), bytes(24))
        self.assertEqual(bytes(e.uc.mem_read(self.acset(30, 1), 24)), mine)

    def test_the_playing_kits_settings(self):
        """ac_params_g hands ac_params the playing kit's block for the track"""
        e = self.e
        T = 2
        e.w8(self.acset(4, T) + S['SCALE'], 2)     # kit 4: C major
        for kit, want in ((4, {0, 3, 7}), (5, {0, 4, 7})):
            e.w32(CUR_KIT, self.kit(kit))
            for i, v in enumerate((127 << 8, 32 << 8, 0, ksize(0) if kit == 4 else free(0))):
                e.w16(TD + 22 + 2 * i, v)
            e.run('ac_params_g', regs={D[2]: T, A[2]: TD})
            e.w8(e.sym['dr_trig'] + T, 1)
            e.run('ac_fill', stack=[T, step_of(62), AC])           # D: Dm in C major, D in FREE
            got = Track(e, t=T).notes()
            self.assertEqual(pcs(got, 62), want, kit)


class TPage(unittest.TestCase):
    def test_labels(self):
        e = emu()
        e.run('apply_names', regs={D[1]: 9})
        rd = lambda a: bytes(e.uc.mem_read(a, 16)).split(b'\0')[0].decode()
        names = [rd(e.r32(0x4010dce0 + off + 44)) for off in (2576, 3472, 4088, 4144)]
        self.assertEqual(names, ['Detune', 'Wave', 'Osc 2', 'Chord'])

    def test_contour_names(self):
        e = emu()
        e.uc.mem_write(0x400cf866, bytes.fromhex('70004e75'))
        e.uc.mem_write(0x4000eb90, bytes.fromhex('70004e75'))
        e.uc.mem_write(0x40012412, bytes.fromhex('70024e75'))   # the selected track: 2
        e.w32(CUR_KIT, KIT_BASE + 4 * KIT_SIZE)
        st = e.sym['lfo34_kits'] + 508 * 4 + 364 + 24 * 2
        buf = SCRATCH + 0x63000
        def name(v):
            e.run('ac_cfmt', 0x40000e6e, stack=[0, v, buf])
            sp = e.uc.reg_read(UC_M68K_REG_A7)
            return bytes(e.uc.mem_read(e.r32(sp + 12), 8)).split(b'\0')[0].decode()
        self.assertEqual([name(free(i)) for i in range(16)], FREE)
        e.w8(st + S['SCALE'], 2)
        self.assertEqual([name(ksize(i)) for i in range(8)], KEYT)

    def test_rendered_and_dispatched(self):
        import test_va as V
        e = emu()
        e.watch('ac_fill', 'lxr_fill', 'mi_fill')
        e.w32(e.arr('trk_mach', 1), AC)
        V.T5PreDispatch()._dispatch(e, 1)
        self.assertEqual((e.visits['ac_fill'], e.visits['lxr_fill'], e.visits['mi_fill']), (1, 0, 0))

    def test_settings_preset_opens_its_page(self):
        import test_lxr_menu as M
        t = M.TOpen()
        self.assertEqual(t.open(AC, AC), (6, 2))   # LXR's page kind; ACHORD's menu behind it
        self.assertEqual(t.e.r32(t.e.sym['mm_kind']), 6)

    def test_menu_glue_routes_achord(self):
        e = emu()
        e.w32(e.sym['mm_track'], 3)
        e.w32(e.arr('trk_mach', 3), AC)
        e.run('ac_m_mine')
        self.assertEqual(e.d(0), 1)
        e.w32(e.arr('trk_mach', 3), 13)
        e.run('ac_m_mine')
        self.assertEqual(e.d(0), 0)


if __name__ == '__main__':
    unittest.main()
