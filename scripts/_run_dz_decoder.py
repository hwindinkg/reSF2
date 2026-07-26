#!/usr/bin/env python3
"""Decompress forge.xml via s3eCompressionDecomp in Unicorn (ARM).
Real Python zlib integration for inflate/inflateInit2_/inflateEnd calls."""
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
def w8(addr, val):
    mu.mem_write(addr, bytes([val]))
def r8(addr):
    return mu.mem_read(addr, 1)[0]

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
CODE  = 0x7F000000; mu.mem_map(CODE,  0x01000000, UC_PROT_ALL)

SCRATCH_OFF = [0x1000]
def emit(*words):
    off = SCRATCH_OFF[0]
    buf = b"".join(struct.pack("<I", w) if isinstance(w, int) else w for w in words)
    mu.mem_write(off, buf)
    SCRATCH_OFF[0] = off + len(buf)
    return off

# ── Stubs ──
ret0 = emit(0xe3a00000, 0xe12fff1e)  # MOV R0,#0; BX LR
TRAP = 0x7F008000
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
# Python zlib decompressor per stream
z_streams = {}  # stream_addr -> decompressobj

Z_STREAM_SIZE = 0x38  # 56 bytes (ARM Android z_stream)
# Offset of key fields from stream base:
Z_NEXT_IN = 0
Z_AVAIL_IN = 4
Z_TOTAL_IN = 8
Z_NEXT_OUT = 12
Z_AVAIL_OUT = 16
Z_TOTAL_OUT = 20
Z_MSG = 24
Z_STATE = 28
Z_ZALLOC = 32
Z_ZFREE = 36
Z_OPAQUE = 40
Z_DATA_TYPE = 44
Z_ADLER = 46
Z_RESERVED = 48  # 8 bytes to make 56

def make_trampoline(func_id):
    return emit(
        0xe92d400f,            # STMFD SP!, {R0-R3, LR}
        0xe3a00000 | func_id,  # MOV R0, #func_id
        0xe51ff004,           # LDR PC, [PC, #-4]
        TRAP,
    )

# ── Hook dispatcher ──
def do_hook(mu, address, size, user_data):
    if address != TRAP:
        return
    sp = mu.reg_read(UC_ARM_REG_SP)
    r0_orig = r32(sp)
    r1_orig = r32(sp + 4)
    r2_orig = r32(sp + 8)
    r3_orig = r32(sp + 12)
    lr_orig = r32(sp + 16)
    func_id = mu.reg_read(UC_ARM_REG_R0)

    def ret(val):
        mu.reg_write(UC_ARM_REG_R0, val)
        mu.reg_write(UC_ARM_REG_PC, lr_orig)

    if func_id == 0:  # malloc(size) -> ptr
        sz = r0_orig
        p = heap_alloc(sz)
        mu.mem_write(p, b"\x00" * sz)
        ret(p)

    elif func_id == 1:  # free(ptr)
        ret(0)

    elif func_id == 2:  # realloc(ptr, new_size) -> ptr
        sz = r1_orig
        p = heap_alloc(sz)
        mu.mem_write(p, b"\x00" * sz)
        ret(p)

    elif func_id == 3:  # memcpy(dst, src, n) -> dst
        data = bytes(mu.mem_read(r1_orig, r2_orig))
        mu.mem_write(r0_orig, data)
        ret(r0_orig)

    elif func_id == 4:  # memset(ptr, val, n) -> ptr
        mu.mem_write(r0_orig, bytes([r1_orig & 0xFF]) * r2_orig)
        ret(r0_orig)

    elif func_id == 5:  # calloc(nmemb, size) -> ptr
        total = r0_orig * r1_orig
        p = heap_alloc(total)
        mu.mem_write(p, b"\x00" * total)
        ret(p)

    # ── zlib functions ──
    elif func_id == 10:  # inflateInit2_(stream, windowBits)
        stream_addr = r0_orig
        windowBits = r1_orig
        print("    [+] inflateInit2_(stream=0x%08x, windowBits=%d)" % (stream_addr, windowBits))
        # Create Python decompressor
        try:
            dec = zlib.decompressobj(-windowBits)  # negative = raw deflate
            z_streams[stream_addr] = dec
            # Initialize stream fields
            w32(stream_addr + Z_NEXT_IN, 0)
            w32(stream_addr + Z_AVAIL_IN, 0)
            w32(stream_addr + Z_TOTAL_IN, 0)
            w32(stream_addr + Z_NEXT_OUT, 0)
            w32(stream_addr + Z_AVAIL_OUT, 0)
            w32(stream_addr + Z_TOTAL_OUT, 0)
            w32(stream_addr + Z_ZALLOC, 0)
            w32(stream_addr + Z_ZFREE, 0)
            w32(stream_addr + Z_OPAQUE, 0)
            ret(0)  # Z_OK
        except Exception as e:
            print("    inflateInit2_ FAIL: %s" % e)
            ret(-4)  # Z_MEM_ERROR

    elif func_id == 11:  # inflate(stream, flush) -> status
        stream_addr = r0_orig
        flush = r1_orig
        if stream_addr not in z_streams:
            print("    inflate(0x%08x) UNKNOWN STREAM!" % stream_addr)
            ret(-2)  # Z_STREAM_ERROR
            return
        dec = z_streams[stream_addr]
        avail_in = r32(stream_addr + Z_AVAIL_IN)
        next_in = r32(stream_addr + Z_NEXT_IN)
        avail_out = r32(stream_addr + Z_AVAIL_OUT)
        next_out = r32(stream_addr + Z_NEXT_OUT)
        total_in = r32(stream_addr + Z_TOTAL_IN)
        total_out = r32(stream_addr + Z_TOTAL_OUT)
        if avail_in > 0 and next_in > 0:
            in_data = bytes(mu.mem_read(next_in, avail_in))
            try:
                out_data = dec.decompress(in_data, avail_out)
                # Write output
                if len(out_data) > 0:
                    mu.mem_write(next_out, out_data)
                # Update stream  
                w32(stream_addr + Z_AVAIL_IN, avail_in - len(in_data))
                w32(stream_addr + Z_NEXT_IN, next_in + len(in_data))
                w32(stream_addr + Z_AVAIL_OUT, avail_out - len(out_data))
                w32(stream_addr + Z_NEXT_OUT, next_out + len(out_data))
                w32(stream_addr + Z_TOTAL_IN, total_in + len(in_data))
                w32(stream_addr + Z_TOTAL_OUT, total_out + len(out_data))
                print("    inflate: %d -> %d bytes (avail_in=%d->%d, avail_out=%d->%d)" % (
                    len(in_data), len(out_data),
                    avail_in, avail_in - len(in_data),
                    avail_out, avail_out - len(out_data)))
                ret(1 if len(out_data) < avail_out else 0)  # Z_STREAM_END or Z_OK
            except Exception as e:
                print("    inflate ERROR: %s" % e)
                ret(-3)  # Z_DATA_ERROR
        else:
            print("    inflate: no data (avail_in=%d, next_in=0x%08x)" % (avail_in, next_in))
            if avail_in == 0 and total_out > 0:
                if dec.eof:
                    ret(1)  # Z_STREAM_END
                else:
                    ret(0)  # Z_OK
            else:
                ret(-2)

    elif func_id == 12:  # inflateEnd(stream)
        stream_addr = r0_orig
        if stream_addr in z_streams:
            del z_streams[stream_addr]
        ret(0)

    else:
        print("    UNKNOWN func_id=%d" % func_id)
        ret(0)

mu.hook_add(UC_HOOK_CODE, do_hook)

# ── Patch PLT ──
dynstr = SO[0x41b0:0x41b0+0x3b28]
plt_by_addr = {}
plt_by_name = {}

for i in range(148):
    r_off, r_info = struct.unpack_from("<II", SO, 0xbe50 + i * 8)
    sym_idx = r_info >> 8
    st_name = struct.unpack_from("<I", SO, 0x1470 + sym_idx * 16)[0]
    end = st_name
    while dynstr[end] != 0: end += 1
    name = dynstr[st_name:end].decode("latin-1", errors="replace")
    plt_addr = 0xc2f0 + 16 + i * 12
    plt_by_addr[plt_addr] = name
    plt_by_name[name] = plt_addr

# Map function names to IDs
hook_map = {
    "malloc": 0, "free": 1, "realloc": 2, "memcpy": 3, "memset": 4, "calloc": 5,
    "inflateInit2_": 10, "inflate": 11, "inflateEnd": 12,
}

# Phase 1: all -> ret0
for addr in plt_by_addr:
    w32(addr, 0xe51ff004)
    w32(addr + 4, ret0)

# Phase 2: hook-managed
for name, fid in hook_map.items():
    if name not in plt_by_name:
        print("  NOT IN PLT: %s" % name)
        continue
    tramp = make_trampoline(fid)
    addr = plt_by_name[name]
    w32(addr, 0xe51ff004)
    w32(addr + 4, tramp)
    print("  %s -> fid=%d @ PLT 0x%05x" % (name, fid, addr))

# ── Parse DZ ──
with open(DZ_PATH, "rb") as f: dz = f.read()
nfiles = struct.unpack_from("<H", dz, 4)[0]
ndirs = struct.unpack_from("<H", dz, 6)[0]
pos = 9
names = []
for _ in range(nfiles):
    end = dz.index(b"\x00", pos)
    names.append(dz[pos:end].decode("utf-8", errors="replace"))
    pos = end + 1
for _ in range(ndirs):
    pos = dz.index(b"\x00", pos) + 1
pos += nfiles * 6 + 4
for i in range(nfiles):
    f0, f1, f2, f3 = struct.unpack_from("<IIII", dz, pos)
    if names[i] == "forge.xml":
        forge = {
            "offset": f1 & 0xFFFFFF,
            "comp_size": f2 & 0xFFFFFF,
            "uncomp_size": f0 & 0xFFFFFF,
            "type": (f2 >> 24) & 0xFF,
        }
        break
    pos += 16

assert forge, "forge.xml not found!"
print("\nforge.xml: comp=%d unc=%d type=%d" % (forge["comp_size"], forge["uncomp_size"], forge["type"]))

# ── Set up ──
IN_BUF = 0x70000000
OUT_PTR_ADDR = 0x70010000
OUT_SZ_ADDR  = 0x70010008

dz_data_start = pos - 16  # HACK: data starts right after the file table
# Actually, let me find the real data start
# The file table starts at pos and has nfiles*16 bytes
file_table_start = pos
data_start = file_table_start + nfiles * 16

comp_data = dz[data_start + forge["offset"]:data_start + forge["offset"] + forge["comp_size"]]
print("Compressed data at offset %d, %d bytes" % (data_start + forge["offset"], len(comp_data)))

mu.mem_write(IN_BUF, comp_data)
w32(OUT_PTR_ADDR, 0)
w32(OUT_SZ_ADDR, 0x100000)

FUNC_ADDR = 0x51c1c  # s3eCompressionDecomp
RET_ADDR  = 0x9FFFF00

SP = STACK + 0x80000
mu.reg_write(UC_ARM_REG_SP, SP)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R0, forge["comp_size"])
mu.reg_write(UC_ARM_REG_R1, IN_BUF)
mu.reg_write(UC_ARM_REG_R2, OUT_PTR_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUT_SZ_ADDR)
w32(SP, forge["type"])

# Pre-set the read-callback globals that s3eCompressionDecompInit reads
# DAT_000d8514 = input pointer (Ghidra addr - 0x10000 = 0xc8514)
# DAT_000d8518 = input size
GLBL_IN_PTR = 0xc8514
GLBL_IN_SZ = 0xc8518
w32(GLBL_IN_PTR, IN_BUF)
w32(GLBL_IN_SZ, forge["comp_size"])

# ── Debug hooks ──
def trace_function_entry(mu, address, size, user_data):
    """Log key function entries."""
    if address == 0x51414:  # s3eCompressionDecompInit (Ghidra 0x61414)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        print("  > s3eCompressionDecompInit(%d, 0x%x, %d)" % (r0, r1, r2))
    elif address == 0x520bc:  # FUN_000620bc (Ghidra 0x620bc)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print("  > FUN_000620bc(ctx=0x%x)" % r0)
    elif address == 0x514f4:  # s3eCompressionDecompInit + 0xe0 (slot found free)
        r0 = mu.reg_read(UC_ARM_REG_R4)
        print("  > Init: slot found free! slot_idx=%d" % r0)
    elif address == 0x514e4:  # bl FUN_0006be3c (all slots full)
        print("  > Init: ALL 4 SLOTS FULL!")
    elif address == 0x514ec:  # return 0
        print("  > Init: returning 0 (early exit)")
    elif address == 0x51208:  # FUN_00061208 - the read callback!
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        print("  > READ CALLBACK(ctx=0x%x buf=0x%x sz=%d)" % (r0, r1, r2))

# Hook the key decision points in init
mu.hook_add(UC_HOOK_CODE, trace_function_entry, begin=0x514f4, end=0x514f8)
mu.hook_add(UC_HOOK_CODE, trace_function_entry, begin=0x514e4, end=0x514e8)
mu.hook_add(UC_HOOK_CODE, trace_function_entry, begin=0x514ec, end=0x514f0)
mu.hook_add(UC_HOOK_CODE, trace_function_entry, begin=0x51208, end=0x51210)

print("\n=== Emulating s3eCompressionDecomp @ 0x%05x ===" % FUNC_ADDR)
print("  R0=%d  R1=0x%08x  R2=0x%08x  R3=0x%08x" % (forge["comp_size"], IN_BUF, OUT_PTR_ADDR, OUT_SZ_ADDR))
print("  [SP]=type=%d" % forge["type"])

try:
    mu.emu_start(FUNC_ADDR, RET_ADDR, timeout=30_000_000)
    print("\n=== Returned! ===")
    ret_code = mu.reg_read(UC_ARM_REG_R0)
    out_ptr = r32(OUT_PTR_ADDR)
    out_sz = r32(OUT_SZ_ADDR)
    print("Return code: %d" % ret_code)
    print("Output: ptr=0x%08x sz=%d" % (out_ptr, out_sz))
    if out_ptr and out_sz and out_sz < 0x2000000:
        result = bytes(mu.mem_read(out_ptr, out_sz))
        out_path = REPO + "/forge_decoded.xml"
        with open(out_path, "wb") as f:
            f.write(result)
        print("Saved %d bytes to %s" % (len(result), out_path))
        print("First bytes: %s" % repr(result[:80]))
    else:
        print("No valid output")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    lr = mu.reg_read(UC_ARM_REG_LR)
    sp = mu.reg_read(UC_ARM_REG_SP)
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r12 = mu.reg_read(UC_ARM_REG_R12)
    print("\n!!! CRASH PC=0x%05x LR=0x%05x SP=0x%08x R12=0x%08x" % (pc, lr, sp, r12))
    print("  R0=0x%08x  Error: %s" % (r0, e))
    try:
        instr = struct.unpack("<I", mu.mem_read(pc, 4))[0]
        print("  Instr at PC: 0x%08x" % instr)
    except: pass
    print("Stack:")
    try:
        for i in range(min(20, (0x90100000 - sp)//4)):
            v = r32(sp + i*4)
            print("  [%02x] 0x%08x" % (i*4, v))
    except: pass

print("\n--- zlib streams still open: %d ---" % len(z_streams))
