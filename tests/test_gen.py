#!/usr/bin/env python3
"""Host tests for the trig generator (src/gen.inc). HOST verification only.

The assembled blob runs on Unicorn with the stock OS as `rts` stubs
(tests/va_emu.py). The pattern routines GEN calls - length, trig on/off, trig
note - are stood in for by tiny stubs whose calls are recorded, so the tests
see exactly what GEN asks the stock code to write.

    python3 -m unittest discover -s tests -v
"""
import os, sys, struct, subprocess, unittest, random

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from va_emu import *  # noqa

OBJ = SCRATCH + 0x5000                  # a stand-in track pattern object
SCALES = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10],
          [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10],
          [0, 1, 3, 5, 6, 8, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 3, 5, 7, 9, 11],
          [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10], list(range(12))]
EMU = None


def emu():
    global EMU
    if EMU is None:
        EMU = Emu()
    else:
        EMU.reset()
    EMU.uc.mem_map(0x80000000, 0x10000)          # the audio clock 0x8000184c
    return EMU


def setv(e, name, v):
    e.w32(e.sym[name], v)


def euclid_ref(n, k, rot):
    return [1 if (((i - rot) % n) * k) % n < k else 0 for i in range(n)]


def hit(e, step, n):
    e.run('gen_hit', regs={D[2]: step, D[3]: n})
    return e.d(0)


class TEuclid(unittest.TestCase):
    def test_every_length_and_count(self):
        e = emu()
        setv(e, 'gen_c_mod', 1)
        rnd = random.Random(3)
        for n in list(range(1, 17)) + [24, 32, 48, 64]:
            for k in range(1, n + 1):
                rot = rnd.randrange(64)
                setv(e, 'gen_c_hit', k - 1); setv(e, 'gen_c_rot', rot)
                got = [hit(e, i, n) for i in range(n)]
                self.assertEqual(got, euclid_ref(n, k, rot % n), (n, k, rot))
                # a Euclidean rhythm: k trigs, gaps differing by at most one
                self.assertEqual(sum(got), k)
                on = [i for i, x in enumerate(got) if x]
                gaps = [(on[(j + 1) % k] - on[j]) % n or n for j in range(k)]
                self.assertLessEqual(max(gaps) - min(gaps), 1, (n, k, got))

    def test_known_rhythms_and_rotation(self):
        e = emu()
        setv(e, 'gen_c_mod', 1); setv(e, 'gen_c_rot', 0)
        for n, k, want in ((8, 3, 'x..x..x.'), (16, 4, 'x...x...x...x...'),
                           (8, 5, 'x.xx.xx.'), (16, 5, 'x..x..x..x..x...')):
            setv(e, 'gen_c_hit', k - 1)
            got = ''.join('x' if hit(e, i, n) else '.' for i in range(n))
            self.assertEqual(sorted(got), sorted(want))
            self.assertEqual(got, ''.join('x' if v else '.' for v in euclid_ref(n, k, 0)))
        setv(e, 'gen_c_hit', 2); setv(e, 'gen_c_rot', 1)
        self.assertEqual(''.join('x' if hit(e, i, 8) else '.' for i in range(8)), '.x..x..x')

    def test_more_hits_than_steps(self):
        e = emu()
        setv(e, 'gen_c_mod', 1); setv(e, 'gen_c_hit', 63); setv(e, 'gen_c_rot', 5)
        self.assertEqual([hit(e, i, 12) for i in range(12)], [1] * 12)


class TRandom(unittest.TestCase):
    def test_density(self):
        e = emu()
        setv(e, 'gen_c_mod', 0)
        for dns in (0, 10, 50, 90, 100):
            setv(e, 'gen_c_dns', dns)
            n = sum(hit(e, 0, 16) for _ in range(2000))
            if dns in (0, 100):
                self.assertEqual(n, 20 * dns)
            else:
                self.assertAlmostEqual(n / 2000, dns / 100, delta=0.04)

    def test_seed_moves(self):
        e = emu()
        seeds = set()
        for clk in range(20):
            e.w32(0x8000184c, clk * 977)
            e.run('gen_mix')
            seeds.add(e.r32(e.sym['gen_seed']))
        self.assertEqual(len(seeds), 20)
        self.assertNotIn(0, seeds)


class TNotes(unittest.TestCase):
    def cands(self, e):
        e.run('gen_cands')
        n = e.r32(e.sym['gen_ncand'])
        return list(e.uc.mem_read(e.sym['gen_cand'], n))

    def test_scales_keys_octaves(self):
        e = emu()
        for scl, degs in enumerate(SCALES):
            for key in (0, 2, 7, 11):
                for octv in (0, 4, 8):
                    for rng in range(4):
                        for name, v in (('gen_c_scl', scl), ('gen_c_key', key),
                                        ('gen_c_oct', octv), ('gen_c_rng', rng)):
                            setv(e, name, v)
                        lo = 12 * (octv + 1) + key
                        want = [lo + s for s in range(12 * (rng + 1) + 1)
                                if s % 12 in degs and lo + s <= 127]
                        self.assertEqual(self.cands(e), want, (scl, key, octv, rng))
        # OCT 4, KEY C: from 60, the track's default note
        setv(e, 'gen_c_scl', 0); setv(e, 'gen_c_key', 0); setv(e, 'gen_c_oct', 4)
        setv(e, 'gen_c_rng', 0)
        self.assertEqual(self.cands(e), [60, 62, 64, 65, 67, 69, 71, 72])

    def test_pick_covers_the_set(self):
        e = emu()
        setv(e, 'gen_c_scl', 9); setv(e, 'gen_c_rng', 1)
        cand = self.cands(e)
        seen = {}
        for _ in range(1500):
            e.run('gen_pick')
            seen[e.d(0)] = seen.get(e.d(0), 0) + 1
        self.assertEqual(sorted(seen), cand)
        self.assertGreater(min(seen.values()), 1500 / len(cand) / 2)


class TGo(unittest.TestCase):
    """GEN's press, against recorded stand-ins for the stock pattern routines."""
    def run_go(self, e, length, track=3):
        code = lambda a, h: e.uc.mem_write(a, bytes.fromhex(h))
        code(0x400cf866, '7055 4e75')                         # ui = 0x55
        code(0x4000cfcc, '203c' + f'{OBJ:08x}' + '4e75')      # the track's object
        code(0x40016402, f'70{length:02x} 4e75')
        setv(e, 'gm_track', track)
        calls = []

        def hook(uc, addr, size, ud):
            sp = uc.reg_read(UC_M68K_REG_A7)
            args = struct.unpack('>3i', uc.mem_read(sp + 4, 12))
            calls.append((addr,) + args)
        h = e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x4000cfcc, end=0x4000cfcc)
        h2 = e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x40017b48, end=0x40017b48)
        h3 = e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x40016642, end=0x40016642)
        h4 = e.uc.hook_add(UC_HOOK_CODE, hook, begin=0x400166c2, end=0x400166c2)
        args = e.run('gen_go', stack=[0, 0])
        for x in (h, h2, h3, h4):
            e.uc.hook_del(x)
        self.assertEqual(e.sp(), args)                          # balanced
        return calls

    def test_euclid_with_notes(self):
        e = emu()
        for name, v in (('gen_c_mod', 1), ('gen_c_hit', 4), ('gen_c_rot', 2),
                        ('gen_c_not', 1), ('gen_c_scl', 10), ('gen_c_key', 9),
                        ('gen_c_oct', 3), ('gen_c_rng', 1)):
            setv(e, name, v)
        calls = self.run_go(e, 16)
        self.assertEqual(calls[0][:3], (0x4000cfcc, 0x55, 3))   # the menu's track
        trig = [c for c in calls if c[0] == 0x40017b48]
        note = [c for c in calls if c[0] == 0x40016642]
        self.assertEqual([c[2] for c in trig], list(range(16)))
        self.assertTrue(all(c[1] == OBJ for c in trig + note))
        self.assertEqual([c[3] for c in trig], euclid_ref(16, 5, 2))
        lo = 12 * 4 + 9
        allowed = [lo + s for s in range(25) if s % 12 in SCALES[10]]
        for t, n in zip(trig, note):
            self.assertEqual(n[2], t[2])
            if t[3]:
                self.assertIn(n[3], allowed)
            else:
                self.assertEqual(n[3], -1)                      # cleared step: default
        self.assertEqual(e.r32(e.sym['gen_args']), 5)           # "Trigs: 5"
        vel = [c for c in calls if c[0] == 0x400166c2]
        self.assertEqual([(c[1], c[2]) for c in vel], [(OBJ, i) for i in range(16)])
        self.assertTrue(all(c[3] == -1 for c in vel))           # VEL OFF: the track's

    def test_random_velocity(self):
        e = emu()
        for name, v in (('gen_c_mod', 1), ('gen_c_hit', 63), ('gen_c_vel', 1),
                        ('gen_c_vmn', 99), ('gen_c_vmx', 39)):  # 100 / 40: either way round
            setv(e, name, v)
        calls = self.run_go(e, 64)
        vel = [c[3] for c in calls if c[0] == 0x400166c2]
        self.assertEqual(len(vel), 64)
        self.assertTrue(all(40 <= v <= 100 for v in vel), vel)
        self.assertGreater(len(set(vel)), 20)
        # empty steps go back to the track's velocity
        setv(e, 'gen_c_mod', 0); setv(e, 'gen_c_dns', 30)
        calls = self.run_go(e, 64)
        trig = [c[3] for c in calls if c[0] == 0x40017b48]
        vel = [c[3] for c in calls if c[0] == 0x400166c2]
        for t, v in zip(trig, vel):
            self.assertTrue(40 <= v <= 100 if t else v == -1)

    def test_velocity_bounds(self):
        e = emu()
        seen = set()
        for lo, hi in ((0, 0), (126, 126), (0, 126), (126, 0)):
            setv(e, 'gen_c_vmn', lo); setv(e, 'gen_c_vmx', hi)
            for _ in range(400):
                e.run('gen_velo')
                v = e.d(0)
                self.assertTrue(min(lo, hi) + 1 <= v <= max(lo, hi) + 1, (lo, hi, v))
                if (lo, hi) == (0, 126):
                    seen.add(v)
            self.assertEqual(e.d(2), 0)                         # d2 kept
        self.assertGreater(len(seen), 100)

    def test_random_without_notes(self):
        e = emu()
        setv(e, 'gen_c_mod', 0); setv(e, 'gen_c_dns', 40); setv(e, 'gen_c_not', 0)
        calls = self.run_go(e, 64)
        trig = [c for c in calls if c[0] == 0x40017b48]
        note = [c for c in calls if c[0] == 0x40016642]
        self.assertEqual(len(trig), 64)
        self.assertTrue(10 < sum(c[3] for c in trig) < 45)
        self.assertTrue(all(c[3] == -1 for c in note))          # the track's note

    def test_no_track_no_length(self):
        e = emu()
        e.uc.mem_write(0x4000cfcc, bytes.fromhex('7000 4e75'))  # no object
        calls = []
        h = e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: calls.append(a),
                          begin=0x40017b48, end=0x40017b48)
        e.run('gen_go', stack=[0, 0])
        self.assertEqual(calls, [])
        calls2 = [c for c in self.run_go(e, 0) if c[0] != 0x4000cfcc]
        self.assertEqual(calls2, [])
        e.uc.hook_del(h)


class TMenu(unittest.TestCase):
    def test_turn_clamps(self):
        e = emu()
        for lab, name, top in (('gen_rot_dns', 'gen_c_dns', 100), ('gen_rot_hit', 'gen_c_hit', 63),
                               ('gen_rot_scl', 'gen_c_scl', 12), ('gen_rot_oct', 'gen_c_oct', 8),
                               ('gen_rot_rng', 'gen_c_rng', 3), ('gen_rot_mod', 'gen_c_mod', 1),
                               ('gen_rot_vel', 'gen_c_vel', 1), ('gen_rot_vmn', 'gen_c_vmn', 126),
                               ('gen_rot_vmx', 'gen_c_vmx', 126)):
            setv(e, name, 0)
            e.run(lab, stack=[0, 0, 5])
            self.assertEqual(e.r32(e.sym[name]), min(5, top))
            e.run(lab, stack=[0, 0, 500])
            self.assertEqual(e.r32(e.sym[name]), top)
            e.run(lab, stack=[0, 0, -1000 & M32])
            self.assertEqual(e.r32(e.sym[name]), 0)

    def test_descriptor(self):
        e = emu()
        d = e.sym['gen_desc']
        self.assertEqual(e.r32(d + 8), 13)
        items = e.r32(d + 12)
        for r in range(13):
            for c in range(4):
                self.assertNotEqual(e.r32(items + 16 * r + 4 * c), 0)
        self.assertEqual(e.r32(items + 16 * 12 + 12), e.sym['gen_go'])

    def test_settings_track_reaches_the_menu(self):
        """SETTINGS held + a fresh TRACK press goes to the menu opener; with no
        menu machinery up (0x40fe4178 = 0) nothing opens and the key passes."""
        e = emu()
        e.uc.reg_write(UC_M68K_REG_SR, 0x2000)                 # supervisor, as the UI task
        e.watch('gen_menu_open')
        EV = SCRATCH + 0x9000
        e.w32(EV + 12, 2); e.w32(EV + 16, 1)                    # TRACK, down
        setv(e, 'set_held', 1)
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.visits['gen_menu_open'], 1)
        self.assertEqual(e.d(0), 2)                             # passed on
        self.assertEqual(e.r32(e.sym['kh_eaten6']), 0)
        setv(e, 'set_held', 0)                                  # TRACK alone: stock
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.visits['gen_menu_open'], 1)

    def test_swallowed_when_open(self):
        """When the menu opens (mm_opened), the press and its release are eaten."""
        e = emu()
        # gen_menu_open stand-in: set mm_opened and return
        mm = e.sym['mm_opened']
        stub = SCRATCH + 0xa000
        e.uc.mem_write(stub, bytes.fromhex('7001 23c0' + f'{mm:08x}' + '4e75'))
        e.uc.mem_write(e.sym['gen_menu_open'], bytes.fromhex('4ef9' + f'{stub:08x}'))
        EV = SCRATCH + 0x9000
        e.w32(EV + 12, 2); e.w32(EV + 16, 1)
        setv(e, 'set_held', 1)
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.d(0), 0)
        self.assertEqual(e.r32(EV + 16) & 8, 8)                 # marked: no stock action
        self.assertEqual(e.r32(e.sym['mod_used']), 1)
        e.w32(EV + 16, 0)                                       # the release
        e.run('key_hook', stack=[EV])
        self.assertEqual(e.d(0), 0)
        self.assertEqual(e.r32(e.sym['kh_eaten6']), 0)



@unittest.skipUnless(os.environ.get('MODEL_CYCLES_STOCK'), "set MODEL_CYCLES_STOCK for the real-pattern test")
class TRealPattern(unittest.TestCase):
    """GEN through the REAL stock trig / note setters, in the full image, on a
    stand-in pattern object: vtable +40 hands out the 722-byte block, +16 (the
    observers) and +60 (the edit notice) return, and the length is the
    block's own +713 (object +60 = 0 selects that path)."""
    @classmethod
    def setUpClass(cls):
        stock = os.environ['MODEL_CYCLES_STOCK']
        tool = os.environ.get('ELEKTRON_FIRMWARE_TOOL', 'elektron-firmware-tool')
        r = subprocess.run([sys.executable, os.path.join(REPO, 'build.py'), '--stock', stock,
                            '--tool', tool, '--out', os.path.join(BUILD, 'test-gen.syx')],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
        cls.img = open(os.path.join(BUILD, 'Model-TG.bin'), 'rb').read()
        cls.sym = symbols(os.path.join(BUILD, '_b.elf'))

    def test_trigs_and_notes_land_in_the_block(self):
        from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
        uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        uc.mem_map(0x40000000, 0x10000000)
        uc.mem_map(0x80000000, 0x100000)
        uc.mem_write(0x40000400, self.img)
        sym = self.sym
        W = lambda a, v: uc.mem_write(a, struct.pack('>I', v & M32))
        OBJ, VT, BLK, RTS, GETB = (SCRATCH + 0x100, SCRATCH + 0x200, SCRATCH + 0x1000,
                                   SCRATCH + 0x400, SCRATCH + 0x410)
        uc.mem_write(RTS, bytes.fromhex('4e75'))
        uc.mem_write(GETB, bytes.fromhex('203c' + f'{BLK:08x}' + '4e75'))
        for i in range(32):
            W(VT + 4 * i, RTS)
        W(VT + 40, GETB)
        W(OBJ, VT); W(OBJ + 60, 0)
        blk = bytearray(722)
        for st in range(64):                            # an old pattern: all trigs,
            blk[2 * st:2 * st + 2] = b'\x08\x01'       # note-trig override, notes
            blk[580 + st] = 30
            blk[128 + st] = 50
        blk[713:715] = struct.pack('>h', 24)            # 24 steps
        uc.mem_write(BLK, bytes(blk))
        # the track lookup: our object
        uc.mem_write(0x400cf866, bytes.fromhex('7001 4e75'))
        uc.mem_write(0x4000f208, bytes.fromhex('4e75'))
        uc.mem_write(0x4000cfcc, bytes.fromhex('203c' + f'{OBJ:08x}' + '4e75'))
        for n, v in (('gen_c_mod', 1), ('gen_c_hit', 6), ('gen_c_rot', 0), ('gen_c_not', 1),
                     ('gen_c_scl', 0), ('gen_c_key', 0), ('gen_c_oct', 4), ('gen_c_rng', 0),
                     ('gen_c_vel', 1), ('gen_c_vmn', 19), ('gen_c_vmx', 29), ('gm_track', 2)):
            W(sym[n], v)
        sp = 0x4fe00000
        for v in (0, 0, RET):
            sp -= 4; W(sp, v)
        uc.reg_write(UC_M68K_REG_SR, 0x2700)
        uc.reg_write(UC_M68K_REG_A7, sp)
        uc.emu_start(sym['gen_go'], RET, count=5_000_000)
        self.assertEqual(uc.reg_read(UC_M68K_REG_PC), RET)
        out = bytes(uc.mem_read(BLK, 722))
        flags = [struct.unpack('>H', out[2 * i:2 * i + 2])[0] for i in range(64)]
        want = euclid_ref(24, 7, 0)
        self.assertEqual([f & 1 for f in flags[:24]], want)
        self.assertTrue(all(f & 0x800 == 0 for f in flags[:24]))   # overrides cleared
        self.assertEqual(flags[24:], [0x0801] * 40)                # past the length: untouched
        notes = [struct.unpack('b', out[580 + i:581 + i])[0] for i in range(64)]
        for i in range(24):
            if want[i]:
                self.assertIn(notes[i], [60, 62, 64, 65, 67, 69, 71, 72])
            else:
                self.assertEqual(notes[i], -1)
        self.assertEqual(notes[24:], [30] * 40)
        vels = [struct.unpack('b', out[128 + i:129 + i])[0] for i in range(64)]
        for i in range(24):
            self.assertTrue(20 <= vels[i] <= 30 if want[i] else vels[i] == -1, (i, vels[i]))
        self.assertEqual(vels[24:], [50] * 40)


if __name__ == '__main__':
    unittest.main(verbosity=2)
