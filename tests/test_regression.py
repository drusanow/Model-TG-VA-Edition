#!/usr/bin/env python3
"""Regression: the original machines (0-5) and the Sampler (6) behave exactly
as they do in the upstream Model-TG this fork is based on.

Both blobs - upstream's (UPSTREAM_REV, assembled from git) and this tree's -
run the same audio-thread entry points on the same inputs under Unicorn
(tests/va_emu.py): sampler_pre, then sampler_dispatch, block after block, a
note on the first. For every block the test compares
  - where sampler_pre hands control back to the stock code,
  - the sequence of stock routines called (the stock OS is a sea of rts, so
    each call is visible but does nothing),
  - the voice, trackData and output buffer afterwards, and the Sampler's
    2x window (its fill and filter, before the stubbed decimator).
Any difference fails. HOST verification only.
"""
import os, sys, subprocess, shutil, struct, random, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa: F401,F403

UPSTREAM_REV = os.environ.get('UPSTREAM_REV', '70b39dd')   # Model-TG v1.1.0's tree
UP = os.path.join(BUILD, 'upstream')


def build_upstream():
    if os.path.exists(os.path.join(UP, 'build', '_b.bin')) and os.environ.get('VA_TEST_NO_BUILD'):
        return os.path.join(UP, 'build')
    shutil.rmtree(UP, ignore_errors=True)
    os.makedirs(UP)
    arc = subprocess.run(['git', '-C', REPO, 'archive', UPSTREAM_REV, 'build.py', 'src'],
                         capture_output=True, check=True).stdout
    subprocess.run(['tar', '-x', '-C', UP], input=arc, check=True)
    subprocess.run([sys.executable, os.path.join(UP, 'build.py'), '--assemble-only'],
                   check=True, capture_output=True)
    return os.path.join(UP, 'build')


TD = SCRATCH + 0x1000
VOICE = SCRATCH + 0x10000
OUT = SCRATCH + 0x20000
FLAGS = SCRATCH + 0x30000
DEC = SCRATCH + 0x31000
PCM = SCRATCH + 0x80000          # one fake sample, 64-byte header + 16-bit BE


class Side:
    def __init__(self, emu):
        self.e = emu
        self.calls = []
        e = emu

        def hook(uc, addr, size, ud):
            if 0x40000400 <= addr < BLOB:
                self.calls.append(addr)
        e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x40000400, end=BLOB - 1)

    def setup(self, t, machine, mode, words, pcm):
        e = self.e; s = e.sym
        for i, w in enumerate(words):                       # word k at trackData + 2k
            e.w16(TD + 0x100 * t + 2 * i, w)
        e.w8(TD + 0x100 * t + 18, machine)                  # k = 9's high byte: the machine
        v = VOICE + 0x1000 * t
        e.w32(v + 232, 45710)
        e.w32(v + 0x318, DEC + 0x40 * t)
        e.w32(s['loop_mode'] + 4 * t, mode)
        e.w32(s['track_slot'] + 4 * t, 0)
        e.w32(s['slot_base'], PCM)
        e.w32(s['slot_count'], len(pcm))
        e.uc.mem_write(PCM, bytes(64) + b''.join(struct.pack('>h', x) for x in pcm))
        e.w32(s['mute_sil'], 0)
        for g in (0x40a78c08, 0x40a78be8, 0x40a78bd0, 0x40a78bb8, 0x40a78b9c, 0x40a78b80):
            e.w32(g + 4 * t, 0x7fffffff)                  # the mixer plays it: live
        for k in range(0x40):
            e.w8(0x40a71708 + k, (k * 37) & 0xff)          # a "Chord descriptor"

    def block(self, t, machine, trig, last):
        e = self.e
        e.w32(FLAGS + 4 * t, 1 if last else 0)
        e.w32(FLAGS + 0x100 + 4 * t, 1 if trig else 0)
        v = VOICE + 0x1000 * t
        e.w32(v + 0x34, 1 if trig else 0)
        self.calls = []
        ex = e.run_to_any('sampler_pre', (0x400a7dcc, 0x400a7db0, 0x400a7de0),
                          regs={D[1]: 0, D[2]: t, A[2]: TD + 0x100 * t, A[3]: FLAGS + 4 * t,
                                A[5]: FLAGS + 0x100 + 4 * t, A[6]: v}, limit=2_000_000)
        pre_calls = self.calls
        self.calls = []
        e.run('sampler_dispatch', 0x400a7e24,
              regs={D[1]: 0, D[2]: t, D[3]: OUT + 0x100 * t, D[4]: 6 if machine >= 6 else machine,
                    D[6]: 0x40118628, A[6]: v, A[2]: TD + 0x100 * t}, limit=5_000_000)
        s = e.sym
        state = (ex, tuple(pre_calls), tuple(self.calls),
                 bytes(e.uc.mem_read(v, 0x400)),
                 bytes(e.uc.mem_read(TD + 0x100 * t, 0x100)),
                 bytes(e.uc.mem_read(OUT + 0x100 * t, 128)),
                 bytes(e.uc.mem_read(s['sampler_buf'] + 32, 256)),
                 e.r32(s['trk_mach'] + 4 * t))
        return state


class TRegression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.up_dir = build_upstream()

    def _compare(self, machine, mode, t, seed):
        rnd = random.Random(seed)
        up, va = Side(Emu(self.up_dir)), Side(Emu())
        words = [rnd.randrange(0, 0x8000) for _ in range(33)]
        words[11:15] = [rnd.randrange(0, 32513) for _ in range(4)]
        pcm = [int(20000 * ((i * 7919) % 2000 - 1000) / 1000) for i in range(8192)]
        for side in (up, va):
            side.setup(t, machine, mode, words, pcm)
        heard = rendered = 0
        for blk in range(12):
            trig = blk in (0, 7)
            last = blk in (1, 8)
            a = up.block(t, machine, trig, last)
            b = va.block(t, machine, trig, last)
            names = ('exit', 'stock calls in sampler_pre', 'stock calls in dispatch',
                     'voice', 'trackData', 'outBuf', 'sampler_buf window', 'trk_mach')
            for n, x, y in zip(names, a, b):
                self.assertEqual(x, y, f"machine {machine} mode {mode} block {blk}: {n} differs")
            heard += any(a[6])
            rendered += (STUB_RENDER + 2 * machine) in a[2]
        # not a vacuous comparison: the Sampler filled its window, a stock
        # machine's own render function was called
        if machine == 6:
            self.assertGreater(heard, 0, f"mode {mode}: the Sampler rendered nothing")
        else:
            self.assertGreater(rendered, 0, f"machine {machine}: its render never ran")
        return up

    def test_stock_machines(self):
        for m in range(6):
            self._compare(m, 0, m % 6, 100 + m)

    def test_sampler_modes(self):
        # One shot, Loop, Slice, Granular, Stretch, Pluck, Wave
        for mode in range(7):
            self._compare(6, mode, 3, 200 + mode)


if __name__ == '__main__':
    unittest.main(verbosity=2)
