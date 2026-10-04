"""Host-side harness for Model-TG's blob: runs the ASSEMBLED code (build/_b.bin,
linked at 0x401ab750) on Unicorn's ColdFire V4e, with the stock OS replaced by
a sea of `rts`. Any call into stock code therefore returns at once, and every
jump into it (the hooks' exits) is caught as a stop address.

This proves what our own code does with the state it is given - registers,
memory, which of our routines run, what they write and nothing else. It
cannot prove what the real stock code does around it; that needs hardware.

    pip install unicorn numpy
"""
import os, struct, subprocess, shutil
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UcError
from unicorn.m68k_const import *

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
BUILD = os.path.join(REPO, 'build')
BLOB = 0x401ab750
M32 = 0xffffffff

# memory map
STOCK_LO, STOCK_HI = 0x40000000, 0x42400000      # stock OS, RAM, our blob
TIMER = 0xfc000000                                 # 0xfc07800c: the profile timer
SCRATCH = 0x42000000                               # voices, trackData, stacks
RET = 0x42300000                                   # return address: a stop
STOCK_CODE_END = 0x400fd000                        # stock code below, data above
STUB_RENDER = 0x400fc000                           # stand-ins for the machines'
STUB_PARAM = 0x400fc100                            # render / parameter functions
STACK_TOP = 0x422f0000

D = [UC_M68K_REG_D0 + i for i in range(8)]
A = [UC_M68K_REG_A0 + i for i in range(8)]


def cross():
    return os.environ.get('CROSS') or next(
        (c for c in ('m68k-elf-', 'm68k-linux-gnu-') if shutil.which(c + 'as')), 'm68k-elf-')


def ensure_built():
    """Assemble and self-check src/ (build.py --assemble-only) once."""
    elf = os.path.join(BUILD, '_b.elf')
    if not os.environ.get('VA_TEST_NO_BUILD'):
        subprocess.run(['python3', os.path.join(REPO, 'build.py'), '--assemble-only'],
                       check=True, capture_output=True)
    return elf


def symbols(elf):
    out = subprocess.run([cross() + 'nm', elf], capture_output=True, text=True, check=True).stdout
    s = {}
    for l in out.splitlines():
        p = l.split()
        if len(p) == 3:
            s[p[2]] = int(p[0], 16)
    return s


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


class Emu:
    def __init__(self, build_dir=None):
        """This tree's blob, or another tree's already-built one (build_dir
        holding _b.elf / _b.bin) - the regression tests load the upstream
        Model-TG blob this fork is based on this way."""
        if build_dir is None:
            elf = ensure_built()
            build_dir = BUILD
        else:
            elf = os.path.join(build_dir, '_b.elf')
        self.sym = symbols(elf)
        with open(os.path.join(build_dir, '_b.bin'), 'rb') as f:
            self.blob = f.read()
        self.reset()

    def reset(self):
        uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        uc.mem_map(STOCK_LO, STOCK_HI - STOCK_LO)
        # the stock OS's code: rts everywhere, so a jsr into it returns at
        # once; its data (from 0x400fd000: vtables, tables, strings) and RAM
        # stay zero, so nothing reads an rts as a pointer
        uc.mem_write(STOCK_LO, b'\x4e\x75' * ((STOCK_CODE_END - STOCK_LO) // 2))
        # the stock machines' parameter and render functions (0x40118628 /
        # 0x40118610, six each): distinct stubs, so a trace shows which ran
        for m in range(6):
            uc.mem_write(0x40118610 + 4 * m, struct.pack('>I', STUB_RENDER + 2 * m))
            uc.mem_write(0x40118628 + 4 * m, struct.pack('>I', STUB_PARAM + 2 * m))
        uc.mem_write(BLOB, self.blob)
        uc.mem_map(TIMER, 0x100000)
        # the sample region (Pluck's delay lines, the retrig histories, the
        # slice tables live at its top); host pages are only touched if used
        uc.mem_map(0x48000000, 0x08000000)
        self.uc = uc
        self.visits = {}
        self._watch = {}
        self._hooked = False

    # ---- memory
    def w32(self, a, v): self.uc.mem_write(a, struct.pack('>I', v & M32))
    def w16(self, a, v): self.uc.mem_write(a, struct.pack('>H', v & 0xffff))
    def w8(self, a, v): self.uc.mem_write(a, bytes([v & 0xff]))
    def r32(self, a): return struct.unpack('>I', self.uc.mem_read(a, 4))[0]
    def r16(self, a): return struct.unpack('>H', self.uc.mem_read(a, 2))[0]
    def r8(self, a): return self.uc.mem_read(a, 1)[0]
    def rs32(self, a): return s32(self.r32(a))
    def arr(self, name, i, size=4):
        return self.sym[name] + i * size
    def blob_now(self):
        return bytes(self.uc.mem_read(BLOB, len(self.blob)))

    # ---- execution
    def watch(self, *names):
        """Count how often execution reaches each of these symbols."""
        for n in names:
            a = self.sym[n] if isinstance(n, str) else n
            self._watch[a] = n
            self.visits[n] = 0
        if not getattr(self, '_hooked', False):
            def hook(uc, addr, size, ud):
                n = self._watch.get(addr)
                if n is not None:
                    self.visits[n] += 1
            self.uc.hook_add(UC_HOOK_CODE, hook)
            self._hooked = True

    def count_instructions(self, start, stop, regs=None, stack=()):
        n = [0]
        h = self.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: n.__setitem__(0, n[0] + 1),
                             begin=BLOB, end=BLOB + len(self.blob))
        self.run(start, stop, regs, stack)
        self.uc.hook_del(h)
        return n[0]

    def run(self, start, stop=RET, regs=None, stack=(), limit=20_000_000):
        """Run from `start` (a symbol or address) with these registers and
        these longs on the stack above the return address RET, until `stop`."""
        uc = self.uc
        sp = STACK_TOP
        for v in reversed(list(stack)):
            sp -= 4
            self.w32(sp, v)
        sp -= 4
        self.w32(sp, RET)
        for r in A[:7] + D:
            uc.reg_write(r, 0)
        uc.reg_write(UC_M68K_REG_A7, sp)
        for k, v in (regs or {}).items():
            uc.reg_write(k, v & M32)
        a = self.sym[start] if isinstance(start, str) else start
        stop = self.sym[stop] if isinstance(stop, str) else stop
        try:
            uc.emu_start(a, stop, count=limit)
        except UcError as ex:
            raise RuntimeError(f"{ex} at pc 0x{uc.reg_read(UC_M68K_REG_PC):08x}") from None
        pc = uc.reg_read(UC_M68K_REG_PC)
        if pc != stop:
            raise RuntimeError(f"stopped at 0x{pc:08x}, not 0x{stop:08x}")
        return sp + 4

    def run_to_any(self, start, stops, regs=None, stack=(), limit=20_000_000):
        """As run(), stopping at whichever of `stops` is reached first;
        returns that address."""
        stops = set(stops)
        hit = []

        def hook(uc, addr, size, ud):
            if addr in stops:
                hit.append(addr)
                uc.emu_stop()
        h = self.uc.hook_add(UC_HOOK_CODE, hook)
        try:
            self.run(start, RET, regs, stack, limit)
        except RuntimeError:
            if not hit:
                raise
        finally:
            self.uc.hook_del(h)
        if not hit:
            raise RuntimeError("returned without reaching any exit")
        return hit[0]

    def d(self, i): return self.uc.reg_read(D[i])
    def a(self, i): return self.uc.reg_read(A[i])
    def sp(self): return self.uc.reg_read(UC_M68K_REG_A7)


# ---- a bit-exact model of va_fill, for comparison --------------------------
VA_DRIFT = 40
VA_DET = None


def det_table():
    global VA_DET
    if VA_DET is None:
        import re
        t = open(os.path.join(REPO, 'src', 'va_tables.inc')).read()
        VA_DET = [int(x) for l in re.findall(r'\.long ([0-9,]+)', t) for x in l.split(',')]
    return VA_DET


def dial(v):
    v = s32(v if v < 0x8000 else v - 0x10000)
    if v < 0:
        v = 0
    v >>= 8
    return min(v, 127)


def wave_code(d):
    return 0 if d < 43 else 1 if d < 86 else 2


class VARef:
    """The VA's per-track state and va_params / va_fill, as integers."""
    def __init__(self):
        self.w1 = [0] * 6; self.w2 = [0] * 6
        self.rat = [65536] * 6; self.mix = [128] * 6
        self.drift = [[0, 0] for _ in range(6)]
        self.seed = 0x6d2b79f5
        self.ph = [[0, 0, 0] for _ in range(6)]
        self.trig = [0] * 6

    def params(self, t, words):        # words: trackData +22, +24, +26, +28 (u16)
        self.w1[t] = wave_code(dial(words[0]))
        self.w2[t] = wave_code(dial(words[1]))
        self.rat[t] = det_table()[dial(words[2])]
        m = dial(words[3]) * 2
        self.mix[t] = 256 if m >= 254 else m

    @staticmethod
    def blep(u, R):
        x = (((u >> 8) * R) & M32) >> 16
        t = (32768 - x) & M32
        return ((t * t) & M32) >> 15

    def osc(self, acc, p, inc, wgt, w):
        if wgt == 0:
            return (p + ((inc << 6) & M32)) & M32
        k = inc >> 8 or 1
        R = 0x80000000 // k
        for i in range(64):
            if w == 2:
                v = s32((p + 0xc0000000) & M32) >> 16
                v = -2 * abs(v) + 32768
            elif w == 0:
                v = s32((p + 0x80000000) & M32) >> 16
                if p < inc:
                    v += self.blep(p, R)
                elif (-p) & M32 < inc:
                    v -= self.blep((-p) & M32, R)
            else:
                v = 32767 if p < 0x80000000 else -32767
                q = p ^ 0x80000000
                if p < inc:
                    v -= self.blep(p, R)
                elif (-p) & M32 < inc:
                    v += self.blep((-p) & M32, R)
                elif q < inc:
                    v += self.blep(q, R)
                elif (-q) & M32 < inc:
                    v -= self.blep((-q) & M32, R)
            acc[i] = s32(acc[i] + s32(v * wgt))
            p = (p + inc) & M32
        return p

    def walk(self, t, o, down):
        n = self.drift[t][o] + (-1 if down else 1)
        if -VA_DRIFT <= n <= VA_DRIFT:
            self.drift[t][o] = n

    def fill(self, t, step):
        if step == 0:
            step = 0x10000
        inc1 = ((step * 357) & M32)
        inc1 = (inc1 + (((step * 52) & M32) >> 8)) & M32
        inc1 >>= 1
        r = self.rat[t] >> 1
        inc2 = (((inc1 >> 15) * r) & M32) + ((((inc1 & 0x7fff) * r) & M32) >> 15)
        inc2 &= M32
        self.seed = (self.seed * 1664525 + 1013904223) & M32
        rnd = self.seed
        self.walk(t, 0, bool(rnd & 0x80000000))
        inc1 = (inc1 + s32((inc1 >> 16) * self.drift[t][0])) & M32
        self.walk(t, 1, bool(rnd & 0x40000000))
        inc2 = (inc2 + s32((inc2 >> 16) * self.drift[t][1])) & M32
        if self.trig[t]:
            self.trig[t] = 0
            self.ph[t][0] = 0x80000000
            self.ph[t][1] = ((rnd >> 3) + 0x70000000) & M32
        acc = [0] * 64
        self.ph[t][0] = self.osc(acc, self.ph[t][0], inc1, 256 - self.mix[t], self.w1[t])
        self.ph[t][1] = self.osc(acc, self.ph[t][1], inc2, self.mix[t], self.w2[t])
        out = []
        for a in acc:
            v = max(-32767, min(32767, a >> 8))
            out.append((v << 15) & M32)
        return out, inc1, inc2
