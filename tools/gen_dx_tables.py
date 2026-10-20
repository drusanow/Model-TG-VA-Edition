#!/usr/bin/env python3
"""Writes src/dx_tables.inc: the tables of the DX machine (src/dx.inc), a
6-operator FM voice after the Yamaha DX7, as Dexed's engine (msfa, Raph
Levien, Apache 2.0) models it.

  dx_alg   the 32 algorithms: one byte per operator, OP6 first (the order a
           voice stores them): bits 0-1 where it writes (0 the output, 1 bus
           one, 2 bus two), bit 2 add to it (else overwrite), bits 4-5 what
           it reads (0 nothing, 1 bus one, 2 bus two), bits 6+7 both set:
           feedback on itself. The routing of msfa's table (fm_core.cc);
           tests/test_dx.py checks it against the DX7's algorithm diagrams
           (ALGOS below).
  dx_e2    2^(-i/256), Q15, i = 0..255: a level in 1/256 octave -> a gain
  dx_sin   sin(2 pi (i + 0.5) / 8192), Q15: the operators' sine, read with the
           phase's top 13 bits (the half step makes the truncation a rounding)
  dx_sol   msfa's scaleoutlevel below 20 (output levels 0..19)
  dx_exps  msfa's exponential keyboard level scaling curve (33 groups)
  dx_pms   pitch modulation sensitivity 0..7 (msfa's pitchmodsenstab)
  dx_veld  msfa's velocity curve (velocity_data, 64 steps of velocity / 2)
  dx_lfor  the LFO's speed per block: msfa's measured rates (lfoSource, Hz)
           x 4437500000 x 32 / 48000, as msfa's lforatio for 32-sample blocks
  dx_pegr  the pitch EG's rates (msfa's pitchenv_rate), dx_pegl its levels
           (pitchenv_tab, signed, x 2^19 = Q24 octaves)
  dx_x2    2^(i/256), Q16, i = 0..256: a fraction of an octave -> a ratio
  dx_dtn   per MIDI note, one detune step's ratio change, Q16: msfa's
           0.0209 exp(-0.396 L) / 7 x L octaves (L the note's log2 Hz) x ln 2
  dx_stat  msfa's static hold per rate (ACCURATE_ENVELOPE's statics, samples
           at 44.1 kHz, x 44100 / 48000 as msfa's sr_multiplier)
  dx_amf   msfa's amplitude modulation curve, Q16 of the level taken off:
           exp(12.2 + 0.07 k) x 16 / 2^28 for k = 0..64 (k = depth / 2^18)
  dx_cart  the built-in cartridge: 32 voices in the DX7's packed 128-byte
           format (VMEM), all of them made here - INIT VOICE and a handful of
           simple original sounds. No Yamaha voice is included.

The numbers of dx_sol, dx_exps, dx_pms and dx_veld are msfa's (Apache License 2.0;
see docs/DX.md for the notice). Rerun after changing anything here; CI
checks that the file is up to date.
"""
import os

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = os.path.join(REPO, 'src', 'dx_tables.inc')

# msfa's algorithm table (fm_core.cc), OP6 .. OP1
DX_ALG = [
    (0xc1, 0x11, 0x11, 0x14, 0x01, 0x14),  # 1
    (0x01, 0x11, 0x11, 0x14, 0xc1, 0x14),  # 2
    (0xc1, 0x11, 0x14, 0x01, 0x11, 0x14),  # 3
    (0xc1, 0x11, 0x94, 0x01, 0x11, 0x14),  # 4
    (0xc1, 0x14, 0x01, 0x14, 0x01, 0x14),  # 5
    (0xc1, 0x94, 0x01, 0x14, 0x01, 0x14),  # 6
    (0xc1, 0x11, 0x05, 0x14, 0x01, 0x14),  # 7
    (0x01, 0x11, 0xc5, 0x14, 0x01, 0x14),  # 8
    (0x01, 0x11, 0x05, 0x14, 0xc1, 0x14),  # 9
    (0x01, 0x05, 0x14, 0xc1, 0x11, 0x14),  # 10
    (0xc1, 0x05, 0x14, 0x01, 0x11, 0x14),  # 11
    (0x01, 0x05, 0x05, 0x14, 0xc1, 0x14),  # 12
    (0xc1, 0x05, 0x05, 0x14, 0x01, 0x14),  # 13
    (0xc1, 0x05, 0x11, 0x14, 0x01, 0x14),  # 14
    (0x01, 0x05, 0x11, 0x14, 0xc1, 0x14),  # 15
    (0xc1, 0x11, 0x02, 0x25, 0x05, 0x14),  # 16
    (0x01, 0x11, 0x02, 0x25, 0xc5, 0x14),  # 17
    (0x01, 0x11, 0x11, 0xc5, 0x05, 0x14),  # 18
    (0xc1, 0x14, 0x14, 0x01, 0x11, 0x14),  # 19
    (0x01, 0x05, 0x14, 0xc1, 0x14, 0x14),  # 20
    (0x01, 0x14, 0x14, 0xc1, 0x14, 0x14),  # 21
    (0xc1, 0x14, 0x14, 0x14, 0x01, 0x14),  # 22
    (0xc1, 0x14, 0x14, 0x01, 0x14, 0x04),  # 23
    (0xc1, 0x14, 0x14, 0x14, 0x04, 0x04),  # 24
    (0xc1, 0x14, 0x14, 0x04, 0x04, 0x04),  # 25
    (0xc1, 0x05, 0x14, 0x01, 0x14, 0x04),  # 26
    (0x01, 0x05, 0x14, 0xc1, 0x14, 0x04),  # 27
    (0x04, 0xc1, 0x11, 0x14, 0x01, 0x14),  # 28
    (0xc1, 0x14, 0x01, 0x14, 0x04, 0x04),  # 29
    (0x04, 0xc1, 0x11, 0x14, 0x04, 0x04),  # 30
    (0xc1, 0x14, 0x04, 0x04, 0x04, 0x04),  # 31
    (0xc4, 0x04, 0x04, 0x04, 0x04, 0x04),  # 32
]

# The DX7's algorithm diagrams: (modulator -> modulated) pairs, the carriers,
# and the operator with feedback (ops numbered 1..6). Algorithms 4 and 6 loop
# the feedback from OP4 / OP5 back into OP6; msfa (and so this machine) feeds
# OP6 back on itself there instead.
ALGOS = {
    1: ([(6, 5), (5, 4), (4, 3), (2, 1)], {1, 3}, 6),
    2: ([(6, 5), (5, 4), (4, 3), (2, 1)], {1, 3}, 2),
    3: ([(6, 5), (5, 4), (3, 2), (2, 1)], {1, 4}, 6),
    4: ([(6, 5), (5, 4), (3, 2), (2, 1)], {1, 4}, 6),
    5: ([(6, 5), (4, 3), (2, 1)], {1, 3, 5}, 6),
    6: ([(6, 5), (4, 3), (2, 1)], {1, 3, 5}, 6),
    7: ([(6, 5), (5, 3), (4, 3), (2, 1)], {1, 3}, 6),
    8: ([(6, 5), (5, 3), (4, 3), (2, 1)], {1, 3}, 4),
    9: ([(6, 5), (5, 3), (4, 3), (2, 1)], {1, 3}, 2),
    10: ([(3, 2), (2, 1), (6, 4), (5, 4)], {1, 4}, 3),
    11: ([(3, 2), (2, 1), (6, 4), (5, 4)], {1, 4}, 6),
    12: ([(2, 1), (6, 3), (5, 3), (4, 3)], {1, 3}, 2),
    13: ([(2, 1), (6, 3), (5, 3), (4, 3)], {1, 3}, 6),
    14: ([(6, 4), (5, 4), (4, 3), (2, 1)], {1, 3}, 6),
    15: ([(6, 4), (5, 4), (4, 3), (2, 1)], {1, 3}, 2),
    16: ([(6, 5), (5, 1), (4, 3), (3, 1), (2, 1)], {1}, 6),
    17: ([(6, 5), (5, 1), (4, 3), (3, 1), (2, 1)], {1}, 2),
    18: ([(6, 5), (5, 4), (4, 1), (3, 1), (2, 1)], {1}, 3),
    19: ([(6, 5), (6, 4), (3, 2), (2, 1)], {1, 4, 5}, 6),
    20: ([(3, 1), (3, 2), (6, 4), (5, 4)], {1, 2, 4}, 3),
    21: ([(3, 1), (3, 2), (6, 4), (6, 5)], {1, 2, 4, 5}, 3),
    22: ([(2, 1), (6, 3), (6, 4), (6, 5)], {1, 3, 4, 5}, 6),
    23: ([(3, 2), (6, 4), (6, 5)], {1, 2, 4, 5}, 6),
    24: ([(6, 3), (6, 4), (6, 5)], {1, 2, 3, 4, 5}, 6),
    25: ([(6, 4), (6, 5)], {1, 2, 3, 4, 5}, 6),
    26: ([(3, 2), (6, 4), (5, 4)], {1, 2, 4}, 6),
    27: ([(3, 2), (6, 4), (5, 4)], {1, 2, 4}, 3),
    28: ([(5, 4), (4, 3), (2, 1)], {1, 3, 6}, 5),
    29: ([(4, 3), (6, 5)], {1, 2, 3, 5}, 6),
    30: ([(5, 4), (4, 3)], {1, 2, 3, 6}, 5),
    31: ([(6, 5)], {1, 2, 3, 4, 5}, 6),
    32: ([], {1, 2, 3, 4, 5, 6}, 6),
}

LEVELLUT = [0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46]
EXP_SCALE = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56, 66, 80,
             94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250]
PMS = [0, 10, 20, 33, 55, 92, 153, 255]
VELD = [0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
        166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202,
        205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232,
        233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251,
        252, 253, 254]
assert len(VELD) == 64
LFO_HZ = [0.062541, 0.125031, 0.312393, 0.437120, 0.624610, 0.750694, 0.936330, 1.125302,
          1.249609, 1.436782, 1.560915, 1.752081, 1.875117, 2.062494, 2.247191, 2.374451,
          2.560492, 2.686728, 2.873976, 2.998950, 3.188013, 3.369840, 3.500175, 3.682224,
          3.812065, 4.000800, 4.186202, 4.310716, 4.501260, 4.623209, 4.814636, 4.930480,
          5.121901, 5.315191, 5.434783, 5.617346, 5.750431, 5.946717, 6.062811, 6.248438,
          6.431695, 6.564264, 6.749460, 6.868132, 7.052186, 7.250580, 7.375719, 7.556294,
          7.687577, 7.877738, 7.993605, 8.181967, 8.372405, 8.504848, 8.685079, 8.810573,
          8.986341, 9.122423, 9.300595, 9.500285, 9.607994, 9.798158, 9.950249, 10.117361,
          11.251125, 11.384335, 12.562814, 13.676149, 13.904338, 15.092062, 16.366612,
          16.638935, 17.869907, 19.193858, 19.425019, 20.833333, 21.034918, 22.502250,
          24.003841, 24.260068, 25.746653, 27.173913, 27.578599, 29.052876, 30.693677,
          31.191516, 32.658393, 34.317090, 34.674064, 36.416606, 38.197097, 38.550501,
          40.387722, 40.749796, 42.625746, 44.326241, 44.883303, 46.772685, 48.590865,
          49.261084]
PEG_RATE = [1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14,
            14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34,
            36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70,
            72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110, 115, 120, 125, 130, 135, 141,
            147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255]
PEG_TAB = [-128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37,
           -35, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18,
           -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2,
           3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
           26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92,
           103, 115, 127]
assert len(LFO_HZ) == len(PEG_RATE) == len(PEG_TAB) == 100
STATICS = [1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
           584325, 507150, 502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500,
           176400, 145530, 145530, 125685, 110250, 110250, 88200, 88200, 74970, 61740, 61740, 55125,
           48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522, 17640, 15435, 14112, 13230,
           11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
           1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705, 573, 573, 529, 441, 441]
assert len(STATICS) == 77


def scale_velocity(vel, sens):
    """msfa's ScaleVelocity: the output level's change, in 1/256 octave"""
    v = VELD[max(0, min(127, vel)) >> 1] - 239
    return ((sens * v + 7) >> 3) << 4


# ---- the built-in voices ------------------------------------------------------
def op(r=(99, 99, 99, 99), l=(99, 99, 99, 0), ol=0, coarse=1, fine=0, det=7, mode=0,
       bp=39, ld=0, rd=0, lc=0, rc=0, rs=0, ams=0, kvs=0):
    return dict(r=r, l=l, ol=ol, coarse=coarse, fine=fine, det=det, mode=mode, bp=bp,
                ld=ld, rd=rd, lc=lc, rc=rc, rs=rs, ams=ams, kvs=kvs)


def voice(name, ops, alg=1, fb=0, oks=1, peg_r=(99, 99, 99, 99), peg_l=(50, 50, 50, 50),
          lfo=(35, 0, 0, 0, 1, 0), pms=3, tr=24):
    """ops: OP1..OP6 as op() dicts (missing ones are silent)"""
    ops = list(ops) + [op()] * (6 - len(ops))
    b = bytearray()
    for o in reversed(ops):                      # stored OP6 first
        b += bytes(o['r']) + bytes(o['l'])
        b += bytes([o['bp'], o['ld'], o['rd'], (o['rc'] << 2) | o['lc'],
                    (o['det'] << 3) | o['rs'], (o['kvs'] << 2) | o['ams'], o['ol'],
                    (o['coarse'] << 1) | o['mode'], o['fine']])
    b += bytes(peg_r) + bytes(peg_l)
    b += bytes([alg - 1, (oks << 3) | fb, lfo[0], lfo[1], lfo[2], lfo[3],
                (pms << 4) | (lfo[5] << 1) | lfo[4], tr])
    b += name.ljust(10)[:10].encode()
    assert len(b) == 128
    return bytes(b)


# Each voice's carriers are moved by TRIM steps of 0.75 dB so the voices play
# about equally loud (measured in the emulator: tests/test_dx.py checks it).
TRIM = {'INIT VOICE': 0, 'E.PIANO MT': -6, 'SYN BASS 1': -1, 'GLASS BELL': -1,
        'BRASS SWEL': -4, 'DRAWBARS 1': -2, 'FM PLUCK 1': 1, 'SOFT PAD 1': -5,
        'MARIMBA 1': 3, 'CLAV 1': 0, 'SINE BASS': 0}


def trimmed(name, ops, alg=1, **k):
    """voice(), with its carriers' levels moved by TRIM[name]"""
    carriers = ALGOS[alg][1]
    t = TRIM.get(name, 0)
    ops = [dict(o, ol=max(0, min(99, o['ol'] + t))) if i + 1 in carriers else o
           for i, o in enumerate(ops)]
    return voice(name, ops, alg=alg, **k)


def voices():
    init = voice('INIT VOICE', [op(ol=99)])
    v = [init]
    # electric piano: three carrier/modulator pairs (alg 5), the tine on 14:1
    v.append(trimmed('E.PIANO MT', [
        op(r=(90, 25, 25, 67), l=(99, 75, 0, 0), ol=98, rs=3, kvs=2),
        op(r=(95, 50, 35, 78), l=(99, 75, 0, 0), ol=78, rs=3, kvs=4),
        op(r=(90, 20, 20, 50), l=(99, 95, 0, 0), ol=96, rs=3, det=8, kvs=2),
        op(r=(95, 29, 20, 50), l=(99, 95, 0, 0), ol=74, coarse=14, rs=3, kvs=4),
        op(r=(90, 20, 20, 50), l=(99, 95, 0, 0), ol=88, det=6, rs=3, kvs=2),
        op(r=(95, 29, 20, 50), l=(99, 95, 0, 0), ol=68, rs=3, kvs=3)], alg=5, fb=6))
    # bass: a carrier under a stack of modulators with feedback (alg 1)
    v.append(trimmed('SYN BASS 1', [
        op(r=(92, 40, 30, 60), l=(99, 80, 70, 0), ol=99, coarse=0),
        op(r=(95, 55, 20, 60), l=(99, 60, 40, 0), ol=82, coarse=0, kvs=3),
        op(r=(92, 30, 30, 60), l=(99, 80, 70, 0), ol=90, coarse=0, det=9),
        op(r=(95, 60, 20, 60), l=(99, 50, 30, 0), ol=80, coarse=1, kvs=3),
        op(r=(95, 70, 20, 60), l=(99, 40, 20, 0), ol=70, coarse=1),
        op(r=(95, 70, 20, 60), l=(99, 40, 20, 0), ol=72, coarse=1)], alg=1, fb=5, tr=12))
    # bell: inharmonic modulator ratios, long decays (alg 5)
    v.append(trimmed('GLASS BELL', [
        op(r=(94, 22, 20, 30), l=(99, 70, 0, 0), ol=98),
        op(r=(95, 30, 25, 30), l=(99, 60, 0, 0), ol=80, coarse=3, fine=50, kvs=3),
        op(r=(94, 24, 20, 30), l=(99, 70, 0, 0), ol=92, coarse=2, det=10),
        op(r=(95, 40, 25, 30), l=(99, 50, 0, 0), ol=76, coarse=7, fine=7, kvs=3),
        op(r=(94, 18, 18, 30), l=(99, 70, 0, 0), ol=86, coarse=4, det=4),
        op(r=(95, 35, 25, 30), l=(99, 50, 0, 0), ol=70, coarse=11)], alg=5, fb=3))
    # brass: the modulators swell after the carriers (alg 22)
    v.append(trimmed('BRASS SWEL', [
        op(r=(60, 40, 30, 55), l=(99, 92, 90, 0), ol=99),
        op(r=(55, 35, 30, 55), l=(99, 85, 80, 0), ol=84, kvs=3),
        op(r=(60, 40, 30, 55), l=(99, 92, 90, 0), ol=96, det=9),
        op(r=(60, 40, 30, 55), l=(99, 92, 90, 0), ol=94, det=5),
        op(r=(60, 40, 30, 55), l=(99, 92, 90, 0), ol=92, coarse=2),
        op(r=(45, 30, 30, 55), l=(99, 80, 75, 0), ol=82, kvs=3)], alg=22, fb=7,
        lfo=(30, 40, 4, 0, 0, 4), pms=4))
    # organ: six carriers on drawbar ratios (alg 32)
    v.append(trimmed('DRAWBARS 1', [
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=94, coarse=0),
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=96, coarse=1),
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=90, coarse=2),
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=88, coarse=3),
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=84, coarse=4),
        op(r=(88, 99, 99, 70), l=(99, 99, 99, 0), ol=76, coarse=8)], alg=32, fb=4,
        lfo=(70, 0, 3, 0, 0, 4), pms=3))
    # pluck: a fast-decaying stack (alg 1)
    v.append(trimmed('FM PLUCK 1', [
        op(r=(94, 45, 30, 60), l=(99, 0, 0, 0), ol=99, rs=4),
        op(r=(97, 70, 30, 60), l=(99, 0, 0, 0), ol=88, coarse=3, rs=4, kvs=4),
        op(r=(94, 45, 30, 60), l=(99, 0, 0, 0), ol=94, rs=4),
        op(r=(97, 60, 30, 60), l=(99, 0, 0, 0), ol=82, coarse=1, rs=4, kvs=3),
        op(r=(97, 80, 30, 60), l=(99, 0, 0, 0), ol=72, coarse=5, rs=4),
        op(r=(97, 80, 30, 60), l=(99, 0, 0, 0), ol=60, coarse=1)], alg=1, fb=4))
    # pad: carriers swelling in over ~0.3 s, slightly detuned pairs (alg 5)
    v.append(trimmed('SOFT PAD 1', [
        op(r=(48, 30, 25, 40), l=(99, 92, 88, 0), ol=96),
        op(r=(44, 30, 25, 40), l=(99, 85, 80, 0), ol=72, coarse=2),
        op(r=(48, 30, 25, 40), l=(99, 92, 88, 0), ol=94, det=11),
        op(r=(44, 30, 25, 40), l=(99, 85, 80, 0), ol=68, coarse=1),
        op(r=(48, 30, 25, 40), l=(99, 92, 88, 0), ol=92, det=3),
        op(r=(44, 30, 25, 40), l=(99, 85, 80, 0), ol=66, coarse=3)], alg=5, fb=2,
        lfo=(25, 50, 5, 0, 0, 4), pms=3))
    # marimba: a short 4:1 strike over a sine (alg 5, two pairs)
    v.append(trimmed('MARIMBA 1', [
        op(r=(95, 38, 30, 50), l=(99, 0, 0, 0), ol=99, rs=3),
        op(r=(97, 70, 50, 50), l=(99, 0, 0, 0), ol=80, coarse=4, rs=3, kvs=4),
        op(r=(95, 50, 30, 50), l=(99, 0, 0, 0), ol=84, coarse=4, rs=3),
        op(r=(97, 80, 50, 50), l=(99, 0, 0, 0), ol=60, coarse=10, rs=3)], alg=5))
    # clav: a bright, nasal stack with a quick decay (alg 3)
    v.append(trimmed('CLAV 1', [
        op(r=(95, 45, 30, 70), l=(99, 40, 0, 0), ol=99),
        op(r=(97, 55, 30, 70), l=(99, 30, 0, 0), ol=86, coarse=3, kvs=4),
        op(r=(97, 60, 30, 70), l=(99, 20, 0, 0), ol=78, coarse=1, kvs=3),
        op(r=(95, 40, 30, 70), l=(99, 40, 0, 0), ol=92, coarse=2),
        op(r=(97, 50, 30, 70), l=(99, 30, 0, 0), ol=80, coarse=5),
        op(r=(97, 70, 30, 70), l=(99, 0, 0, 0), ol=60, coarse=1)], alg=3, fb=5))
    # sine bass: one carrier with a touch of its octave (alg 1)
    v.append(trimmed('SINE BASS', [
        op(r=(92, 40, 35, 60), l=(99, 88, 80, 0), ol=99, coarse=0),
        op(r=(95, 60, 40, 60), l=(99, 50, 30, 0), ol=70, coarse=1, kvs=3)], alg=1, tr=12))
    while len(v) < 32:
        v.append(init)
    return b''.join(v)


def main():
    lines = ['| Written by tools/gen_dx_tables.py - do not edit.', '    .align 2', 'dx_alg:']
    for i, a in enumerate(DX_ALG):
        lines.append('    .byte ' + ', '.join('0x%02x' % x for x in a) + '   | %d' % (i + 1))
    lines.append('    .align 2')
    lines.append('dx_e2:')
    e2 = [round(32767 * 2 ** (-i / 256)) for i in range(256)]
    for i in range(0, 256, 16):
        lines.append('    .word ' + ', '.join(str(x) for x in e2[i:i + 16]))
    lines.append('dx_sin:')
    sn = [round(32767 * __import__('math').sin(2 * __import__('math').pi * (i + 0.5) / 8192))
          for i in range(8192)]
    for i in range(0, 8192, 16):
        lines.append('    .word ' + ', '.join(str(x & 0xffff) for x in sn[i:i + 16]))
    lines.append('dx_sol:')
    lines.append('    .byte ' + ', '.join(str(x) for x in LEVELLUT))
    lines.append('dx_exps:')
    lines.append('    .byte ' + ', '.join(str(x) for x in EXP_SCALE))
    lines.append('dx_pms:')
    lines.append('    .byte ' + ', '.join(str(x) for x in PMS))
    import math
    lines.append('    .align 2')
    lines.append('dx_lfor:')
    lr = [round(h * 4437500000 * 32 / 48000) for h in LFO_HZ]
    for i in range(0, 100, 10):
        lines.append('    .long ' + ', '.join(str(x) for x in lr[i:i + 10]))
    lines.append('dx_x2:')
    x2 = [round(65536 * 2 ** (i / 256)) for i in range(257)]
    for i in range(0, 257, 16):
        lines.append('    .long ' + ', '.join(str(x) for x in x2[i:i + 16]))
    lines.append('dx_dtn:')
    dt = []
    for n in range(128):
        L = math.log2(440) + (n - 69) / 12
        dt.append(round(0.0209 * math.exp(-0.396 * L) / 7 * L * math.log(2) * 65536))
    for i in range(0, 128, 16):
        lines.append('    .word ' + ', '.join(str(x) for x in dt[i:i + 16]))
    lines.append('dx_amf:')
    am = [min(65535, round(math.exp(12.2 + 0.07 * k) * 16 / 2 ** 28 * 65536)) for k in range(65)]
    for i in range(0, 65, 13):
        lines.append('    .word ' + ', '.join(str(x) for x in am[i:i + 13]))
    lines.append('dx_stat:')
    st = [STATICS[r] if r < 77 else 20 * (99 - r) for r in range(100)]
    st = [x * round(44100 / 48000 * (1 << 24)) >> 24 for x in st]
    for i in range(0, 100, 10):
        lines.append('    .long ' + ', '.join(str(x) for x in st[i:i + 10]))
    lines.append('dx_pegr:')
    lines.append('    .byte ' + ', '.join(str(x) for x in PEG_RATE))
    lines.append('dx_pegl:')
    lines.append('    .byte ' + ', '.join(str(x & 0xff) for x in PEG_TAB))
    lines.append('    .align 2')
    lines.append('dx_veld:')
    for i in range(0, 64, 16):
        lines.append('    .byte ' + ', '.join(str(x) for x in VELD[i:i + 16]))
    lines.append('    .align 2')
    lines.append('dx_cart:')
    c = voices()
    for i in range(0, len(c), 16):
        lines.append('    .byte ' + ', '.join(str(x) for x in c[i:i + 16]))
    lines.append('    .align 2')
    open(OUT, 'w').write('\n'.join(lines) + '\n')
    print(f"wrote {os.path.relpath(OUT, REPO)}")


if __name__ == '__main__':
    main()
