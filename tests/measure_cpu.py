#!/usr/bin/env python3
"""Counts the instructions OUR code executes per 32-sample block, for the VA
and, for comparison, the Sampler's Wave mode (the wavetable renderer the VA
is built from) and One shot. Stock routines are stubs here, so what they cost
(the decimator, the amp envelope, the per-voice parameters) is not counted -
it is the same for the VA as for the Sampler. Instruction counts are not
cycles: multiplies and divides cost more, so read them as relative cost.
On hardware use Device Config > System, second page (per-track share).

    python3 tests/measure_cpu.py
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa
from test_regression import Side, TD, VOICE  # noqa

BLOCKS = 40


def va_fill_cost(e, w1, w2, mix, step):
    for t in range(1):
        for i, v in enumerate((w1 << 8, w2 << 8, 70 << 8, mix << 8)):
            e.w16(TD + 22 + 2 * i, v)
        e.run('va_params', regs={D[2]: 0, A[2]: TD})
    n = 0
    for _ in range(BLOCKS):
        n += e.count_instructions('va_fill', RET, stack=[0, step])
    return n / BLOCKS


def wave_fill_cost(e, detune):
    s = e.sym
    pcm = SCRATCH + 0x80000
    e.uc.mem_write(pcm, b''.join(struct.pack('>h', (i * 37) % 30000 - 15000) for i in range(8192)))
    e.w32(s['wv_shift'], 11)
    e.w32(s['wv_fa'], 0); e.w32(s['wv_fb'], 1); e.w32(s['wv_m8'], 100)
    e.w32(s['wv_det'], 400 if detune else 0)
    n = 0
    for _ in range(BLOCKS):
        n += e.count_instructions('wave_fill', RET, stack=[0, 0x10000, pcm, 8192])
    return n / BLOCKS


def path_cost(machine, mode, words_fn):
    """sampler_pre + sampler_dispatch (our code only), per block, playing."""
    side = Side(Emu())
    e = side.e
    words = [0] * 33
    words_fn(words)
    pcm = [int(20000 * ((i * 7919) % 2000 - 1000) / 1000) for i in range(48000)]
    side.setup(0, machine, mode, words, pcm)
    total = 0
    h = [0]
    hook = e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s_, u: h.__setitem__(0, h[0] + 1),
                         begin=BLOB, end=BLOB + len(e.blob))
    for blk in range(BLOCKS):
        h[0] = 0
        side.block(0, machine, blk == 0, blk == 1)
        total += h[0]
    e.uc.hook_del(hook)
    return total / BLOCKS


def main():
    e = Emu()
    print("instructions of our code per 32-sample block (stock routines not counted)\n")
    print("va_fill alone (two oscillators, 64 samples at 96 kHz):")
    for name, w1, w2, mix in (("saw + saw, MIX 64", 0, 0, 64), ("square + square, MIX 64", 50, 50, 64),
                              ("tri + tri, MIX 64", 100, 100, 64), ("saw alone, MIX 0", 0, 0, 0),
                              ("square + saw, MIX 64", 50, 0, 64)):
        for step, note in ((0x10000, "C4"), (0x80000, "C7")):
            print(f"  {name:24s} {note}: {va_fill_cost(e, w1, w2, mix, step):7.0f}")
    print("\nwave_fill alone (the Sampler's Wave mode, 2048-sample frames):")
    print(f"  no Detune (one voice)      : {wave_fill_cost(e, False):7.0f}")
    print(f"  Detune (three voices)      : {wave_fill_cost(e, True):7.0f}")

    def va_words(w):
        w[11:15] = [0, 0, 70 << 8, 64 << 8]

    def smp_words(w):
        w[11:15] = [0, 32512, 32512, 0]

    def wave_words(w):
        w[11:15] = [8000, 20000, 16000, 0]
    print("\nwhole per-track path (sampler_pre + sampler_dispatch + render + amp_hook):")
    print(f"  VA, saw + saw              : {path_cost(7, 0, va_words):7.0f}")
    print(f"  Sampler, One shot          : {path_cost(6, 0, smp_words):7.0f}")
    print(f"  Sampler, Wave              : {path_cost(6, 6, wave_words):7.0f}")

    def drum_words(*d):
        def f(w):
            w[11:15] = [v << 8 for v in d]
        return f
    print(f"  VA KICK, defaults          : {path_cost(8, 0, drum_words(48, 40, 24, 40)):7.0f}")
    print(f"  VA KICK, full drive        : {path_cost(8, 0, drum_words(48, 40, 127, 40)):7.0f}")
    print(f"  VA SNARE, defaults         : {path_cost(9, 0, drum_words(64, 32, 88, 16)):7.0f}")
    print(f"  VA SNARE, full drive       : {path_cost(9, 0, drum_words(64, 32, 88, 127)):7.0f}")
    print(f"  VA HIHAT, defaults         : {path_cost(10, 0, drum_words(80, 64, 16, 0)):7.0f}")
    print(f"  VA HIHAT, full drive       : {path_cost(10, 0, drum_words(80, 64, 16, 127)):7.0f}")


if __name__ == '__main__':
    main()
