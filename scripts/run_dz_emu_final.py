#!/usr/bin/env python3
"""
Final DZ decompression emulation.
Key insight: FUN_000a6a64 (ELF VA 0x96a64) is a memset wrapper that branches
to PLT[memset]+4 (word 2) which recomputes R12 and then does LDR PC,[R12,#offset].
But R12 gets computed to the wrong value (0xC1450) in our emulation.

Solution: Hook the B instruction at 0x96a78 (b #0xc448) and handle memset directly.
"""

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
DATA  = 0x70000000; mu.mem_map(DATA,  0x02000000, UC_PROT_ALL)  # 32MB: includes OUT_BUF at 0x71000000
SCRATCH = 0x7F000000; mu.mem_map(SCRATCH, 0x01000000, UC_PROT_ALL)
SCRATCH_OFF = [0x7F001000]

def emit(*words):
    off = SCRATCH_OFF[0]
    buf = b"".join(struct.pack("<I", w) if isinstance(w, int) else w for w in words)
    mu.mem_write(off, buf)
    SCRATCH_OFF[0] = off + len(buf)
    return off

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

# ── Build trampoline area ──
TRAP = 0x7F100000
w32(TRAP, 0xe7f000f0)  # UDF (should never be reached)

# Make a dispatch trampoline: saves regs, sets R0=func_id, jumps to TRAP
# The TRAP handler reads function id from R0 and arguments from stack
dispatch_tramps = {}
def make_dispatch(func_id):
    addr = emit(
        0xe92d400f,      # STMFD SP!, {R0-R3, LR}
        0xe3a00000 | func_id,  # MOV R0, #func_id
        0xe51ff004,      # LDR PC, [PC, #-4]
        TRAP,
    )
    return addr

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
        newpc = lr_orig & ~1
        mu.reg_write(UC_ARM_REG_PC, newpc)
        if (lr_orig & 1) and not thumb:
            mu.reg_write(UC_ARM_REG_CPSR, cpsr | (1 << 5))

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
        mu.mem_write(r0_orig, bytes([r1_orig & 0xFF]) * r2_orig)
        ret(r0_orig)
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
        if stream_addr not in z_streams: ret(-2); return
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
                ret(1 if len(out_data) < avail_out else 0)
            except:
                ret(-3)
        else: ret(0)
    elif func_id == 12:  # inflateEnd
        stream_addr = r0_orig
        if stream_addr in z_streams: del z_streams[stream_addr]
        ret(0)
    else:
        print(f"    UNKNOWN func_id={func_id}")
        ret(0)

mu.hook_add(UC_HOOK_CODE, do_hook)

# ── Debug hooks for init and read callback ──
INIT_FUNC = 0x51414   # s3eCompressionDecompInit (ELF VA)
READ_CB   = 0x51208   # FUN_00061208 input read callback (ELF VA)

def debug_init(mu, address, size, user_data):
    if address != INIT_FUNC: return
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r1 = mu.reg_read(UC_ARM_REG_R1)
    r2 = mu.reg_read(UC_ARM_REG_R2)
    r3 = mu.reg_read(UC_ARM_REG_R3)
    sp = mu.reg_read(UC_ARM_REG_SP)
    print(f"  [DEBUG] s3eCompressionDecompInit(type={r0}, cb=0x{r1:08x}, user=0x{r2:08x})")

def debug_readcb(mu, address, size, user_data):
    if address != READ_CB: return
    r0 = mu.reg_read(UC_ARM_REG_R0)
    sp = mu.reg_read(UC_ARM_REG_SP)
    r0_val = r32(r0) if r0 > 0x1000 else 0
    r3_val = r32(r0 + 4) if r0 > 0x1000 else 0
    G0 = r32(0xC8514)  # G_base[0] (ELF VA = Ghidra 0xD8514 - 0x10000)
    G1 = r32(0xC8518)  # G_base[4] (ELF VA = Ghidra 0xD8518 - 0x10000)
    print(f"  [DEBUG] read_cb(buf_ptr=0x{r0:08x}, *buf=0x{r0_val:08x}, buf[1]={r3_val}, G[0]=0x{G0:x}, G[4]=0x{G1:x})")

# mu.hook_add(UC_HOOK_CODE, debug_init)
# mu.hook_add(UC_HOOK_CODE, debug_readcb)
# ── Debug: trace type-4 init ──
TYPE4_INIT = 0x520bc   # FUN_000620bc (ELF VA)

def debug_type4_init(mu, address, size, user_data):
    """Trace FUN_000620bc (type-4 decompressor init)."""
    if address == TYPE4_INIT:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [TYPE4] FUN_000620bc(slot=0x{r0:08x})")
    elif address == TYPE4_INIT + 0x78:  # after FUN_000491d0 call
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [TYPE4] FUN_000491d0 returned {r0}")
    elif address == TYPE4_INIT + 0x9c:  # after malloc
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if r0:
            print(f"  [TYPE4] alloc 0x80000 at 0x{r0:08x} — SUCCESS!")
        else:
            print(f"  [TYPE4] alloc failed!")

# Hook at the return from FUN_000491d0 (call at 0x52148, return check at 0x5214c)
# FUN_000620bc calls FUN_000491d0 at ELF 0x52148
# After return: BL at 0x52148 -> next PC = 0x5214c
# The code checks: compare R0 with 2
# Let's just hook the whole function and log at key points

T4_INIT_STATE = {'entered': False, 'step': 0}
def debug_t4_full(mu, address, size, user_data):
    global T4_STATE
    if address == TYPE4_INIT:
        T4_INIT_STATE['entered'] = True
        T4_INIT_STATE['step'] = 0
    if not T4_INIT_STATE['entered']:
        return
    if address == TYPE4_INIT + 0x3c:  # after FUN_00061e38 (read 13 bytes)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [TYPE4] FUN_00061e38 returned {r0} (expected 13)")
        T4_INIT_STATE['step'] = 1
    elif address == TYPE4_INIT + 0x126:  # after alloc check
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if r0 == 0:
            print(f"  [TYPE4] alloc 0x70 FAILED!")
        else:
            print(f"  [TYPE4] alloc 0x70 at 0x{r0:08x}")
        T4_INIT_STATE['step'] = 2
    elif address == TYPE4_INIT + 0x130:  # after FUN_000491d0 call
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [TYPE4] FUN_000491d0 returned {r0}")
        T4_INIT_STATE['step'] = 3
        if r0 != 0:
            print(f"  *** TYPE-4 INIT FAILED at FUN_000491d0! ***")
    elif address == TYPE4_INIT + 0x15c:
        print(f"  [TYPE4] SUCCESS path reached!")
        T4_INIT_STATE['step'] = 4
    elif address >= TYPE4_INIT + 0x1a0:
        # Check if returning
        if (mu.mem_read(address, 4) == b'\x70\xbd\x08\x00' or  # common ret sequence
            mu.mem_read(address, 4) == b'\x1e\xff\x2f\xe1'):   # BX LR
            r0 = mu.reg_read(UC_ARM_REG_R0)
            print(f"  [TYPE4] RETURNING R0={r0}")
            T4_INIT_STATE['entered'] = False

# ── Direct debug hooks for type-4 init ──
T4_HOOK_ADDRS = set([
    0x520bc, # FUN_000620bc entry
    0x520f4, # after FUN_00061e38 call (read 13 bytes) - check R0
    0x52124, # after alloc 0x70 - check R0
    0x52148, # call to FUN_000491d0
    0x5214c, # after FUN_000491d0 returns - check R0
    0x5216c, # after alloc 0x80000 - check R0
    0x521fc, # after ERROR: log failure
])

def debug_t4(mu, address, size, user_data):
    if address == 0x520bc:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [T4] FUN_000620bc(slot=0x{r0:08x})")
    elif address == 0x520f4:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [T4] read 13 bytes: returned {r0}")
        # dump what was read from stack
        sp = mu.reg_read(UC_ARM_REG_SP)
        buf = bytes(mu.mem_read(sp-0x20, 16))
        print(f"  [T4]   stack buf: {buf[:13].hex()}")
    elif address == 0x52124:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if r0 == 0:
            print(f"  [T4] alloc 0x70 FAILED!")
        else:
            print(f"  [T4] alloc 0x70 at 0x{r0:08x}")
    elif address == 0x5214c:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [T4] FUN_000491d0 returned {r0}")
    elif address == 0x5216c:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if r0:
            print(f"  [T4] alloc 0x80000 at 0x{r0:08x} SUCCESS!")
        else:
            print(f"  [T4] alloc 0x80000 FAILED!")
    elif address == 0x521fc:
        print(f"  [T4] ERROR PATH REACHED!")
        
# Hook init epilogue at 0x614ec (add sp, #0x14) to see return value
INIT_EPILOGUE = 0x514ec
def debug_init_ret(mu, address, size, user_data):
    if address != INIT_EPILOGUE: return
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r4 = mu.reg_read(UC_ARM_REG_R4)
    # r4 is about to be restored from stack, r0 was set by cpy r0, r4
    print(f"  [INIT_RET] R0={r0} (will be return value)")
# mu.hook_add(UC_HOOK_CODE, debug_init_ret)
# mu.hook_add(UC_HOOK_CODE, debug_t4)
# ── Hook: trace decompression reads ──
DECOMP_READ_BODY = 0x51a10  # s3eCompressionDecompRead (ELF VA)
T4_READ_FUNC = 0x51f60      # FUN_00061f60 (type-4 read)
T4_DECODE_FUNC = 0x489f8    # FUN_000489f8 (type-4 decode core)
T4_FILL_BUF = 0x51e38       # FUN_00061e38 (fill buffer from callback)
T4_DECODE_STEP = 0x4751c    # FUN_0004751c (range decode step)
T4_LZ77_STEP = 0x47adc      # FUN_00047adc (LZ77 match step)

def debug_decomp(mu, address, size, user_data):
    if address == DECOMP_READ_BODY:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        print(f"  [DECOMP_READ] slot={r0} buf=0x{r1:08x} ptr_to_size=0x{r2:08x}", flush=True)
    elif address == T4_READ_FUNC:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        print(f"  [T4_READ] slot=0x{r0:08x} buf=0x{r1:08x} size_ptr=0x{r2:08x}", flush=True)
    elif address == T4_DECODE_FUNC:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        r3 = mu.reg_read(UC_ARM_REG_R3)
        print(f"  [T4_DECODE] state=0x{r0:08x} out=0x{r1:08x} out_size=0x{r2:08x} in_buf=0x{r3:08x}", flush=True)
    elif address == T4_FILL_BUF:
        print(f"  [READ_CALLBACK] FUN_00061e38 called", flush=True)

# Enable by uncommenting:
# mu.hook_add(UC_HOOK_CODE, debug_decomp)
# Also hook the actual decode functions to trace crashes
T4_DECODE_CORE = 0x389f8   # FUN_000489f8 (ELF: Ghidra 0x489F8 - 0x10000)
T4_RANGE_DEC = 0x3751c     # FUN_0004751c (ELF: Ghidra 0x4751C - 0x10000)
T4_LZ77 = 0x37adc          # FUN_00047adc (ELF: Ghidra 0x47ADC - 0x10000)
T4_RANGE_CALL1 = 0x38d28   # ELF for Ghidra 0x48D28 (1st range call)
T4_RANGE_CALL2 = 0x38ff8   # ELF for Ghidra 0x48FF8 (2nd range call)

def debug_t4_decode(mu, address, size, user_data):
    """Trace type-4 decode core entry and key calls."""
    if address == T4_DECODE_CORE:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        r3 = mu.reg_read(UC_ARM_REG_R3)
        sp = mu.reg_read(UC_ARM_REG_SP)
        print(f"  [DECODE] state=0x{r0:08x} out=0x{r1:08x} out_sz=0x{r2:08x} in=0x{r3:08x} sp=0x{sp:08x}", flush=True)
    elif address == T4_RANGE_DEC:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        print(f"  [RANGE_DEC] state=0x{r0:08x} table=0x{r1:08x} idx={r2}", flush=True)
    elif address == T4_LZ77:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        r3 = mu.reg_read(UC_ARM_REG_R3)
        print(f"  [LZ77] state=0x{r0:08x} window=0x{r1:x} end=0x{r2:x} buf=0x{r3:08x}", flush=True)
    # Track range coder calls from decode core
    elif address == T4_RANGE_CALL1 or address == T4_RANGE_CALL2:
        print(f"  [RANGE_CALLED] at 0x{address:05x}", flush=True)
    # Debug: check if we reach mid-point in decode (0x38B90 = ~halfway to first range call)
    elif address == 0x38b90:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r9 = mu.reg_read(UC_ARM_REG_R9)
        print(f"  [DECODE_MID] r0=0x{r0:08x} state(r9)=0x{r9:08x}", flush=True)
    elif address == 0x38a40:  # label LAB_00048a40 in Ghidra
        r3 = mu.reg_read(UC_ARM_REG_R3)
        r9 = mu.reg_read(UC_ARM_REG_R9)
        print(f"  [DECODE_LOOP] state=0x{r9:08x} r3={r3}", flush=True)
    elif address == 0x38ec4:  # LAB_00048ec4 (Ghidra 0x48EC4) - alternate path for no input
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r9 = mu.reg_read(UC_ARM_REG_R9)
        sp = mu.reg_read(UC_ARM_REG_SP)
        print(f"  [DECODE_NODATA] state=0x{r9:08x} r0={r0} sp=0x{sp:08x}", flush=True)
    elif address == 0x38fec:  # 0x48FEC Ghidra - just before range coder call
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        r9 = mu.reg_read(UC_ARM_REG_R9)
        print(f"  [PRE_RANGE] state=0x{r9:08x} r0=0x{r0:08x} r1=0x{r1:08x} r2={r2}", flush=True)
    elif address == 0x38ff8:  # BL range coder
        print(f"  [RANGE_CALLED!] at 0x38ff8", flush=True)
    elif address == 0x38ffc:  # after BL range coder
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"  [AFTER_RANGE] r0={r0}", flush=True)

# mu.hook_add(UC_HOOK_CODE, debug_t4_decode)
# ── Hook s3eRealloc (needed for output buffer allocation in decomp) ──

# ── Patch: hook FUN_0007f4e8 (custom allocator) to use our heap ──
# This function calls malloc internally through a PLT stub with wrong address
ALLOC_FUNC = 0x6f4e8  # Ghidra 0x7F4E8 - 0x10000
def hook_alloc(mu, address, size, user_data):
    if address != ALLOC_FUNC: return
    r0 = mu.reg_read(UC_ARM_REG_R0)  # size
    r1 = mu.reg_read(UC_ARM_REG_R1)  # second param
    lr = mu.reg_read(UC_ARM_REG_LR)  # return address
    pc = mu.reg_read(UC_ARM_REG_PC)
    print(f"    [ALLOC] Entry: R0=0x{r0:08x}, R1=0x{r1:08x}, LR=0x{lr:05x}, PC=0x{pc:05x}", flush=True)
    sz = r0 if r0 > 0 else (r1 if r1 > 0 else 0x70)
    if sz == 0: sz = 1
    p = heap_alloc(sz)
    mu.mem_write(p, b'\x00' * sz)
    print(f"    [ALLOC] size={sz} -> 0x{p:08x} (heap_ptr now: 0x{heap_ptr[0]:08x})", flush=True)
    # Return via BX LR
    mu.reg_write(UC_ARM_REG_R0, p)
    mu.reg_write(UC_ARM_REG_PC, lr & ~1)
    if lr & 1:
        cpsr = mu.reg_read(UC_ARM_REG_CPSR)
        mu.reg_write(UC_ARM_REG_CPSR, cpsr | (1 << 5))
    print(f"    [ALLOC] Set R0=0x{p:08x}, PC=0x{lr & ~1:05x}", flush=True)

mu.hook_add(UC_HOOK_CODE, hook_alloc)

# ── Hook s3eRealloc (needed for output buffer allocation in decomp) ──
S3E_REALLOC = 0x6f87c  # Ghidra 0x7F87C - 0x10000

def hook_realloc(mu, address, size, user_data):
    if address != S3E_REALLOC: return
    r0 = mu.reg_read(UC_ARM_REG_R0)  # old_ptr (0 for first call)
    r1 = mu.reg_read(UC_ARM_REG_R1)  # new_size
    lr = mu.reg_read(UC_ARM_REG_LR)
    if r0 == 0:
        # malloc
        p = heap_alloc(r1)
        mu.mem_write(p, b'\x00' * r1)
    else:
        # realloc - just alloc new, skip copying old for simplicity
        # s3eRealloc called with growing buffer: old is always in our heap
        p = heap_alloc(r1)
        mu.mem_write(p, b'\x00' * r1)
    print(f"    [REALLOC] old=0x{r0:08x} new_size={r1} -> 0x{p:08x}", flush=True)
    mu.reg_write(UC_ARM_REG_R0, p)
    mu.reg_write(UC_ARM_REG_PC, lr & ~1)
    if lr & 1:
        mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))

mu.hook_add(UC_HOOK_CODE, hook_realloc)

# ── Hook PLT entries directly for memcpy/memset used in decode ──
# PLT entries compute wrong GOT addresses; hook them directly
PLT_MEMCPY = 0xc94c  # PLT entry for memcpy (word 2) - called as +4
PLT_MEMSET = 0xc44c  # PLT entry for memset (word 2) - called as +4

def hook_plt_mem(mu, address, size, user_data):
    if address == PLT_MEMCPY:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        lr = mu.reg_read(UC_ARM_REG_LR)
        if r2 > 0 and r0 > 0x1000 and r1 > 0x1000:
            mu.mem_write(r0, bytes(mu.mem_read(r1, r2)))
        mu.reg_write(UC_ARM_REG_R0, r0)
        mu.reg_write(UC_ARM_REG_PC, lr & ~1)
        if lr & 1:
            mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))
    elif address == PLT_MEMSET:
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        lr = mu.reg_read(UC_ARM_REG_LR)
        if r2 > 0 and r0 > 0x1000:
            mu.mem_write(r0, bytes([r1 & 0xFF]) * r2)
        mu.reg_write(UC_ARM_REG_R0, r0)
        mu.reg_write(UC_ARM_REG_PC, lr & ~1)
        if lr & 1:
            mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))

mu.hook_add(UC_HOOK_CODE, hook_plt_mem)

# ── Hook the DZ decoder alloc/free callbacks directly ──
T4_ALLOC = 0x51f38  # LAB_00061f38 at Ghidra 0x61f38
T4_FREE  = 0x51f14  # LAB_00061f14 at Ghidra 0x61f14

def hook_t4_alloc(mu, address, size, user_data):
    if address == T4_ALLOC:
        r0 = mu.reg_read(UC_ARM_REG_R0)  # struct ptr
        r1 = mu.reg_read(UC_ARM_REG_R1)  # size
        lr = mu.reg_read(UC_ARM_REG_LR)
        sz = r1 if r1 > 0 else 0x80000
        p = heap_alloc(sz)
        mu.mem_write(p, b'\x00' * sz)
        print(f"    [T4_ALLOC] size={sz} -> 0x{p:08x}")
        mu.reg_write(UC_ARM_REG_R0, p)
        mu.reg_write(UC_ARM_REG_PC, lr & ~1)
        if lr & 1:
            mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))
    elif address == T4_FREE:
        lr = mu.reg_read(UC_ARM_REG_LR)
        print(f"    [T4_FREE] skip")
        mu.reg_write(UC_ARM_REG_R0, 0)
        mu.reg_write(UC_ARM_REG_PC, lr & ~1)
        if lr & 1:
            mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))

mu.hook_add(UC_HOOK_CODE, hook_t4_alloc)

# ── Replace FUN_000491d0 (DZ decoder state init) with Python version ──
FUN_491D0 = 0x491d0  # Ghidra 0x591d0 -> ELF 0x491d0... wait
# Ghidra 0x491D0 -> ELF 0x491D0 (segment 1 starts at 0)
# Actually Ghidra image base = 0x10000, so ELF = 0x491D0 - 0x10000 = 0x391D0
# But the function body is at Ghidra 0x491D0 which is in segment 1 starting at 0
# So ELF VA = 0x491D0
# Hmm wait: Ghidra image_base is 0x10000. FUN_000491d0 at Ghidra 0x491D0 means
# ELF VA = 0x491D0 because it's in segment 1 (vaddr=0, file_offset=0, so Ghidra addr = file_offset + img_base)
# So ELF VA = Ghidra addr - img_base = 0x491D0 - 0x10000 = 0x391D0 

FUN_491D0 = 0x391d0  # ELF VA (Ghidra 0x491D0 - 0x10000)
# ALSO try: the function might be called through a thunk at 0x52150+0xoffset
# Let me also hook the call site to verify
T4_CALL_491D0 = 0x52150  # bl FUN_000491d0 in T4 init

def debug_t4_call(mu, address, size, user_data):
    if address == FUN_491D0:
        lr = mu.reg_read(UC_ARM_REG_LR)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"    [491d0 ENTRY] LR=0x{lr:05x} R0=0x{r0:08x}")
    elif address == T4_CALL_491D0:
        print(f"    [T4_CALL] Calling FUN_000491d0 from T4 init")
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r1 = mu.reg_read(UC_ARM_REG_R1)
        r2 = mu.reg_read(UC_ARM_REG_R2)
        r3 = mu.reg_read(UC_ARM_REG_R3)
        print(f"      R0=0x{r0:08x} R1=0x{r1:08x} R2={r2} R3=0x{r3:08x}")

# mu.hook_add(UC_HOOK_CODE, debug_t4_call)
def hook_491d0(mu, address, size, user_data):
    """Replace FUN_000491d0: parse header, allocate buffers, init state."""
    if address != FUN_491D0: return
    
    r0 = mu.reg_read(UC_ARM_REG_R0)  # param_1 = __ptr (0x70-byte struct)
    r1 = mu.reg_read(UC_ARM_REG_R1)  # param_2 = 13-byte header (stack addr)
    r2 = mu.reg_read(UC_ARM_REG_R2)  # param_3 = 5
    r3 = mu.reg_read(UC_ARM_REG_R3)  # param_4 = &local_30 (callback pair)
    lr = mu.reg_read(UC_ARM_REG_LR)
    
    # Read the 13-byte header from the stack
    header = bytes(mu.mem_read(r1, 13))
    print(f"    [491d0] __ptr=0x{r0:08x} header={header[:13].hex()}")
    
    # Parse header
    byte0 = header[0]
    b1, b2, b3, b4 = header[1], header[2], header[3], header[4]
    
    if byte0 > 0xe0:
        print(f"    [491d0] byte0 > 0xe0, returning 4")
        mu.reg_write(UC_ARM_REG_R0, 4)
        mu.reg_write(UC_ARM_REG_PC, lr & ~1)
        return
    
    # Compute parameters
    uVar5 = byte0
    uVar4 = (b3 << 16) | (b2 << 8) | b1 | (b4 << 24)
    if uVar4 < 0x1000: uVar4 = 0x1000
    uVar2 = (uVar5 // 9) % 5
    uVar3 = (0x300 << (uVar2 + uVar5 % 9)) + 0x736
    
    # Allocate first buffer (uVar3 * 2 bytes)
    sz1 = uVar3 * 2
    buf1 = heap_alloc(sz1)
    mu.mem_write(buf1, b'\x00' * sz1)
    print(f"    [491d0] buf1={sz1} bytes @ 0x{buf1:08x}")
    
    # Write to struct: param_1[0x15] = uVar3, param_1[4] = buf1
    w32(r0 + 0x54, uVar3)  # param_1[0x15] at offset 0x54
    w32(r0 + 0x10, buf1)   # param_1[4] at offset 0x10
    
    # Allocate second buffer (uVar4 bytes = window, but cap at 1MB)
    uVar4_capped = min(uVar4, 0x100000)  # 1 MB max
    buf2 = heap_alloc(uVar4_capped)
    mu.mem_write(buf2, b'\x00' * uVar4_capped)
    print(f"    [491d0] buf2={uVar4_capped} bytes @ 0x{buf2:08x} (uVar4={uVar4})")
    
    # Write to struct: buf2 and window size (use CAPPED size!)
    w32(r0 + 0x14, buf2)           # param_1[5] at offset 0x14
    w32(r0 + 0x28, uVar4_capped)   # param_1[10] = actual window size (NOT uVar4!)
    
    # Write mode params
    w32(r0, uVar5 % 9)             # param_1[0] = mode
    w32(r0 + 4, uVar2)             # param_1[1]
    w32(r0 + 0xc, uVar4_capped)    # param_1[3] = actual window size (NOT uVar4!)
    w32(r0 + 8, (uVar5 // 9) // 5) # param_1[2]
    
    # Range coder initial state (critical! FUN_000489d0 sets has_data=1, 
    # so FUN_000489f8 skips init and reads param_1[7]/[8] directly)
    w32(r0 + 0x1c, 0xFFFFFFFF)     # param_1[7] = range = -1 (0xFFFFFFFF)
    # First 4 bytes of compressed stream (offset 13): 0x59, 0xb8, 0xd9, 0x5b
    range_init = 0x5BD9B859        # LE: 0x59|(0xb8<<8)|(0xd9<<16)|(0x5b<<24)
    w32(r0 + 0x20, range_init)     # param_1[8] = range coder initial code
    # Range init code reads bytes from state[0x18,0x5C-0x5F] in specific order:
    # param_1[8] = state[0x5E]<<16 | state[0x5D]<<24 | state[0x18] | state[0x5F]<<8
    # For init=0x5BD9B859: 0x5B<<24 | 0xD9<<16 | 0x59 | 0xB8<<8
    # So: state[0x18]=0x59, state[0x5D]=0x5B, state[0x5E]=0xD9, state[0x5F]=0xB8
    mu.mem_write(r0 + 0x18, b'\x59')        # LSB of range init
    mu.mem_write(r0 + 0x5D, b'\x5b\xd9\xb8') # remaining 3 bytes (0x5B, 0xD9, 0xB8)
    # bits_remaining = 0 (heap default, FUN_000489d0 str to 0x48 is NOPed)
    # 0 leads to LZ77 path which then calls range coder when no data
    
    # FUN_000489d0 will set these after we return, but that's OK:
    # offset 0x4C=1 (has_data), offset 0x50=1 (inited)
    # We must NOT set them here - FUN_000489d0 does it
    
    print(f"    [491d0] Range init=0x{range_init:08x}", flush=True)
    
    print(f"    [491d0] SUCCESS (return 0)")
    mu.reg_write(UC_ARM_REG_R0, 0)
    mu.reg_write(UC_ARM_REG_PC, lr & ~1)
    if lr & 1:
        mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) | (1 << 5))

mu.hook_add(UC_HOOK_CODE, hook_491d0)

# ── Trace ALL instructions in FUN_000489f8 with branches ──
DECODE_ENTRY = 0x389f8
TRACE_START = 0x389f8   # FUN_000489f8 entry
TRACE_END = 0x38fc0     # memcpy

trace_active = [False]
trace_count = [0]
def trace_all_decode(mu, address, size, user_data):
    if address == DECODE_ENTRY:
        trace_active[0] = True
        trace_count[0] = 0
    if not trace_active[0]:
        return
    if trace_count[0] < 2000:  # limit output to 2000 instructions
        trace_count[0] += 1
        r0 = mu.reg_read(UC_ARM_REG_R0)
        r3 = mu.reg_read(UC_ARM_REG_R3)
        r4 = mu.reg_read(UC_ARM_REG_R4)
        r12 = mu.reg_read(UC_ARM_REG_R12)
        print(f"  0x{address:05x}: r0={r0} r3={r3} r4={r4}", flush=True)
    if address >= 0x38fc0:
        trace_active[0] = False

mu.hook_add(UC_HOOK_CODE, trace_all_decode)
# Disable verbose decode core hook
# mu.hook_add(UC_HOOK_CODE, debug_t4_decode)

# Hook at the CALL to FUN_000620bc (T4 init) at 0x51870
# And at the bics type check at 0x5184c to see if we get past it
T4_CALL = 0x51870
TYPE_CHECK = 0x5184c
BVAR4_CHECK = 0x51848

def debug_flow(mu, address, size, user_data):
    if address == T4_CALL:
        print(f"  [FLOW] CALL T4 init at 0x51870! IN BUF!")
        r0 = mu.reg_read(UC_ARM_REG_R0)
        print(f"    R0=slot=0x{r0:08x}")
    elif address == TYPE_CHECK:
        r3 = mu.reg_read(UC_ARM_REG_R3)
        r6 = mu.reg_read(UC_ARM_REG_R6)
        print(f"  [FLOW] TYPE_CHECK: r6(type)={r6} r3={r3} (will bne if r3!=0)")
    elif address == BVAR4_CHECK:
        r3 = mu.reg_read(UC_ARM_REG_R3)
        print(f"  [FLOW] BVAR4 check: r3={r3} (beq to error if r3==0)")
    elif address == 0x51614:
        r6 = mu.reg_read(UC_ARM_REG_R6)
        print(f"  [FLOW] REACHED ERROR PATH! r6(type)={r6}")
        
# mu.hook_add(UC_HOOK_CODE, debug_flow)
# ── PATCH: skip bVar4=false from 0x900000 check ──
# At Ghidra 0x61838 (ELF 0x51838): cmp r2, #0x900000
# Change to: cmp r2, r2 (always equal, preserves bVar4)
# Original bytes: 09 06 52 e3
# Patched bytes:  00 00 52 e1
print("=== Patching FUN_000489d0 (str to 0x48,0x4C,0x50 -> NOP) ===")
PATCH_489D0_48 = 0x389DC
old489d0 = bytes(mu.mem_read(PATCH_489D0_48, 4))
mu.mem_write(PATCH_489D0_48, b'\x00\x00\xa0\xe1')
print(f"  Patched 0x{PATCH_489D0_48:05x}: {old489d0.hex()} -> NOP (str r3,[r0,#0x48])")
PATCH_489D0_4C = 0x389E4  
old489d0_4c = bytes(mu.mem_read(PATCH_489D0_4C, 4))
mu.mem_write(PATCH_489D0_4C, b'\x00\x00\xa0\xe1')
print(f"  Patched 0x{PATCH_489D0_4C:05x}: {old489d0_4c.hex()} -> NOP (str r3,[r0,#0x4C])")
PATCH_489D0_50 = 0x389E8  
old489d0_50 = bytes(mu.mem_read(PATCH_489D0_50, 4))
mu.mem_write(PATCH_489D0_50, b'\x00\x00\xa0\xe1')
print(f"  Patched 0x{PATCH_489D0_50:05x}: {old489d0_50.hex()} -> NOP (str r3,[r0,#0x50])")

print("=== Patching bVar4/type checks to force T4 init ===")

# Patch 1: at 0x61848 (beq 0x00061614) — NOP it, always fall through to type check
# Original: 71 ff ff 0a = beq 0x61614
# Patched:  00 00 a0 e1 = mov r0, r0 (NOP)
P1 = 0x51848
b_old = bytes(mu.mem_read(P1, 4))
mu.mem_write(P1, b'\x00\x00\xa0\xe1')
print(f"  beq->NOP @0x{P1:05x}: {b_old.hex()} -> 00 00 a0 e1")

# Patch 2: at 0x61850 (bne 0x61998) — NOP it, type check failure also falls through
# Original: 50 00 00 1a = bne 0x61998
# Patched:  00 00 a0 e1 = NOP
P2 = 0x51850
b2_old = bytes(mu.mem_read(P2, 4))
mu.mem_write(P2, b'\x00\x00\xa0\xe1')
print(f"  bne->NOP @0x{P2:05x}: {b2_old.hex()} -> 00 00 a0 e1")

# ── Strategy: patch BOTH PLT entries and GOT entries ──
# 1. Identify all funcs we need to hook from .rel.plt
dynstr = SO[0x41b0:0x41b0+0x3b28]
hook_map = { "malloc": 0, "free": 1, "realloc": 2, "memcpy": 3, "memset": 4, "calloc": 5,
             "inflateInit2_": 10, "inflate": 11, "inflateEnd": 12 }

# Patch GOT entries (for normal PLT calls through word 1)
print("=== Patching GOT entries ===")
for i in range(148):
    r_off, r_info = struct.unpack_from("<II", SO, 0xbe50 + i * 8)
    sym_idx = r_info >> 8
    st_name = struct.unpack_from("<I", SO, 0x1470 + sym_idx * 16)[0]
    end = st_name
    while dynstr[end] != 0: end += 1
    name = dynstr[st_name:end].decode("latin-1", errors="replace")
    if name in hook_map:
        tramp = make_dispatch(hook_map[name])
        w32(r_off, tramp)
        print(f"  {name:15s} GOT@0x{r_off:05x} -> trampoline@0x{tramp:05x}")

# 2. Now handle the special case: FUN_000a6a64 (memset wrapper) branches to PLT+4
# We need to add the direct B hook at 0x96a78
# But first, let's also set up correct GOT entries for the PLT+4 code path.
# The PLT entry for memset is at 0xc444, and it computes:
#   word2: r12 = PC + 0 = 0xc450  
#   word3: r12 += 0x0B5000 = 0xC1450
# Then falls to word1 of next PLT: ldr pc, [r12, offset]
# 
# We can patch the GOT entries at [0xC1450 + offset] to point to our dispatch.
# But finding the exact address is tricky. 
# 
# SIMPLER approach: patch the PLT stub at 0xc444 to redirect to our dispatch directly!

# Actually, EVEN SIMPLER: set R12 = GOT base (= 0xC1ECC) before emulation starts!
# This makes ALL PLT entries work correctly through word 1 (normal PLT path).
# The function at 0x96a64 branches to word 2+3 but those overwrite R12 anyway.
# For the word 1 path: R12 = GOT_base, [R12 + 0xafc] = correct GOT entry.
# For the word 2+3 path: R12 is recomputed (to wrong value), causing issues.
# So we ALSO need to handle the word 2+3 path.

# Solution: patch the PLT stub's word 2+3 to do something useful.
# Change word 2 from "add r12, pc, #0" to a jump to our dispatch.
# But word 3 also needs to be a valid instruction (NOP-like).

# Actually, the cleanest fix: since b 0xc448 jumps there, we can also
# just hook 0x96a78 (the B instruction) and handle memset there directly.
# When execution reaches 0x96a78:
#   R0 = destination ptr
#   R1 = 0 (already set to 0 by mov r1, #0 at 0x96a74)
#   R2 = count (set by rsb r2, r0, r1 at 0x96a70)
# We do: memset(R0, R1, R2) and return to LR

def handle_memset_wrapper(mu, address, size, user_data):
    """Hook at 0x96a78 (B instruction in FUN_000a6a64)."""
    if address != 0x96a78: 
        return
    
    r0 = mu.reg_read(UC_ARM_REG_R0)  # dst
    r1 = mu.reg_read(UC_ARM_REG_R1)  # fill byte (= 0)
    r2 = mu.reg_read(UC_ARM_REG_R2)  # count
    lr = mu.reg_read(UC_ARM_REG_LR)  # return address
    
    print(f"    memset_wrapper: ptr=0x{r0:08x} val=0x{r1:02x} count={r2} lr=0x{lr:05x}")
    
    if r2 > 0 and r0 > 0x100000:
        mu.mem_write(r0, bytes([r1 & 0xFF]) * r2)
    
    # Return to caller (skip the B instruction; go back to caller)
    mu.reg_write(UC_ARM_REG_PC, lr & ~1)
    if lr & 1:
        cpsr = mu.reg_read(UC_ARM_REG_CPSR)
        mu.reg_write(UC_ARM_REG_CPSR, cpsr | (1 << 5))

mu.hook_add(UC_HOOK_CODE, handle_memset_wrapper)

# ── Parse DZ ──
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
print(f"Compressed: {len(comp_data)} bytes")
print(f"First 16: {comp_data[:16].hex()}")

# ── Set up emulation ──
IN_BUF = 0x70000000
OUT_PTR_ADDR = 0x70010000
OUT_SZ_ADDR  = 0x70010008

OUT_BUF = 0x71000000  # pre-allocated output buffer (separate from input)

mu.mem_write(IN_BUF, comp_data)
w32(OUT_PTR_ADDR, OUT_BUF)  # pointer to pre-allocated buffer
w32(OUT_SZ_ADDR, 0x20000)   # 128KB buffer (larger than uncompressed 102953)

# R12 (IP) is CRITICAL: must equal GOT base (0xC1ECC) for PLT word 1 to work
GOT_BASE = 0xC1ECC

FUNC_ADDR = 0x51c1c  # s3eCompressionDecomp (ELF VA)
RET_ADDR  = 0x9FFFF00

SP = STACK + 0x80000
mu.reg_write(UC_ARM_REG_SP, SP)
mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
mu.reg_write(UC_ARM_REG_R12, GOT_BASE)  # CRITICAL: set GOT base!
mu.reg_write(UC_ARM_REG_R0, IN_BUF)            # R0 = input buffer (NOT size!)
mu.reg_write(UC_ARM_REG_R1, len(comp_data))    # R1 = compressed size (NOT buffer!)
mu.reg_write(UC_ARM_REG_R2, OUT_PTR_ADDR)
mu.reg_write(UC_ARM_REG_R3, OUT_SZ_ADDR)
w32(SP, 4)  # type hint = 4

print(f"\n=== Emulating s3eCompressionDecomp @ 0x{FUNC_ADDR:05x} ===")
print(f"  R0=IN_BUF=0x{IN_BUF:08x}  R1=size={len(comp_data)}  [SP]=type=4  R12=GOT_base=0x{GOT_BASE:05x}")

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
        gt_path = REPO + "/assets/forge.xml"
        with open(gt_path, "rb") as f:
            gt = f.read()
        if result == gt:
            print(f"\n*** PERFECT MATCH with ground truth! ({len(result)} bytes) ***")
        else:
            min_len = min(len(result), len(gt))
            for i in range(min_len):
                if result[i] != gt[i]:
                    print(f"\nMismatch at byte {i}: got 0x{result[i]:02x} expected 0x{gt[i]:02x}")
                    if i > 10:
                        print(f"  Prev 10: {result[i-10:i].hex()}")
                        print(f"  GT   10: {gt[i-10:i].hex()}")
                    break
            print(f"  len(got)={len(result)} len(gt)={len(gt)}")
    else:
        print("No valid output")
except UcError as e:
    pc = mu.reg_read(UC_ARM_REG_PC)
    lr = mu.reg_read(UC_ARM_REG_LR)
    sp = mu.reg_read(UC_ARM_REG_SP)
    cpsr = mu.reg_read(UC_ARM_REG_CPSR)
    thumb = (cpsr >> 5) & 1
    r0 = mu.reg_read(UC_ARM_REG_R0)
    r12 = mu.reg_read(UC_ARM_REG_R12)
    print(f"\n!!! CRASH PC=0x{pc:05x} LR=0x{lr:05x} SP=0x{sp:08x} Thumb={thumb}")
    print(f"  R0=0x{r0:08x} R12=0x{r12:08x}  Error: {e}")
    try:
        instr = struct.unpack("<I", mu.mem_read(pc & ~1, 4))[0]
        print(f"  Instr at PC: 0x{instr:08x}")
    except: pass

print(f"\n--- zlib streams still open: {len(z_streams)} ---")
