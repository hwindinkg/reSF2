#!/usr/bin/env python3
"""Fixed Unicorn emulation: patch GOT entries instead of PLT stubs.
GOT entries work regardless of ARM/Thumb calling mode."""

import struct, zlib
from unicorn import *
from unicorn.arm_const import *

REPO = "E:/reSF2"
SO_PATH = REPO + "/reverse/binaries/libs3e_android.so"
DZ_PATH = REPO + "/assets/files.dz"
with open(SO_PATH, "rb") as f: SO = f.read()

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)

def w32(addr, val):
    mu.mem_write(addr, struct.pack("<I", val))
def r32(addr):
    return struct.unpack("<I", mu.mem_read(addr, 4))[0]

# ── Load ELF segments ──
for i in range(6):
    p = struct.unpack_from("<IIIIIIII", SO, 0x34 + i * 32)
    if p[0] == 1:
        va, off, fs, ms = p[2], p[1], p[4], p[5]
        ps = va & ~0xFFF
        pe = ((va + ms - 1) & ~0xFFF) + 0x1000
        try: mu.mem_map(ps, pe - ps, UC_PROT_ALL)
        except: pass
        mu.mem_write(va, SO[off:off + fs])
        if ms > fs:
            mu.mem_write(va + fs, b"\x00" * (ms - fs))

# ── Extra memory ──
STACK = 0x90000000; mu.mem_map(STACK, 0x100000, UC_PROT_ALL)
HEAP  = 0x80000000; mu.mem_map(HEAP,  0x08000000, UC_PROT_ALL)
DATA  = 0x70000000; mu.mem_map(DATA,  0x01000000, UC_PROT_ALL)

SCRATCH = 0x7F000000
mu.mem_map(SCRATCH, 0x01000000, UC_PROT_ALL)
SCRATCH_OFF = [0x7F001000]

def emit(*words):
    off = SCRATCH_OFF[0]
    buf = b"".join(struct.pack("<I", w) if isinstance(w, int) else w for w in words)
    mu.mem_write(off, buf)
    SCRATCH_OFF[0] = off + len(buf)
    return off

ret0 = emit(0xe3a00000, 0xe12fff1e)  # MOV R0,#0; BX LR
TRAP = 0x7F100000
w32(TRAP, 0xe7f000f0)  # UDF

# ── Heap ──
heap_ptr = [0x80010000]
def heap_alloc(size):
    if size == 0: size = 1
    size = (size + 3) & ~3
    a = heap_ptr[0]
    heap_ptr[0] = a + size
    return a

# ── z_stream tracking ──
z_streams = {}
Z_NEXT_IN = 0; Z_AVAIL_IN = 4; Z_TOTAL_IN = 8
Z_NEXT_OUT = 12; Z_AVAIL_OUT = 16; Z_TOTAL_OUT = 20

def make_trampoline(func_id):
    return emit(
        0xe92d400f,            # STMFD SP!, {R0-R3, LR}
        0xe3a00000 | func_id,  # MOV R0, #func_id
        0xe51ff004,            # LDR PC, [PC, #-4]
        TRAP,
    )

# ── Hook dispatcher ──
def do_hook(mu, address, size, user_data):
    if address != TRAP: return
    sp = mu.reg_read(UC_ARM_REG_SP)
    r0_orig = r32(sp)
    r1_orig = r32(sp + 4)
    r2_orig = r32(sp + 8)
    r3_orig = r32(sp + 12)
    lr_orig = r32(sp + 16)
    func_id = mu.reg_read(UC_ARM_REG_R0)
    cpsr = mu.reg_read(UC_ARM_REG_CPSR)
    thumb = (cpsr >> 5) & 1

    def ret(val):
        mu.reg_write(UC_ARM_REG_R0, val)
        mu.reg_write(UC_ARM_REG_PC, lr_orig & ~1)
        if (lr_orig & 1) and not thumb:
            # Switch to Thumb mode if return addr has LSB set
            new_cpsr = cpsr | (1 << 5)
            mu.reg_write(UC_ARM_REG_CPSR, new_cpsr)

    if func_id == 0:  # malloc
        p = heap_alloc(r0_orig)
        mu.mem_write(p, b"\x00" * r0_orig)
        ret(p)
    elif func_id == 1:  # free
        ret(0)
    elif func_id == 2:  # realloc
        p = heap_alloc(r1_orig)
        mu.mem_write(p, b"\x00" * r1_orig)
        ret(p)
    elif func_id == 3:  # memcpy
        mu.mem_write(r0_orig, bytes(mu.mem_read(r1_orig, r2_orig)))
        ret(r0_orig)
    elif func_id == 4:  # memset
        ptr = r0_orig
        val = r1_orig & 0xFF
        n = r2_orig
        mu.mem_write(ptr, bytes([val]) * n)
        ret(ptr)
    elif func_id == 5:  # calloc
        total = r0_orig * r1_orig
        p = heap_alloc(total)
        mu.mem_write(p, b"\x00" * total)
        ret(p)
    elif func_id == 10:  # inflateInit2_
        stream_addr = r0_orig
        windowBits = r1_orig
        print(f"    [+] inflateInit2_(stream=0x{stream_addr:08x}, windowBits={windowBits})")
        try:
            dec = zlib.decompressobj(-windowBits)
            z_streams[stream_addr] = dec
            w32(stream_addr + Z_NEXT_IN, 0)
            w32(stream_addr + Z_AVAIL_IN, 0)
            w32(stream_addr + Z_TOTAL_IN, 0)
            w32(stream_addr + Z_NEXT_OUT, 0)
            w32(stream_addr + Z_AVAIL_OUT, 0)
            w32(stream_addr + Z_TOTAL_OUT, 0)
            ret(0)
        except:
            ret(-4)
    elif func_id == 11:  # inflate
        stream_addr = r0_orig
        if stream_addr not in z_streams:
            ret(-2); return
        dec = z_streams[stream_addr]
        avail_in = r32(stream_addr + Z_AVAIL_IN)
        next_in = r32(stream_addr + Z_NEXT_IN)
        avail_out = r32(stream_addr + Z_AVAIL_OUT)
        next_out = r32(stream_addr + Z_NEXT_OUT)
        if avail_in > 0 and next_in > 0:
            in_data = bytes(mu.mem_read(next_in, avail_in))
            try:
                out_data = dec.decompress(in_data, avail_out)
                if len(out_data) > 0:
                    mu.mem_write(next_out, out_data)
                w32(stream_addr + Z_AVAIL_IN, avail_in - len(in_data))
                w32(stream_addr + Z_NEXT_IN, next_in + len(in_data))
                w32(stream_addr + Z_AVAIL_OUT, avail_out - len(out_data))
                w32(stream_addr + Z_NEXT_OUT, next_out + len(out_data))
                w32(stream_addr + Z_TOTAL_IN, r32(stream_addr + Z_TOTAL_IN) + len(in_data))
                w32(stream_addr + Z_TOTAL_OUT, r32(stream_addr + Z_TOTAL_OUT) + len(out_data))
                print(f"    inflate: {len(in_data)} -> {len(out_data)} bytes")
                ret(1 if len(out_data) < avail_out else 0)
            except:
                ret(-3)
        else:
            ret(0)
    elif func_id == 12:  # inflateEnd
        stream_addr = r0_orig
        if stream_addr in z_streams: del z_streams[stream_addr]
        ret(0)
    else:
        print(f"    UNKNOWN func_id={func_id}")
        ret(0)

mu.hook_add(UC_HOOK_CODE, do_hook)

# ── Build GOT-to-function mapping ──
# .rel.plt at 0xbe50, 148 entries of 8 bytes each
# .dynsym at 0x1470
# .dynstr at 0x41b0
# .got at 0xc1ecc
dynstr = SO[0x41b0:0x41b0+0x3b28]
got_relocs = {}  # GOT_addr -> function_name
for i in range(148):
    r_off, r_info = struct.unpack_from("<II", SO, 0xbe50 + i * 8)
    sym_idx = r_info >> 8
    st_name = struct.unpack_from("<I", SO, 0x1470 + sym_idx * 16)[0]
    end = st_name
    while dynstr[end] != 0: end += 1
    name = dynstr[st_name:end].decode("latin-1", errors="replace")
    got_relocs[r_off] = name

# Functions we need to hook
hook_map = {
    "malloc": 0, "free": 1, "realloc": 2, "memcpy": 3, "memset": 4, "calloc": 5,
    "inflateInit2_": 10, "inflate": 11, "inflateEnd": 12,
}

# Patch GOT entries
print("=== Patching GOT entries ===")
for got_addr, name in got_relocs.items():
    if name in hook_map:
        tramp = make_trampoline(hook_map[name])
        w32(got_addr, tramp)
        print(f"  {name} GOT@0x{got_addr:05x} -> trampoline@0x{tramp:05x}")
    else:
        # Unhooked: redirect to ret0 (return 0)
        w32(got_addr, ret0)

# ── Parse DZ correctly ──
with open(DZ_PATH, "rb") as f: dz = f.read()
assert dz[:4] == b'DTRZ'
nfiles = struct.unpack_from('<H', dz, 4)[0]
ndirs = struct.unpack_from('<H', dz, 6)[0]

pos = 9
names = []
for _ in range(nfiles):
    end = dz.index(b'\x00', pos)
    names.append(dz[pos:end].decode('utf-8', errors='replace'))
    pos = end + 1

for _ in range(ndirs):
    if pos >= len(dz): break
    end = dz.index(b'\x00', pos) if b'\x00' in dz[pos:] else len(dz)
    pos = end + 1

pos += nfiles * 6 + 3
idx = names.index('forge.xml')
f0 = struct.unpack_from('<I', dz, pos + idx*16)[0]
f1 = struct.unpack_from('<I', dz, pos + idx*16 + 4)[0]
f2 = struct.unpack_from('<I', dz, pos + idx*16 + 8)[0]
f3 = struct.unpack_from('<I', dz, pos + idx*16 + 12)[0]
print(f"\nforge.xml: off={f0} comp={f1} uncomp={f2} type={f3}")

comp_data = dz[f0:f0 + f1]
print(f"Compressed at abs offset {f0}, {len(comp_data)} bytes")
print(f"First 16: {comp_data[:16].hex()}")

# ── Set up emulation ──
IN_BUF = 0x70000000
OUT_PTR_ADDR = 0x70010000
OUT_SZ_ADDR  = 0x70010008

mu.mem_write(IN_BUF, comp_data)
w32(OUT_PTR_ADDR, 0)
w32(OUT_SZ_ADDR, 0x100000)

# The s3eCompressionDecomp sets these globals itself:
# DAT_000d8514@VA 0xd8514 (- 0x10000 = 0xc8514)
# But we pre-set them in case BSS is zero
GLBL_IN_PTR = 0xc8514
GLBL_IN_SZ = 0xc8518
w32(GLBL_IN_PTR, IN_BUF)
w32(GLBL_IN_SZ, len(comp_data))

FUNC_ADDR = 0x51c1c  # s3eCompressionDecomp
RET_ADDR  = 0x9FFFF00

SP = STACK + 0x80000
mu.reg_write(UC_ARM_REG_SP, SP)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, len(comp_data))
mu.reg_write(UC_ARM_REG_R1, IN_BUF)
mu.reg_write(UC_ARM_REG_R2, OUT_PTR_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUT_SZ_ADDR)
w32(SP, 4)  # type hint = 4

print(f"\n=== Emulating s3eCompressionDecomp @ 0x{FUNC_ADDR:05x} ===")
print(f"  R0={len(comp_data)}  R1=0x{IN_BUF:08x}  [SP]=type=4")

try:
    mu.emu_start(FUNC_ADDR, RET_ADDR, timeout=60_000_000)
    print(f"\n=== Returned! ===")
    ret_code = mu.reg_read(UC_ARM_REG_R0)
    out_ptr = r32(OUT_PTR_ADDR)
    out_sz = r32(OUT_SZ_ADDR)
    print(f"Return code: {ret_code}")
    print(f"Output: ptr=0x{out_ptr:08x} sz={out_sz}")
    if out_ptr and out_sz and out_sz < 0x2000000:
        result = bytes(mu.mem_read(out_ptr, out_sz))
        out_path = REPO + "/forge_decoded.xml"
        with open(out_path, "wb") as f:
            f.write(result)
        print(f"Saved {len(result)} bytes to {out_path}")
        print(f"First 32: {result[:32].hex()}")
        with open(REPO + "/assets/forge.xml", "rb") as f:
            gt = f.read()
        if result == gt:
            print(f"\n*** PERFECT MATCH with ground truth! ***")
        else:
            for i in range(min(len(result), len(gt))):
                if result[i] != gt[i]:
                    print(f"\nMismatch at byte {i}: got 0x{result[i]:02x} expected 0x{gt[i]:02x}")
                    break
    else:
        print("No valid output")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    lr = mu.reg_read(UC_ARM_REG_LR)
    sp = mu.reg_read(UC_ARM_REG_SP)
    cpsr = mu.reg_read(UC_ARM_REG_CPSR)
    thumb = (cpsr >> 5) & 1
    r0 = mu.reg_read(UC_ARM_REG_R0)
    print(f"\n!!! CRASH PC=0x{pc:05x} LR=0x{lr:05x} SP=0x{sp:08x} Thumb={thumb}")
    print(f"  R0=0x{r0:08x}  Error: {e}")
    try:
        instr = struct.unpack("<I", mu.mem_read(pc, 4))[0]
        print(f"  Instr at PC: 0x{instr:08x}")
    except: pass

print(f"\n--- zlib streams still open: {len(z_streams)} ---")
