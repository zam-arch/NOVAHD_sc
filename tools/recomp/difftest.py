#!/usr/bin/env python3
"""Differential test: original functions in Unicorn (ARM32) against their translations compiled for the host.

The whole library is translated and built into one DLL, so a tested function's callees run translated too.
Both sides start from the same game memory (relocated image, scratch buffers, stack, heap) and the same CPU
state. Imports run the models in models.py on both sides; kernel helpers run in rc_kuser on the host and in
an equivalent hook in Unicorn. After the function returns, every core register, the flags, FPSCR's NZCV,
the VFP registers and every writable byte are compared. NaNs compare equal to any NaN: x86's default NaN
has the opposite sign to ARM's, and arm64 behaves like ARM here.

  py -3 tools/recomp/difftest.py --exe NovaHD --sample 1000 --runs 16 --out recovered/recomp/m1

Thumb functions start in Unicorn with the Thumb bit set; the translated side does not care about the mode.
"""
from __future__ import annotations

import argparse
import re
from compile_plan import compile_plan
import csv
import ctypes
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
import time
from array import array
from pathlib import Path

from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM, UC_PROT_EXEC, UC_PROT_READ, Uc, UcError
from unicorn import arm_const as U

sys.path.insert(0, str(Path(__file__).parent))
from macho import IMAGE_BASE, IMPORT_STUB_BASE, Image  # noqa: E402
from lift import Lifter, Unsupported, branch_targets, entries  # noqa: E402
import models  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x02000000, 0x100000
STACK, STACK_SIZE = 0x03000000, 0x100000
SP = STACK + 0xF0000
SENTINEL = 0x04000000
KUSER = 0xFFFF0000
HOST_SIZE = models.HEAP + models.HEAP_SIZE
RUNTIME = Path(__file__).parent / "runtime"
BX_LR = (0xE12FFF1E).to_bytes(4, "little")
SHARDS = 48


class Cpu(ctypes.Structure):
    _fields_ = [("r", ctypes.c_uint32 * 16), ("n", ctypes.c_uint32), ("z", ctypes.c_uint32), ("c", ctypes.c_uint32),
                ("v", ctypes.c_uint32), ("fpscr", ctypes.c_uint32), ("fault", ctypes.c_uint32),
                ("budget", ctypes.c_uint32), ("memsize", ctypes.c_uint32), ("depth", ctypes.c_uint32),
                ("tls", ctypes.c_uint32), ("pad", ctypes.c_uint32), ("d", ctypes.c_uint64 * 32),
                ("trace", ctypes.c_uint32 * 64)]


IMPORT_HANDLER = ctypes.CFUNCTYPE(None, ctypes.POINTER(Cpu), ctypes.c_void_p, ctypes.c_uint32)


# ---- inputs ------------------------------------------------------------------------------------
def rand_float_bits(rng) -> int:
    x = rng.choice((rng.uniform(-1000, 1000), rng.uniform(-1, 1), float(rng.randint(-100, 100)), 0.0))
    return struct.unpack("<I", struct.pack("<f", x))[0]


def rand_double_bits(rng) -> int:
    x = rng.choice((rng.uniform(-1000, 1000), rng.uniform(-1, 1), float(rng.randint(-100, 100)), 0.0))
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def scratch_image(rng, data_lo, data_hi) -> bytes:
    words = array("I")
    for _ in range(SCRATCH_SIZE // 4):
        p = rng.random()
        if p < 0.40:
            w = SCRATCH + (rng.randrange(0, 0x40000) & ~3)
        elif p < 0.60:
            w = rng.randrange(0, 65)
        elif p < 0.75:
            w = 0
        elif p < 0.85:
            w = rand_float_bits(rng)
        elif p < 0.90:
            w = data_lo + (rng.randrange(0, data_hi - data_lo) & ~3)
        else:
            w = rng.getrandbits(32)
        words.append(w)
    return words.tobytes()


def reg_value(rng, data_lo, data_hi) -> int:
    p = rng.random()
    if p < 0.45:
        return SCRATCH + (rng.randrange(0, 0x40000) & ~3)
    if p < 0.65:
        return rng.randrange(0, 65)
    if p < 0.75:
        return 0
    if p < 0.90:
        return rng.getrandbits(32)
    return data_lo + (rng.randrange(0, data_hi - data_lo) & ~3)


# ---- comparison --------------------------------------------------------------------------------
def is_nan32(b: bytes) -> bool:
    u = int.from_bytes(b, "little")
    return (u & 0x7F800000) == 0x7F800000 and (u & 0x007FFFFF) != 0


def is_nan64(b: bytes) -> bool:
    if len(b) != 8:
        return False
    u = int.from_bytes(b, "little")
    return (u & 0x7FF0000000000000) == 0x7FF0000000000000 and (u & 0x000FFFFFFFFFFFFF) != 0


def same_vfp(a: int, b: int) -> bool:
    if a == b:
        return True
    if is_nan64(a.to_bytes(8, "little")) and is_nan64(b.to_bytes(8, "little")):
        return True
    for sh in (0, 32):
        x, y = (a >> sh) & 0xFFFFFFFF, (b >> sh) & 0xFFFFFFFF
        if x != y and not (is_nan32(x.to_bytes(4, "little")) and is_nan32(y.to_bytes(4, "little"))):
            return False
    return True


def compare(u, h):
    if h["fault"]:
        return f"host fault {h['fault']}"
    for k in range(16):
        # softfp passes floats in core registers, so the x86 NaN sign shows up here too
        if u["r"][k] != h["r"][k] and not (is_nan32(u["r"][k].to_bytes(4, "little"))
                                           and is_nan32(h["r"][k].to_bytes(4, "little"))):
            return f"r{k}: unicorn {u['r'][k]:#x} host {h['r'][k]:#x}"
    if u["f"] != h["f"]:
        return f"nzcv: unicorn {u['f']:04b} host {h['f']:04b}"
    if u["fp"] != h["fp"]:
        return f"fpscr nzcv: unicorn {u['fp']:04b} host {h['fp']:04b}"
    for k in range(32):
        if not same_vfp(u["d"][k], h["d"][k]):
            return f"d{k}: unicorn {u['d'][k]:#018x} host {h['d'][k]:#018x}"
    for a, data in u["mem"].items():
        other = h["mem"][a]
        if data == other:
            continue
        for j in range(0, len(data), 4):
            if data[j:j + 4] == other[j:j + 4]:
                continue
            if is_nan32(data[j:j + 4]) and is_nan32(other[j:j + 4]):
                continue
            k = j & ~7
            if is_nan64(data[k:k + 8]) and is_nan64(other[k:k + 8]):
                continue
            return f"memory {a + j:#x}: unicorn {data[j:j + 4].hex()} host {other[j:j + 4].hex()}"
    return None


# ---- build -------------------------------------------------------------------------------------
X86_TEST = ("x86_64-windows-gnu", ["-O1", "-DRECOMP_CHECKED"], "recomp.dll")
# zig ships no Android libc headers; musl's are enough because the translated code calls no libc (only builtins),
# and the library links with -nostdlib, so the result has no libc dependency and loads on Android as well
ARM64_ANDROID = ("aarch64-linux-musl", ["-O2", "-fPIC", "-g0"], "libnovahd_recomp.so")


HOSTFN_BASE, HOSTFN_MAX = 0x00E00000, 0x10000  # runtime/rt.h: RT_HOSTFN_BASE, RT_HOSTFN_MAX


def compile_objects(image: Image, ents: dict, sources: dict, workdir: Path, mutate=None, target=X86_TEST,
                    variant="test", *, emit_only=False) -> list:
    """Compile every entry (translated, or a stub that faults) plus the dispatcher. Object files are cached by
    content, so a rebuild recompiles only the shards that changed. The x86 test build is checked (bounds, budget);
    the arm64 build is the optimized one. variant "test" adds the comparison harness's glue (an import handler
    set from Python, rc_run); variant "runtime" leaves rc_import and rc_hostcall to runtime/."""
    triple, opt, out_name = target[:3]
    compiler = list(target[3]) if len(target) > 3 else [sys.executable, "-m", "ziglang", "cc"]
    workdir.mkdir(parents=True, exist_ok=True)
    protos = "".join(f"void f_{a:07x}(cpu_t *c, uint8_t *m);\n" for a in sorted(ents))
    (workdir / "protos.h").write_text(protos, encoding="utf-8")
    head = ('#include "recomp.h"\n#include "protos.h"\n#pragma clang diagnostic ignored "-Wunused-label"\n'
            '#pragma clang diagnostic ignored "-Wunused-variable"\n#pragma clang diagnostic ignored "-Wparentheses-equality"\n'
            '#pragma clang diagnostic ignored "-Wunused-but-set-variable"\n')
    addrs = sorted(ents)
    files = []
    for k in range(SHARDS):
        part = addrs[k::SHARDS]
        body = [head]
        for a in part:
            src = sources.get(a)
            if src is None:
                src = (f"void f_{a:07x}(cpu_t *c, uint8_t *m) {{ (void)m; c->fault = RC_FAULT_UNLIFTED; "
                       f"c->r[15] = {a:#x}u; }}")  # r15: which function was missing
            elif mutate:
                src = src.replace(*mutate)
            body.append(src)
        files.append((workdir / f"shard{k:02d}.c", "\n".join(body) + "\n"))
    disp = [head, "#ifdef _WIN32", "#define RC_EXPORT __declspec(dllexport)", "#else",
            "#define RC_EXPORT __attribute__((visibility(\"default\")))", "#endif"]
    if variant == "test":
        disp += ["static void (*handler)(cpu_t *, uint8_t *, uint32_t);",
                 "RC_EXPORT void rc_set_import_handler(void *h) { handler = (void (*)(cpu_t *, uint8_t *, uint32_t))h; }",
                 "void rc_import(cpu_t *c, uint8_t *m, uint32_t slot) { handler(c, m, slot); c->r[15] = c->r[14]; }",
                 "void rc_hostcall(cpu_t *c, uint8_t *m, uint32_t slot) { (void)m; (void)slot; c->fault = RC_FAULT_DISPATCH; }",
                 "RC_EXPORT void rc_run(uint32_t addr, cpu_t *c, uint8_t *m) { rc_dispatch(c, m, addr); }"]
    disp += ["int rc_is_entry(uint32_t t) {",
             f"  if (t >= {HOSTFN_BASE:#x}u && t < {HOSTFN_BASE + 4 * HOSTFN_MAX:#x}u) return 1;",
             f"  if (t >= {IMPORT_STUB_BASE:#x}u && t < {IMPORT_STUB_BASE + 4 * len(image.imports):#x}u) return 1;",
             "  switch (t & ~1u) {"]
    disp += [f"  case {IMAGE_BASE + a:#x}u:" for a in addrs]
    disp += [f"  case {IMAGE_BASE + a:#x}u:" for a in sorted(image.plt)]
    disp += ["    return 1;", "  }", "  return 0;", "}"]
    disp += ["void rc_dispatch(cpu_t *c, uint8_t *m, uint32_t t) {",
            f"  if (t >= {HOSTFN_BASE:#x}u && t < {HOSTFN_BASE + 4 * HOSTFN_MAX:#x}u) "
            f"{{ rc_hostcall(c, m, (t - {HOSTFN_BASE:#x}u) / 4); return; }}",
            f"  if (t >= {IMPORT_STUB_BASE:#x}u && t < {IMPORT_STUB_BASE + 4 * len(image.imports):#x}u) "
            f"{{ rc_import(c, m, (t - {IMPORT_STUB_BASE:#x}u) / 4); return; }}",
            "  if (rc_kuser(c, m, t)) return;",
            "  switch (t & ~1u) {  /* a Thumb address has its low bit set */"]
    for a in addrs:
        disp.append(f"  case {IMAGE_BASE + a:#x}u: f_{a:07x}(c, m); return;")
    slots = {name: k for k, name in enumerate(image.imports)}
    for a, (name, _) in sorted(image.plt.items()):
        disp.append(f"  case {IMAGE_BASE + a:#x}u: rc_import(c, m, {slots[name]}); return;  /* {name} via the PLT */")
    disp += ["  }", "  c->fault = RC_FAULT_DISPATCH; c->r[15] = t;", "}"]
    files.append((workdir / f"dispatch-{variant}.c", "\n".join(disp) + "\n"))

    flags = [opt[0], "-target", triple, "-fwrapv", "-fno-strict-aliasing", "-ffp-contract=off", *opt[1:],
             f"-I{RUNTIME}", f"-I{workdir}"]
    key_extra = (RUNTIME / "recomp.h").read_text() + protos + " ".join(flags)

    plan = []
    for path, text in files:
        digest = hashlib.sha256((text + key_extra).encode()).hexdigest()[:16]
        obj = path.with_suffix(f".{digest}.o")
        if not obj.exists() or not path.exists():
            path.write_text(text, encoding="utf-8")
        plan.append((path, obj))

    if emit_only:
        return [[str(item) for item in (*compiler, "-c", *flags, src, "-o", obj)] for src, obj in plan]
    return compile_plan([[str(item) for item in (*compiler, "-c", *flags, src, "-o", obj)]
                         for src, obj in plan], os.cpu_count() or 4)


def build(image: Image, ents: dict, sources: dict, workdir: Path, mutate=None, target=X86_TEST) -> Path:
    """The test variant of compile_objects, linked into one shared library."""
    triple, opt, out_name = target
    objs = compile_objects(image, ents, sources, workdir, mutate, target, "test")
    lib = workdir / out_name
    link = ["-nostdlib", "-Wl,--no-undefined", "-s"] if triple.startswith("aarch64") else []
    r = subprocess.run([sys.executable, "-m", "ziglang", "cc", "-shared", "-target", triple, *link,
                        *map(str, objs), "-o", str(lib)], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"link failed:\n{r.stderr[-4000:]}")
    return lib


# ---- the two machines --------------------------------------------------------------------------
class UcMachine(models.Machine):
    def __init__(self, uc, writable):
        self.uc = uc
        self.regs = [getattr(U, f"UC_ARM_REG_R{k}") for k in range(13)] + [U.UC_ARM_REG_SP, U.UC_ARM_REG_LR]
        super().__init__(writable)

    def reg(self, k):
        return self.uc.reg_read(self.regs[k])

    def set_reg(self, k, v):
        self.uc.reg_write(self.regs[k], v)

    def read(self, a, n):
        return bytes(self.uc.mem_read(a, n))

    def write(self, a, data):
        self.uc.mem_write(a, data)


class HostMachine(models.Machine):
    def __init__(self, host, writable):
        self.base = ctypes.addressof(host)
        self.cpu = None
        super().__init__(writable)

    def reg(self, k):
        return self.cpu.r[k]

    def set_reg(self, k, v):
        self.cpu.r[k] = v

    def check(self, a, n):
        if a < 0x00001000 or a + n > HOST_SIZE:
            raise models.Unmodelled(f"host access {a:#x}")

    def read(self, a, n):
        self.check(a, n)
        return ctypes.string_at(self.base + a, n)

    def write(self, a, data):
        self.check(a, len(data))
        ctypes.memmove(self.base + a, data, len(data))


class Harness:
    def __init__(self, image: Image, dll, seed: int):
        self.image = image
        self.dll = dll
        self.import_names = list(image.imports)
        rw_start = IMAGE_BASE + (image.writable[0][0] & ~0xFFF)
        self.writable = [(rw_start, IMAGE_BASE + image.size)]
        self.data_lo, self.data_hi = IMAGE_BASE + image.writable[0][0], IMAGE_BASE + image.writable[-1][1]
        self.scratch = [scratch_image(random.Random(seed * 100 + k), self.data_lo, self.data_hi) for k in range(8)]
        self.regions = self.writable + [(SCRATCH, SCRATCH + SCRATCH_SIZE), (STACK, STACK + STACK_SIZE),
                                        (models.HEAP, models.HEAP + models.HEAP_SIZE)]
        self.base_image = bytes(image.memory)

        uc = self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.mem_map(IMAGE_BASE, rw_start - IMAGE_BASE, UC_PROT_READ | UC_PROT_EXEC)
        uc.mem_map(rw_start, IMAGE_BASE + image.size - rw_start)
        uc.mem_map(SCRATCH, SCRATCH_SIZE)
        uc.mem_map(STACK, STACK_SIZE)
        uc.mem_map(models.HEAP, models.HEAP_SIZE)
        uc.mem_write(IMAGE_BASE, self.base_image)  # mem_write ignores the protection
        # import stubs and kernel helpers: `bx lr` at each address, the work is done in a code hook
        uc.mem_map(IMPORT_STUB_BASE, 0x1000, UC_PROT_READ | UC_PROT_EXEC)
        uc.mem_write(IMPORT_STUB_BASE, BX_LR * 0x400)
        uc.mem_map(KUSER, 0x1000, UC_PROT_READ | UC_PROT_EXEC)
        uc.mem_write(KUSER, BX_LR * 0x400)
        uc.hook_add(UC_HOOK_CODE, self._uc_import, begin=IMPORT_STUB_BASE, end=IMPORT_STUB_BASE + 0xFFF)
        uc.hook_add(UC_HOOK_CODE, self._uc_kuser, begin=KUSER, end=KUSER + 0xFFF)
        try:
            uc.reg_write(U.UC_ARM_REG_C1_C0_2, uc.reg_read(U.UC_ARM_REG_C1_C0_2) | (0xF << 20))
        except (UcError, AttributeError):
            pass
        uc.reg_write(U.UC_ARM_REG_FPEXC, 0x40000000)
        self.ucm = UcMachine(uc, self.regions)
        # CPSR as Unicorn starts (ARM state, little-endian); restored in full before every run, so a run that
        # executed garbage (SETEND BE, a mode change) cannot leak into the next one
        self.cpsr0 = uc.reg_read(U.UC_ARM_REG_CPSR) & 0x0FFFFFFF & ~0x220

        self.host = ctypes.create_string_buffer(HOST_SIZE)
        ctypes.memmove(ctypes.addressof(self.host) + IMAGE_BASE, self.base_image, len(self.base_image))
        # the same stub words as in Unicorn: code may read through a pointer to an import
        ctypes.memmove(ctypes.addressof(self.host) + IMPORT_STUB_BASE, BX_LR * 0x400, 0x1000)
        self.hm = HostMachine(self.host, self.regions)
        self._handler = IMPORT_HANDLER(self._host_import)  # keep a reference: the DLL holds the pointer
        dll.rc_set_import_handler(self._handler)
        self.core = [getattr(U, f"UC_ARM_REG_R{k}") for k in range(13)] + [U.UC_ARM_REG_SP, U.UC_ARM_REG_LR]
        self.dregs = [getattr(U, f"UC_ARM_REG_D{k}") for k in range(32)]

    # hooks
    def _uc_import(self, uc, address, size, _):
        name = self.import_names[(address - IMPORT_STUB_BASE) // 4]
        try:
            models.run_import(self.ucm, name)
        except (models.Unmodelled, UcError) as e:
            self.abort = f"import {name}" if isinstance(e, models.Unmodelled) and str(e) == name else f"import {name}: {e}"
            uc.emu_stop()

    def _uc_kuser(self, uc, address, size, _):
        m = self.ucm
        if address == 0xFFFF0FC0:
            ok = m.u32(m.reg(2)) == m.reg(0)
            if ok:
                try:
                    m.store(m.reg(2), m.reg(1).to_bytes(4, "little"))
                except models.Unmodelled as e:
                    self.abort = f"kernel helper: {e}"
                    uc.emu_stop()
                    return
            m.set_reg(0, 0 if ok else 1)
            cpsr = uc.reg_read(U.UC_ARM_REG_CPSR) & ~(0x6 << 28)
            uc.reg_write(U.UC_ARM_REG_CPSR, cpsr | ((0x6 << 28) if ok else 0))
        elif address == 0xFFFF0FE0:
            m.set_reg(0, models.TLS)
        elif address != 0xFFFF0FA0:
            self.abort = f"kernel helper {address:#x}"
            uc.emu_stop()

    def _host_import(self, cpu, m, slot):
        self.hm.cpu = cpu.contents
        try:
            models.run_import(self.hm, self.import_names[slot])
        except Exception as e:  # noqa: BLE001 - an exception must not cross the C frames
            self.host_error = f"import {self.import_names[slot]}: {e!r}"[:120]
            cpu.contents.fault = 8

    # runs
    def snapshot(self, variant: int):
        mem = {a: self.base_image[a - IMAGE_BASE:b - IMAGE_BASE] for a, b in self.writable}
        mem[SCRATCH] = self.scratch[variant]
        mem[STACK] = self.scratch[(variant + 3) % 8]
        mem[models.HEAP] = bytes(models.HEAP_SIZE)
        return mem

    def state(self, rng):
        regs = [reg_value(rng, self.data_lo, self.data_hi) for _ in range(13)] + [SP, SENTINEL]
        d = []
        for _ in range(32):
            p = rng.random()
            if p < 0.45:
                d.append(rand_double_bits(rng))
            elif p < 0.9:
                d.append(rand_float_bits(rng) | (rand_float_bits(rng) << 32))
            else:
                d.append(rng.getrandbits(64))
        return regs, rng.getrandbits(4), rng.getrandbits(4) << 28, d

    def run_unicorn(self, addr, mem, regs, flags, fpscr, d, thumb=False, until=SENTINEL):
        uc = self.uc
        for a, data in mem.items():
            uc.mem_write(a, data)
        for k, r in enumerate(self.core):
            uc.reg_write(r, regs[k])
        uc.reg_write(U.UC_ARM_REG_CPSR, self.cpsr0 | (flags << 28))
        uc.reg_write(U.UC_ARM_REG_FPSCR, fpscr)
        for k, r in enumerate(self.dregs):
            uc.reg_write(r, d[k])
        self.ucm.reset()
        self.abort = None
        try:
            uc.emu_start((IMAGE_BASE + addr) | thumb, until & ~1, count=2_000_000)
        except UcError as e:
            return None, f"unicorn: {e}"
        if self.abort:
            return None, self.abort
        pc = uc.reg_read(U.UC_ARM_REG_PC)
        if pc != until & ~1:
            return None, "unicorn stopped"
        pc = until  # the Thumb bit of a return address is not part of PC
        return {
            "r": [uc.reg_read(r) for r in self.core] + [pc],
            "f": uc.reg_read(U.UC_ARM_REG_CPSR) >> 28,
            "fp": uc.reg_read(U.UC_ARM_REG_FPSCR) >> 28,
            "d": [uc.reg_read(r) & 0xFFFFFFFFFFFFFFFF for r in self.dregs],
            "mem": {a: bytes(uc.mem_read(a, b - a)) for a, b in self.regions},
        }, None

    def run_host(self, addr, mem, regs, flags, fpscr, d):
        base = ctypes.addressof(self.host)
        # The translation runs first, so a run the original later rejects (a write to code) must not leave its
        # writes behind: restore the read-only image and the import stub words, not only the snapshot regions.
        ctypes.memmove(base + IMAGE_BASE, self.base_image, len(self.base_image))
        ctypes.memmove(base + IMPORT_STUB_BASE, BX_LR * 0x400, 0x1000)
        for a, data in mem.items():
            ctypes.memmove(base + a, data, len(data))
        c = Cpu()
        for k in range(15):
            c.r[k] = regs[k]
        c.n, c.z, c.c, c.v = (flags >> 3) & 1, (flags >> 2) & 1, (flags >> 1) & 1, flags & 1
        c.fpscr, c.budget, c.memsize, c.tls = fpscr, 2_000_000, HOST_SIZE, models.TLS
        for k in range(32):
            c.d[k] = d[k]
        self.hm.reset()
        self.dll.rc_run(ctypes.c_uint32(IMAGE_BASE + addr), ctypes.byref(c), self.host)
        return {
            "r": list(c.r), "f": (c.n << 3) | (c.z << 2) | (c.c << 1) | c.v, "fp": c.fpscr >> 28, "d": list(c.d),
            "mem": {a: ctypes.string_at(base + a, b - a) for a, b in self.regions}, "fault": c.fault,
            "depth": c.depth, "trace": list(c.trace),
        }

    def test(self, addr, runs, thumb=False):
        completed, notes = 0, {}
        for k in range(runs):
            rng = random.Random(addr * 1000 + k)
            mem = self.snapshot(k % 8)
            regs, flags, fpscr, d = self.state(rng)
            # The translation runs first. A function entered with random state may return to an address other
            # than the sentinel (a mid-function entry pops a code address); the translation stops there, so the
            # original is stopped at the same address. A wrong translation still shows: the original then never
            # reaches that address, or arrives with other registers.
            self.host_error = None
            h = self.run_host(addr, mem, regs, flags, fpscr, d)
            if h["fault"] == 8 and self.host_error and "Unmodelled" in self.host_error:
                # the import has no model, or the model refused an access: the original would stop there too
                key = self.host_error.split(":")[0]
                notes[key] = notes.get(key, 0) + 1
                continue
            until = h["r"][15] if not h["fault"] and h["r"][15] != SENTINEL else SENTINEL
            u, why = self.run_unicorn(addr, mem, regs, flags, fpscr, d, thumb, until)
            if u is None:
                key = why.split(" at ")[0].split(" (")[0][:60]
                notes[key] = notes.get(key, 0) + 1
                continue
            if h["fault"] == 4:
                # a computed jump to an address that is no entry: random state made a "virtual call" read a code
                # word (a jump table inside a function). The translation supports computed jumps to entries only.
                notes["dispatch to a non-entry, original continued"] = notes.get("dispatch to a non-entry, original continued", 0) + 1
                continue
            if until != SENTINEL:
                notes["returned elsewhere"] = notes.get("returned elsewhere", 0) + 1
            if h["fault"] == 7:
                return "untranslated-callee", completed, f"run {k}: reached a function the lifter could not translate"
            diff = compare(u, h)
            if diff:
                if h["fault"] == 8:
                    diff += f" ({self.host_error})"
                return "fail", completed, f"run {k}: {diff}"
            completed += 1
        if completed < 4:
            return "no-coverage", completed, json.dumps(notes)
        return "pass", completed, ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", "--so", dest="so", type=Path, required=True, help="the iOS executable (thin armv6/armv7)")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--work", type=Path, default=Path("build/recomp"))
    ap.add_argument("--sample", type=int, default=1000)
    ap.add_argument("--runs", type=int, default=16)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--kind", choices=("all", "leaf", "calls"), default="all", help="which functions to sample")
    ap.add_argument("--select", choices=("all", "thumb", "switch"), default="all",
                    help="restrict the pool: Thumb entries only, or entries with an inline __switch32 site; "
                         "--sample 0 tests every entry of the pool")
    ap.add_argument("--only", type=lambda s: int(s, 16), nargs="*", help="test these link addresses only")
    ap.add_argument("--picked-only", action="store_true", help="with --kind leaf: build a DLL of the tested entries only")
    ap.add_argument("--mutate", help="OLD=>NEW: break every translation on purpose; the run should report failures")
    args = ap.parse_args()

    t0 = time.time()
    image = Image(args.so)
    ents = entries(image)
    lifter = Lifter(image, ents)
    sources, reasons = {}, {}
    for a, f in ents.items():
        try:
            sources[a] = lifter.lift(f)
        except Unsupported as e:
            key = str(e).split(" at ")[0].split(" to 0x")[0]
            reasons[key] = reasons.get(key, 0) + 1
    print(f"translated {len(sources)} of {len(ents)} entries in {time.time() - t0:.0f}s")
    calls = {a for a, f in ents.items() if any(True for _ in branch_targets(image, f))
             and any(is_bl or not (f.address <= t < f.address + f.size) for _, t, is_bl in branch_targets(image, f))}
    calls |= {a for a, src in sources.items() if "rc_dispatch" in src or "rc_import" in src}

    pool = sorted(sources)
    if args.kind == "leaf":
        pool = [a for a in pool if a not in calls]
    elif args.kind == "calls":
        pool = [a for a in pool if a in calls]
    if args.select == "thumb":
        pool = [a for a in pool if getattr(ents[a], "thumb", False)]
    elif args.select == "switch":
        pool = [a for a in pool if "uint32_t res = addc(r0, ~" in sources[a] and "switch (r0) {" in sources[a]]
    if args.sample <= 0:
        args.sample = len(pool)
    print(f"pool {len(pool)} ({args.select}/{args.kind})")
    picks = sorted(args.only) if args.only else sorted(random.Random(args.seed).sample(pool, min(args.sample, len(pool))))
    mutate = tuple(args.mutate.split("=>", 1)) if args.mutate else None
    b_ents, b_sources = ents, sources
    if args.picked_only:  # leaf functions only: the DLL holds just the tested entries (a mutation run compiles little)
        assert args.kind == "leaf", "--picked-only needs --kind leaf"
        b_sources = {a: sources[a] for a in picks}
        b_ents = dict(b_sources)  # entries the picks mention (a fall-through into the next block) become fault stubs
        for a in picks:
            for x in re.findall(r"f_([0-9a-f]{7})\(c, m\)", sources[a]):
                b_ents.setdefault(int(x, 16), None)
        b_ents = {a: ents[a] for a in b_ents}
    dll = ctypes.CDLL(str(build(image, b_ents, b_sources, args.work, mutate)))
    print(f"built in {time.time() - t0:.0f}s")
    harness = Harness(image, dll, args.seed)
    args.out.mkdir(parents=True, exist_ok=True)
    rows, counts = [], {}
    for a in picks:
        f = ents[a]
        status, completed, detail = harness.test(a, args.runs, bool(getattr(f, "thumb", False)))
        kind = "calls" if a in calls else "leaf"
        counts.setdefault(kind, {})[status] = counts.setdefault(kind, {}).get(status, 0) + 1
        rows.append({"address": f"0x{a:x}", "size": f.size, "kind": kind, "mode": "thumb" if getattr(f, "thumb", False) else "arm", "status": status, "runs": completed,
                     "detail": detail, "name": f.name})
        if status == "fail":
            print(f"fail 0x{a:x} {f.name[:60]}: {detail}")
    with (args.out / "results.csv").open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]), lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    nocov = {}
    for r in rows:
        if r["status"] == "no-coverage":
            for k, n in json.loads(r["detail"]).items():
                nocov[k] = nocov.get(k, 0) + n
    summary = {"executable": args.so.name, "entries": len(ents), "translated": len(sources),
               "untranslated": dict(sorted(reasons.items(), key=lambda kv: -kv[1])), "tested": len(picks),
               "kind": args.kind, "select": args.select, "runs_per_function": args.runs, "seed": args.seed, "results": counts,
               "incomplete_runs_by_reason": dict(sorted(nocov.items(), key=lambda kv: -kv[1])[:25]),
               "seconds": round(time.time() - t0)}
    (args.out / "summary.json").write_text(json.dumps(summary, indent=1) + "\n", encoding="utf-8")
    print(json.dumps(counts))


if __name__ == "__main__":
    main()
