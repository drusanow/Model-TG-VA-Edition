#!/usr/bin/env python3
"""Host tests for the alternative send effects (src/sendfx.inc).

The blob runs on Unicorn with the stock OS as `rts` stubs (tests/va_emu.py).
The delay and reverb slots are called as the output stage calls them - (out,
in, params), 32 stereo frames of 32-bit samples - and the stock effects
they stand in front of (0x40057488, 0x400579c4) are watched for what they
are fed. The playing kit is one of the 96 (CUR_KIT), its types in its LFO
3/4 block.

    python3 -m unittest tests.test_sendfx -v
"""
import os, sys, struct, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

KIT_BASE, KIT_SIZE = 0x409C9C80, 7094
CUR_KIT = 0x40a78888
TEMPO = 0x40149310
DLY, REV = 0x40057488, 0x400579c4
IN, OUT, PAR = SCRATCH + 0x20000, SCRATCH + 0x20100, SCRATCH + 0x20200
STO = SCRATCH + 0x10000
EMU = None
FS = 1 << 16                                         # one 16-bit step, as a 32-bit sample


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    return EMU


class Base(unittest.TestCase):
    KIT = 9

    def setUp(self):
        self.e = e = emu()
        self.s = e.sym
        e.w32(CUR_KIT, KIT_BASE + self.KIT * KIT_SIZE)
        e.w32(TEMPO, 120 * 120)                      # 120 BPM
        self.fed = []
        self.stock_out = None                        # what the stock stand-ins put out
        for a in (DLY, REV):
            e.uc.hook_add(UC_HOOK_CODE, self._fed, begin=a, end=a)

    def _fed(self, uc, addr, size, ud):
        sp = uc.reg_read(UC_M68K_REG_A7)
        out, inp, par = struct.unpack('>3I', uc.mem_read(sp + 4, 12))
        silent = bytes(uc.mem_read(inp, 256)) == b'\0' * 256
        self.fed.append((addr, out, inp, par, silent))
        if inp != IN:                                # the stock reverb scribbles on
            uc.mem_write(inp, b'\x77' * 256)        # its input buffer
        if self.stock_out is not None:
            uc.mem_write(out, self.stock_out)

    def blk(self, n=None):
        return self.s['lfo34_kits'] + 172 * (self.KIT if n is None else n)

    def types(self, d=None, r=None):
        if d is not None:
            self.e.w8(self.blk() + 120, d)
        if r is not None:
            self.e.w8(self.blk() + 121, r)

    def params(self, p0, p1):
        self.e.w16(PAR, p0)
        self.e.w16(PAR + 2, p1)

    def block(self, fn, left, right=None):
        """One block through fn: 32 left / right samples in (16-bit values),
        their 32-bit outputs back."""
        e = self.e
        right = left if right is None else right
        e.uc.mem_write(IN, b''.join(struct.pack('>ii', l * FS, r * FS) for l, r in zip(left, right)))
        e.uc.mem_write(OUT, b'\xa5' * 256)
        args = e.run(fn, stack=[OUT, IN, PAR])
        self.assertEqual(e.sp(), args)
        v = struct.unpack('>64i', e.uc.mem_read(OUT, 256))
        return list(v[0::2]), list(v[1::2])

    def run_for(self, fn, left, right=None):
        """Any number of samples (a multiple of 32) -> outputs, as 16-bit."""
        right = left if right is None else right
        ol, orr = [], []
        for i in range(0, len(left), 32):
            a, b = self.block(fn, left[i:i + 32], right[i:i + 32])
            ol += [x / FS for x in a]
            orr += [x / FS for x in b]
        return ol, orr

    def settle(self, fn, blocks):
        """Run a new type through its clearing: silence in, silence out."""
        for _ in range(blocks):
            a, b = self.block(fn, [0] * 32)
            self.assertEqual(a + b, [0] * 64)


HOLD = 16384                                         # FX_HOLD: one turn of the delay's ring


class THidden(Base):
    """A stock effect hidden behind another type stops being run once it has put out
    silence for FX_HOLD blocks in a row (fed silence, as fx_off's rule)."""

    def calls(self, fn, n, left=8000):
        self.fed = []
        outs = [self.block(fn, [left] * 32) for _ in range(n)]
        return len(self.fed), outs

    def test_stops_after_a_ring_turn_of_silence(self):
        e = self.e
        self.types(1, 2)
        self.params(1792, 16384)
        self.stock_out = bytes(256)                  # a hidden stock effect gone silent
        for fn, q in (('sfx_dly', 'sfx_dq'), ('sfx_rev', 'sfx_rq')):
            self.settle(fn, 32)
            self.assertEqual(e.r32(e.sym[q]), 32)    # counting while it runs
            e.w32(e.sym[q], HOLD - 3)
            n, _ = self.calls(fn, 10)
            self.assertEqual(n, 3, fn)               # three more blocks, then no more calls
            self.assertEqual(e.r32(e.sym[q]), HOLD)

    def test_frozen_or_not_ours_sounds_the_same(self):
        e = self.e
        self.stock_out = bytes(256)
        got = []
        for frozen in (False, True):
            e.reset()
            e.w32(CUR_KIT, KIT_BASE + self.KIT * KIT_SIZE)
            e.w32(TEMPO, 120 * 120)
            self.types(2, 3)
            self.params(1792, 16384)
            self.settle('sfx_dly', 32)
            self.settle('sfx_rev', 5)
            outs = []
            for i in range(200):
                for q in ('sfx_dq', 'sfx_rq'):
                    e.w32(e.sym[q], HOLD if frozen else 0)
                sig = [int(9000 * ((i * 32 + k) % 37 - 18) / 18) for k in range(32)]
                outs.append(self.block('sfx_dly', sig))
                outs.append(self.block('sfx_rev', sig))
            got.append(outs)
        self.assertEqual(got[0], got[1])

    def test_a_sound_restarts_the_count(self):
        e = self.e
        self.types(1, 2)
        self.params(1792, 16384)
        self.stock_out = struct.pack('>64i', *([3 * FS] * 64))   # still ringing
        for fn, q in (('sfx_dly', 'sfx_dq'), ('sfx_rev', 'sfx_rq')):
            e.w32(e.sym[q], HOLD - 1)
            n, _ = self.calls(fn, 4)
            self.assertEqual(n, 4, fn)
            self.assertEqual(e.r32(e.sym[q]), 0)

    def test_back_to_stock_runs_it_again(self):
        e = self.e
        self.types(1, 2)
        self.params(1792, 16384)
        self.stock_out = bytes(256)
        for fn, q, stock, d, r in (('sfx_dly', 'sfx_dq', DLY, 0, None), ('sfx_rev', 'sfx_rq', REV, None, 0)):
            self.settle(fn, 32)
            e.w32(e.sym[q], HOLD)
            self.assertEqual(self.calls(fn, 2)[0], 0)
            self.types(3 if d is not None else None, 4 if r is not None else None)   # another
            self.assertEqual(self.calls(fn, 2)[0], 0, fn)                            # still frozen
            self.types(d, r)                         # the stock type: run again, at once
            self.assertEqual(e.run_to_any(fn, (stock,), stack=[OUT, IN, PAR]), stock)
            self.assertEqual(e.r32(e.sym[q]), 0)
            self.types(1 if d is not None else None, 2 if r is not None else None)
            self.assertEqual(self.calls(fn, 2)[0], 2, fn)                            # hidden again: counted afresh


class TSlots(Base):
    def test_stock_types_are_the_stock_effects(self):
        e = self.e
        for fn, stock in (('sfx_dly', DLY), ('sfx_rev', REV)):
            st = e.run_to_any(fn, (stock,), stack=[OUT, IN, PAR])
            self.assertEqual(st, stock)
            self.assertEqual(struct.unpack('>3I', e.uc.mem_read(e.sp() + 4, 12)), (OUT, IN, PAR))
        self.types(7, 9)                             # out of range: stock too
        self.assertEqual(e.run_to_any('sfx_dly', (DLY,), stack=[OUT, IN, PAR]), DLY)
        self.assertEqual(e.run_to_any('sfx_rev', (REV,), stack=[OUT, IN, PAR]), REV)

    def test_alt_feeds_the_stock_silence_and_starts_clear(self):
        e = self.e
        self.types(1, 2)
        # garbage where the lines and the silence go
        e.uc.mem_write(self.dbuf(), b'\x5a' * 0x80000)
        e.uc.mem_write(self.rbuf(), b'\x5a' * 0x1f000)
        self.params(1792, 16384)
        for fn, n in (('sfx_dly', 32), ('sfx_rev', 5)):
            self.fed = []
            for _ in range(n):                       # clearing: silence, whatever comes in
                a, b = self.block(fn, [9000] * 32)
                self.assertEqual(a + b, [0] * 64)
            zero = self.rbuf() + 0x1f000
            self.assertEqual(set(f[1:] for f in self.fed), {(OUT, zero, PAR, True)})
        self.assertEqual(bytes(e.uc.mem_read(self.dbuf(), 0x80000)), b'\0' * 0x80000)
        self.assertEqual(bytes(e.uc.mem_read(self.rbuf(), 0x14000)), b'\0' * 0x14000)
        a, b = self.block('sfx_dly', [0] * 32)       # then the effect: nothing in, nothing out
        self.assertEqual(a + b, [0] * 64)

    def dbuf(self):
        return self.s['SFX_DBUF']

    def rbuf(self):
        return self.s['SFX_RBUF']

    def test_type_change_clears_again(self):
        self.types(1)
        self.params(1792, 16384)
        self.settle('sfx_dly', 32)
        self.run_for('sfx_dly', [8000] * 64)
        self.types(2)
        self.settle('sfx_dly', 32)                   # from silence again
        ol, orr = self.run_for('sfx_dly', [0] * 8000)
        self.assertEqual(max(map(abs, ol + orr)), 0)


def impulse(n, at=0, v=16384):
    x = [0] * n
    x[at] = v
    return x


def peak(x, lo, hi):
    seg = x[lo:hi]
    m = max(range(len(seg)), key=lambda i: abs(seg[i]))
    return lo + m, seg[m]


class TDelays(Base):
    def setUp(self):
        super().setUp()
        self.params(1792, 16384)                     # 1/16 at 120 BPM: 6000 samples; feedback 0.5

    def test_ping_pong(self):
        self.types(1)
        self.settle('sfx_dly', 32)
        ol, orr = self.run_for('sfx_dly', impulse(19200))
        self.assertEqual(peak(ol, 0, 19200), (6000, 16384))     # left first, at full level
        self.assertEqual(peak(orr, 0, 9000), (0, 0))
        self.assertEqual(peak(orr, 9000, 19200), (12000, 8192))  # then right, half
        self.assertEqual(peak(ol, 12100, 19200), (18000, 4096))  # then left again
        self.assertEqual(max(map(abs, ol[6001:17999])), 0)

    def test_ping_pong_follows_the_tempo(self):
        self.e.w32(TEMPO, 150 * 120)
        self.types(1)
        self.settle('sfx_dly', 32)
        ol, _ = self.run_for('sfx_dly', impulse(6400))
        self.assertEqual(peak(ol, 0, 6400)[0], 4800)

    def test_tape_wobbles_and_darkens(self):
        self.types(2)
        self.settle('sfx_dly', 32)
        ol, orr = self.run_for('sfx_dly', impulse(19200), [0] * 19200)
        at, v = peak(ol, 0, 9000)
        self.assertLess(abs(at - 6000), 60)                     # wow: +-50 samples
        self.assertTrue(4000 < v < 12000, v)                    # the lowpass smears it
        self.assertGreater(sum(1 for x in ol[at - 20:at + 20] if abs(x) > 200), 3)
        at2, v2 = peak(ol, at + 3000, 19200)
        self.assertLess(abs(at2 - 2 * at), 150)
        self.assertTrue(0 < v2 < v)
        self.assertEqual(peak(orr, 0, 19200)[1], 0)             # each side its own

    def test_tape_saturates_without_running_away(self):
        self.params(1792, 32767)
        self.types(2)
        self.settle('sfx_dly', 32)
        loud = [30000 if (i // 50) % 2 else -30000 for i in range(6400)]
        ol, _ = self.run_for('sfx_dly', loud + [0] * 64000)
        self.assertLessEqual(max(map(abs, ol)), 32767)
        self.assertLess(max(map(abs, ol[-6400:])), max(map(abs, ol[6000:12800])))

    def test_chorus_and_flanger(self):
        self.params(4096, 0)                                    # chorus, no depth: 15 ms
        self.types(3)
        self.settle('sfx_dly', 32)
        ol, orr = self.run_for('sfx_dly', impulse(2048))
        self.assertEqual(peak(ol, 0, 2048), (720, 16384))
        self.assertEqual(peak(orr, 0, 2048), (720, 16384))
        self.assertEqual(sum(1 for x in ol if x), 1)            # no feedback
        self.params(4096, 32767)                                # flanger, feedback 0.9
        self.run_for('sfx_dly', [0] * 32 * 300)
        ol, _ = self.run_for('sfx_dly', impulse(1024))
        first = next(i for i, x in enumerate(ol) if abs(x) > 1000)
        self.assertTrue(8 <= first <= 185, first)
        self.assertGreater(sum(1 for x in ol[first + 5:] if abs(x) > 1000), 2)   # repeats

    def test_chorus_moves(self):
        self.params(127 << 8, 16000)                            # fast, deep
        self.types(3)
        self.settle('sfx_dly', 32)
        tone = [int(8000 * ((i % 96) < 48) - 4000) for i in range(32 * 400)]
        ol, orr = self.run_for('sfx_dly', tone)
        self.assertNotEqual(ol[-3000:], orr[-3000:])            # the sides apart
        self.assertLessEqual(max(map(abs, ol + orr)), 8001)


class TReverbs(Base):
    def test_phaser(self):
        self.params(64 << 8, 0)
        self.types(r=1)
        self.settle('sfx_rev', 5)
        ol, orr = self.run_for('sfx_rev', [0] * 640)
        self.assertEqual(max(map(abs, ol + orr)), 0)            # silence stays silence
        ol, _ = self.run_for('sfx_rev', impulse(640))
        self.assertGreater(sum(1 for x in ol if abs(x) > 50), 8)   # spread out in time
        self.params(64 << 8, 32767)                             # with feedback, bounded
        sq = [30000 if (i // 40) % 2 else -30000 for i in range(32 * 200)]
        ol, orr = self.run_for('sfx_rev', sq)
        self.assertLessEqual(max(map(abs, ol + orr)), 32767)
        self.assertNotEqual(ol[-640:], orr[-640:])              # the sides apart

    def test_spring_rings_and_decays(self):
        tails = {}
        for size in (0, 127 << 8):
            self.setUp()
            self.params(size, 16384)
            self.types(r=2)
            self.settle('sfx_rev', 5)
            ol, orr = self.run_for('sfx_rev', impulse(32 * 600))
            e1 = sum(x * x for x in ol[2000:6000])
            e2 = sum(x * x for x in ol[12000:16000])
            self.assertGreater(e1, 0)
            self.assertLess(e2, e1)
            self.assertNotEqual(ol[:4000], orr[:4000])
            tails[size] = e2 / e1
        self.assertGreater(tails[127 << 8], tails[0] * 10)     # Size: a longer tail

    def test_ring(self):
        self.params(64 << 8, 0)                                 # ~290 Hz triangle
        self.types(r=3)
        self.settle('sfx_rev', 5)
        ol, orr = self.run_for('sfx_rev', [0] * 640)
        self.assertEqual(max(map(abs, ol + orr)), 0)
        ol, orr = self.run_for('sfx_rev', [8192] * 4800)
        self.assertLessEqual(max(map(abs, ol)), 8192)
        self.assertGreater(max(ol), 8000)
        flips = sum(1 for a, b in zip(ol, ol[1:]) if (a < 0) != (b < 0))
        f = 20 * 200 ** (64 / 127)
        self.assertLess(abs(flips - 2 * f * 0.1), 4, (flips, f))
        self.assertNotEqual(ol, orr)                            # right a quarter on
        self.params(64 << 8, 32767)                             # Tone: towards a square
        ol, _ = self.run_for('sfx_rev', [8192] * 960)
        self.assertGreater(sum(1 for x in ol if abs(x) > 8000), 700)

    def test_dist(self):
        self.params(0, 32767)                                   # gentle, open
        self.types(r=4)
        self.settle('sfx_rev', 5)
        sine = [round(200 * __import__('math').sin(i / 10)) for i in range(32 * 40)]
        ol, _ = self.run_for('sfx_rev', sine)
        for x, y in zip(sine[200:], ol[200:]):
            self.assertLess(abs(y - x * 17 / 32), 4)            # x1.06, -6 dB
        self.params(127 << 8, 32767)                            # driven hard
        ol, _ = self.run_for('sfx_rev', sine)
        self.assertLessEqual(max(map(abs, ol)), 16385)
        self.assertGreater(sum(1 for x in ol[200:] if abs(x) > 15000), 300)
        self.params(127 << 8, 0)                                # dark
        ol2, _ = self.run_for('sfx_rev', sine)
        self.assertLess(max(map(abs, ol2[200:])), max(map(abs, ol[200:])))


class TReso(Base):
    def run_reso(self, size, tone, x, right=None):
        self.params(size, tone)
        self.types(r=5)
        self.settle('sfx_rev', 5)
        return self.run_for('sfx_rev', x, right)

    def test_silence_stays_silent(self):
        ol, orr = self.run_reso(64 << 8, 64 << 8, [0] * 3200)
        self.assertEqual(max(map(abs, ol + orr)), 0)

    def test_rings_at_its_pitch(self):
        """An impulse rings at Size's pitch, C1 x 200^(k/127): the
        autocorrelation's peak near the period is within 2% of it."""
        for k in (16, 48, 80):
            self.setUp()
            ol, orr = self.run_reso(k << 8, 100 << 8, impulse(32 * 300))
            per = 48000 / (32.70 * 200 ** (k / 127))
            seg = ol[1500:5500]

            def ac(lag):
                return sum(seg[i] * seg[i + lag] for i in range(0, len(seg) - lag))
            lags = range(int(per * 0.8), int(per * 1.25) + 2)
            best = max(lags, key=ac)
            self.assertLess(abs(best / per - 1), 0.02, (k, per, best))
            self.assertNotEqual(ol[2000:4000], orr[2000:4000])   # the sides a few cents apart
        self.assertLess(abs(sum(ol[2000:9000]) / 7000), 200)     # no DC left

    def test_tone_sets_the_ring(self):
        tails = []
        for tone in (0, 127 << 8):
            self.setUp()
            ol, _ = self.run_reso(48 << 8, tone, impulse(32 * 600))
            tails.append(sum(x * x for x in ol[9600:19200]))
        self.assertGreater(tails[1], tails[0] * 100)            # bright and long vs dark and short

    def test_bounded(self):
        loud = [30000 if (i // 37) % 2 else -30000 for i in range(32 * 400)]
        ol, orr = self.run_reso(40 << 8, 127 << 8, loud)
        self.assertLessEqual(max(map(abs, ol + orr)), 32767)
        self.assertGreater(max(map(abs, ol)), 1000)


class TCpu(Base):
    def test_a_block_each(self):
        """Instructions a block, every type: the budget the docs quote - once the hidden
        stock effect has gone quiet and is no longer run (its count at FX_HOLD)."""
        e = self.e
        e.w32(e.sym['sfx_dq'], HOLD)
        e.w32(e.sym['sfx_rq'], HOLD)
        e.uc.mem_write(IN, b''.join(struct.pack('>i', (i * 7919 % 60000 - 30000) * FS) for i in range(64)))
        self.params(100 << 8, 100 << 8)
        got = {}
        for d in (1, 2, 3):
            self.types(d, 0)
            for _ in range(33):
                e.run('sfx_dly', stack=[OUT, IN, PAR])
            got['dly%d' % d] = e.count_instructions('sfx_dly', RET, stack=[OUT, IN, PAR])
        for r in (1, 2, 3, 4, 5):
            self.types(0, r)
            for _ in range(6):
                e.run('sfx_rev', stack=[OUT, IN, PAR])
            got['rev%d' % r] = e.count_instructions('sfx_rev', RET, stack=[OUT, IN, PAR])
        print('\n  send FX, instructions a block:', got)
        self.assertLess(max(got.values()), 9000, got)


class TMenu(Base):
    def test_rows_turn_the_playing_kit(self):
        e = self.e
        for fn, off, n in (('sfx_rot_d', 120, 4), ('sfx_rot_r', 121, 6)):
            seen, want, v = [], [], 0
            for delta in (1, 1, 1, 1, 1, 1, -1, 0, -1, -1, -1, -1, -1, -3, 5):
                e.run(fn, stack=[0, 0, delta & M32])
                seen.append(e.r8(self.blk() + off))
                v = min(max(v + (delta > 0) - (delta < 0), 0), n - 1)
                want.append(v)
            self.assertEqual(seen, want)
        self.assertEqual(e.r8(self.blk(10) + 120), 0)          # another pattern's: untouched

    def test_rows_show_the_playing_kit(self):
        e = self.e
        self.types(2, 4)
        for fn, want, names in (('sfx_val_d', 2, 'sfx_n_d'), ('sfx_val_r', 4, 'sfx_n_r')):
            st = e.run_to_any(fn, (self.s['gm_val'],), stack=[0, 0, 0, 0, 0])
            self.assertEqual(st, self.s['gm_val'])
            self.assertEqual((e.a(0), e.a(1), e.r32(self.s['sfx_v'])), (self.s['sfx_v'], self.s[names], want))
        strings = []
        for names, n in (('sfx_n_d', 4), ('sfx_n_r', 6)):
            for i in range(n):
                a = e.r32(self.s[names] + 4 * i)
                b = bytes(e.uc.mem_read(a, 8))
                strings.append(b[:b.index(0)].decode())
        self.assertEqual(strings, ['DELAY', 'PING', 'TAPE', 'CHORUS',
                                   'REVERB', 'PHASER', 'SPRING', 'RING', 'DIST', 'RESO'])

    def test_four_lines(self):
        """A heading, then its effect on the line under it: no line carries
        both a label and a value. Both lines of a pair turn the same type."""
        e = self.e
        d = self.s['sfx_desc']
        self.assertEqual(e.r32(d + 8), 4)
        items = e.r32(d + 12)
        rows = [[e.r32(items + 16 * r + 4 * c) for c in range(4)] for r in range(4)]
        S = self.s
        self.assertEqual(rows, [[S['sfx_lab_d'], S['sfx_val_e'], S['sfx_rot_d'], S['new_slot2_invoker']],
                                [S['sfx_lab_e'], S['sfx_val_d'], S['sfx_rot_d'], S['new_slot2_invoker']],
                                [S['sfx_lab_r'], S['sfx_val_e'], S['sfx_rot_r'], S['new_slot2_invoker']],
                                [S['sfx_lab_e'], S['sfx_val_r'], S['sfx_rot_r'], S['new_slot2_invoker']]])
        e.run_to_any('sfx_val_e', (S['gm_val'],), stack=[0, 0, 0, 0, 0])
        self.assertEqual(e.r8(e.r32(e.a(1) + 4 * e.r32(e.a(0)))), 0)     # the heading's value: ""
        for lab, want in (('sfx_lab_d', 'DELAY FX'), ('sfx_lab_r', 'REVERB FX'), ('sfx_lab_e', '')):
            e.run_to_any(lab, (S['gm_lab'],), stack=[0])
            b = bytes(e.uc.mem_read(e.a(1), 12))
            self.assertEqual(b[:b.index(0)].decode(), want)

    def test_settings_page_reaches_the_menu(self):
        e = self.e
        e.uc.reg_write(UC_M68K_REG_SR, 0x2000)
        e.watch('sfx_menu_open')
        EV = SCRATCH + 0x9000
        e.w32(EV + 12, 15); e.w32(EV + 16, 1)                  # PAGE, down
        e.w32(self.s['set_held'], 1)
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.visits['sfx_menu_open'], 1)
        self.assertEqual(e.d(0), 15)                            # nothing opened: passed on
        e.w32(self.s['set_held'], 0)                            # PAGE alone: stock
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.visits['sfx_menu_open'], 1)

    def test_swallowed_when_open(self):
        e = self.e
        mm = self.s['mm_opened']
        stub = SCRATCH + 0xa000
        e.uc.mem_write(stub, bytes.fromhex('7001 23c0' + f'{mm:08x}' + '4e75'))
        e.uc.mem_write(self.s['sfx_menu_open'], bytes.fromhex('4ef9' + f'{stub:08x}'))
        EV = SCRATCH + 0x9000
        e.w32(EV + 12, 15); e.w32(EV + 16, 1)
        e.w32(self.s['set_held'], 1)
        e.w32(self.s['mod_used'], 0)
        e.run('key_hook', stack=[EV])
        self.assertEqual((e.d(0), e.r32(EV + 16) & 8, e.r32(self.s['mod_used'])), (0, 8, 1))
        e.w32(EV + 16, 0)                                       # the release
        e.run('key_hook', stack=[EV])
        self.assertEqual((e.d(0), e.r32(self.s['kh_eaten7'])), (0, 0))


class TKits(Base):
    def save(self, kit):
        e = self.e
        e.uc.mem_write(0x4005b2b6, bytes.fromhex('70014fef001c4e75'))
        args = e.run('kit_save_hook', stack=[STO, kit, 0])
        self.assertEqual(e.sp(), args)

    def load(self, kit):
        e = self.e
        e.uc.mem_write(0x4005d088, bytes.fromhex('70014fef00244e75'))
        args = e.run('kit_load_hook', stack=[kit, STO])
        self.assertEqual(e.sp(), args)

    def kit(self, n):
        return KIT_BASE + n * KIT_SIZE

    def test_saved_and_loaded(self):
        e = self.e
        self.types(3, 4)
        self.save(self.kit(self.KIT))
        self.assertEqual((e.r8(STO + 646), e.r8(STO + 647)), (0x43, 0xbc))
        self.load(self.kit(40))
        self.assertEqual((e.r8(self.blk(40) + 120), e.r8(self.blk(40) + 121)), (3, 4))
        self.save(SCRATCH + 0x20000)                          # not one of the 96: stock
        self.assertEqual((e.r8(STO + 646), e.r8(STO + 647)), (0, 0xff))

    def test_older_or_odd_records_load_stock(self):
        e = self.e
        for a, b in ((0, 0), (0xff, 0xff), (0x43, 0x00), (0x97, 0x68), (0x5f, 0xa0)):
            e.w8(self.blk(41) + 120, 2)
            e.w8(self.blk(41) + 121, 2)
            e.w8(STO + 646, a)
            e.w8(STO + 647, b)
            self.load(self.kit(41))
            got = (e.r8(self.blk(41) + 120), e.r8(self.blk(41) + 121))
            want = {(0x5f, 0xa0): (0, 5)}.get((a, b), (0, 0))   # RESO's 5: in range
            self.assertEqual(got, want, (a, b))
        e.w8(STO + 646, 0x31)                                   # one in range, one not
        e.w8(STO + 647, 0xce)
        self.load(self.kit(41))
        self.assertEqual((e.r8(self.blk(41) + 120), e.r8(self.blk(41) + 121)), (1, 3))
        e.w8(STO + 646, 0x7f)
        e.w8(STO + 647, 0x80)
        self.load(self.kit(41))
        self.assertEqual((e.r8(self.blk(41) + 120), e.r8(self.blk(41) + 121)), (0, 0))

    def test_copied_with_the_kit(self):
        e = self.e
        self.types(2, 3)
        e.run('kit_memcpy', stack=[self.kit(50), self.kit(self.KIT), KIT_SIZE])
        self.assertEqual((e.r8(self.blk(50) + 120), e.r8(self.blk(50) + 121)), (2, 3))

    def test_fx_defaults_bring_back_the_stock_effects(self):
        e = self.e
        self.types(2, 3)
        e.w8(self.blk(11) + 120, 1)
        st = e.run_to_any('sfx_fxd', (0x40061320,), stack=[self.kit(self.KIT) + 628],
                          regs={D[2]: 2, A[2]: 0x22, A[3]: 0x33, A[4]: 0x44})
        self.assertEqual(st, 0x40061320)
        sp = e.sp()
        self.assertEqual(struct.unpack('>4I', e.uc.mem_read(sp, 16)), (2, 0x22, 0x33, 0x44))
        self.assertEqual(e.r32(sp + 32), self.kit(self.KIT) + 628)   # the frame as stock
        self.assertEqual((e.r8(self.blk() + 120), e.r8(self.blk() + 121)), (0, 0))
        self.assertEqual(e.r8(self.blk(11) + 120), 1)          # another kit's: kept
        e.run_to_any('sfx_fxd', (0x40061320,), stack=[SCRATCH + 0x5000])
        self.assertEqual(e.r8(self.blk(11) + 120), 1)


if __name__ == '__main__':
    unittest.main()
