#!/usr/bin/env python3
"""
DZ type-4 decoder via Unicorn ARM emulation of libs3e_android.so.

Emulates the original Marmalade derbh decompressor for byte-exact output.
Verifies against pre-extracted assets/ ground truth.

Verified symbol addresses [ORIGINAL] (from libs3e_android.so .dynsym):
  s3eCompressionDecomp     = 0x051c1c
  s3eCompressionDecompInit = 0x051414
  s3eCompressionDecompRead = 0x051a10
  s3eCompressionDecompFinal= 0x051250
  s3eMallocBase            = 0x06e770
  s3eFreeBase              = 0x06e5f8
  s3eReallocBase           = 0x06ea68
"""
import struct, os, sys
from unicorn import *
from unicorn.arm_const import *

REPO = "/home/z/resf2-analysis/reSF2"
SO_PATH = f"{REPO}/reverse/binaries/libs3e_android.so"
DZ_PATH = f"{REPO}/sf2/assets/assets/files.dz"
ASSETS_DIR = f"{REPO}/assets"

# Verified addresses
ADDR_DECOMP      = 0x051c1c
ADDR_DECOMP_INIT = 0x051414
ADDR_DECOMP_READ = 0x051a10
ADDR_DECOMP_FIN  = 0x051250
ADDR_MALLOC      = 0x06e770
ADDR_FREE        = 0x06e5f8
ADDR_REALLOC     = 0x06ea68
CODER_TABLE      = 0x0c8514   # 4 slots * 0x88, slot+0x64=init fn, slot+0x68=read fn

with open(SO_PATH, "rb") as f:
    SO = f.read()

# ---------- ELF loading ----------
def parse_elf():
    e_phoff = struct.unpack_from("<I", SO, 0x1c)[0]
    e_phentsize = struct.unpack_from("<H", SO, 0x2a)[0]
    e_phnum = struct.unpack_from("<H", SO, 0x2c)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p = struct.unpack_from("<IIIIIIII", SO, off)
        if p[0] == 1:  # PT_LOAD
            segs.append((p[2], p[1], p[4], p[5]))
    e_shoff = struct.unpack_from("<I", SO, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", SO, 0x2e)[0]
    e_shnum = struct.unpack_from("<H", SO, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", SO, 0x32)[0]
    sections = [struct.unpack_from("<IIIIIIIIII", SO, e_shoff + i*e_shentsize) for i in range(e_shnum)]
    rel_dyn = rel_plt = None
    dynsym = dynstr = None
    for s in sections:
        if s[1] == 11: dynsym = s          # SHT_DYNSYM
        elif s[1] == 9: rel_dyn = s         # SHT_REL
        elif s[1] == 4: rel_plt = s         # SHT_RELA (or plt rel)
    # strtab = sections[dynsym[6]]
    dynstr = sections[dynsym[6]]
    dynstr_data = SO[dynstr[4]:dynstr[4]+dynstr[5]]
    syms = []
    entsize = dynsym[9] or 16
    for i in range(dynsym[5]//entsize):
        off = dynsym[4] + i*entsize
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", SO, off)
        end = dynstr_data.find(b'\x00', st_name)
        nm = dynstr_data[st_name:end].decode('latin-1')
        syms.append({'name': nm, 'value': st_value, 'shndx': st_shndx})
    return segs, rel_dyn, rel_plt, syms

def setup_unicorn():
    segs, rel_dyn, rel_plt, syms = parse_elf()
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    # Map 256MB at 0 for the .so
    mu.mem_map(0, 0x10000000, UC_PROT_ALL)
    for vaddr, offset, filesz, memsz in segs:
        mu.mem_write(vaddr, SO[offset:offset+filesz])
    # Apply R_ARM_RELATIVE (23) and R_ARM_ABS32 (2) relocations
    reloc_count = 0
    if rel_dyn:
        for i in range(rel_dyn[5]//8):
            off = rel_dyn[4] + i*8
            r_offset, r_info = struct.unpack_from("<II", SO, off)
            r_type = r_info & 0xFF
            r_sym = r_info >> 8
            if r_type == 23:  # R_ARM_RELATIVE
                val = struct.unpack_from("<I", SO, r_offset)[0]
                mu.mem_write(r_offset, struct.pack("<I", val))
                reloc_count += 1
            elif r_type in (2, 21, 22) and r_sym < len(syms):
                sym = syms[r_sym]
                if sym['value']:
                    mu.mem_write(r_offset, struct.pack("<I", sym['value']))
                    reloc_count += 1
    # PLT relocations: point malloc/free to real addresses, stub others
    STUB_BASE = 0x70000000
    mu.mem_map(STUB_BASE, 0x10000, UC_PROT_ALL)
    stub_ptr = STUB_BASE
    STUB_CODE = b'\x00\x00\xa0\xe3\x1e\xff\x2f\xe1'  # mov r0,#0; bx lr
    if rel_plt:
        for i in range(rel_plt[5]//8):
            off = rel_plt[4] + i*8
            r_offset, r_info = struct.unpack_from("<II", SO, off)
            r_sym = r_info >> 8
            if r_sym < len(syms):
                sym = syms[r_sym]
                nm = sym['name'].lower()
                if 'malloc' in nm and 's3e' in nm:
                    mu.mem_write(r_offset, struct.pack("<I", ADDR_MALLOC))
                elif 'free' in nm and 's3e' in nm and 'free' in nm:
                    mu.mem_write(r_offset, struct.pack("<I", ADDR_FREE))
                elif 'realloc' in nm and 's3e' in nm:
                    mu.mem_write(r_offset, struct.pack("<I", ADDR_REALLOC))
                else:
                    mu.mem_write(stub_ptr, STUB_CODE)
                    mu.mem_write(r_offset, struct.pack("<I", stub_ptr))
                    stub_ptr += 16
    # Stack
    mu.mem_map(0x80000000, 0x100000, UC_PROT_ALL)
    SP = 0x80000000 + 0x100000 - 0x1000
    # Heap (bump allocator)
    mu.mem_map(0x90000000, 0x4000000, UC_PROT_ALL)
    # Return sentinel
    RET_ADDR = 0xDEAD0000
    mu.mem_map(RET_ADDR & ~0xFFF, 0x1000, UC_PROT_ALL)
    mu.mem_write(RET_ADDR, b'\xfe\xde\xff\xe7')  # b .
    # Auto-map unmapped memory
    def hook_mem(uc, access, address, size, value, user_data):
        page = address & ~0xFFF
        try:
            uc.mem_map(page, 0x1000, UC_PROT_ALL)
            return True
        except:
            return False
    mu.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED, hook_mem)
    # Heap bump allocator state
    state = {'heap_ptr': 0x90000000}
    def hook_code(uc, address, size, user_data):
        # Intercept calls to s3eMallocBase/s3eFreeBase/s3eReallocBase
        if address == ADDR_MALLOC:
            sz = uc.reg_read(UC_ARM_REG_R0)
            ptr = state['heap_ptr']
            state['heap_ptr'] += (sz + 15) & ~15
            uc.reg_write(UC_ARM_REG_R0, ptr)
            lr = uc.reg_read(UC_ARM_REG_LR)
            uc.reg_write(UC_ARM_REG_PC, lr)
        elif address == ADDR_FREE:
            # no-op
            uc.reg_write(UC_ARM_REG_R0, 0)
            lr = uc.reg_read(UC_ARM_REG_LR)
            uc.reg_write(UC_ARM_REG_PC, lr)
        elif address == ADDR_REALLOC:
            old = uc.reg_read(UC_ARM_REG_R0)
            sz = uc.reg_read(UC_ARM_REG_R1)
            newptr = state['heap_ptr']
            state['heap_ptr'] += (sz + 15) & ~15
            if old:
                try:
                    olddata = bytes(uc.mem_read(old, min(sz, 0x10000)))
                    uc.mem_write(newptr, olddata)
                except: pass
            uc.reg_write(UC_ARM_REG_R0, newptr)
            lr = uc.reg_read(UC_ARM_REG_LR)
            uc.reg_write(UC_ARM_REG_PC, lr)
    mu.hook_add(UC_HOOK_CODE, hook_code)
    # Run init_array constructors to register coders / global state
    init_array_base = 0xc0108
    ran = 0
    for i in range(16):
        val = struct.unpack_from("<I", SO, init_array_base + i * 4)[0]
        if val == 0:
            continue
        mu.reg_write(UC_ARM_REG_SP, SP)
        mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
        try:
            mu.emu_start(val, RET_ADDR, timeout=5*1000000, count=100000)
            ran += 1
        except UcError:
            pass
    # Check coder table state after init
    coder_table = bytes(mu.mem_read(CODER_TABLE, 0x88 * 4))
    # Print slot 0 fields
    return mu, SP, RET_ADDR, state

# ---------- DZ archive parsing ----------
# File table entry: 4 x u32 = (offset, comp_size, uncomp_size, type)
#   offset      = position in file where compressed block starts
#   comp_size   = compressed block size (offset difference between adjacent files)
#   uncomp_size = REAL uncompressed size (matches ground-truth file size on disk)
#   type        = 4 (DZ custom) for all entries in files.dz
def parse_dz(path):
    with open(path, 'rb') as f:
        data = f.read()
    num_files = struct.unpack_from('<H', data, 4)[0]
    num_dirs = struct.unpack_from('<H', data, 6)[0]
    pos = 9
    filenames = []
    for i in range(num_files):
        end = data.index(b'\x00', pos)
        filenames.append(data[pos:end].decode('utf-8','replace'))
        pos = end + 1
    for i in range(max(0, num_dirs-1)):
        end = data.index(b'\x00', pos)
        pos = end + 1
    pos += num_files * 6 + 4
    entries = []
    for i in range(num_files):
        off, comp_size, uncomp_size, ftype = struct.unpack_from('<IIII', data, pos)
        pos += 16
        entries.append({'name': filenames[i], 'offset': off,
                        'comp_size': comp_size, 'uncomp_size': uncomp_size, 'type': ftype})
    return data, entries

# ---------- Decompression attempt ----------
def _is_mapped(mu, addr):
    try:
        mu.mem_read(addr, 1)
        return True
    except:
        return False

def _ensure_map(mu, addr, size):
    if not _is_mapped(mu, addr):
        # round up to pages
        start = addr & ~0xFFF
        end = (addr + size + 0xFFF) & ~0xFFF
        try:
            mu.mem_map(start, end - start, UC_PROT_ALL)
        except:
            pass

def try_init(mu, SP, RET_ADDR, verbose=False):
    """Call s3eCompressionDecompInit(type=4) to set up the coder table."""
    mu.reg_write(UC_ARM_REG_SP, SP)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    mu.reg_write(UC_ARM_REG_R0, 4)  # type = 4 (DZ)
    try:
        mu.emu_start(ADDR_DECOMP_INIT, RET_ADDR, timeout=10*1000000, count=500000)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        if verbose:
            print(f"  Init(4) -> r0=0x{r0:x}")
        return r0
    except UcError as e:
        pc = mu.reg_read(UC_ARM_REG_PC)
        if verbose:
            print(f"  Init ERROR: {e} at PC=0x{pc:x}")
        return None

def try_decomp(mu, SP, RET_ADDR, dz_data, entry, verbose=False):
    """Decompress one file via s3eCompressionDecomp(input, output, in_size_ptr, out_size_ptr, type=4)."""
    name = entry['name']
    off = entry['offset']
    comp_size = entry['comp_size']
    uncomp_size = entry['uncomp_size']
    if verbose:
        print(f"  [{name}] offset=0x{off:x} comp={comp_size} uncomp={uncomp_size}")
    # Buffers
    INPUT_BUF = 0xA0000000
    OUTPUT_BUF = 0xB0000000
    INPUT_SIZE_ADDR = 0xC0000000
    OUTPUT_SIZE_ADDR = 0xC0000010
    _ensure_map(mu, INPUT_BUF, comp_size + 16)
    _ensure_map(mu, OUTPUT_BUF, uncomp_size + 16)
    _ensure_map(mu, INPUT_SIZE_ADDR, 0x1000)
    chunk = dz_data[off:off + comp_size]
    mu.mem_write(INPUT_BUF, chunk)
    mu.mem_write(OUTPUT_BUF, b'\x00' * (uncomp_size + 16))
    mu.mem_write(INPUT_SIZE_ADDR, struct.pack("<I", comp_size))
    mu.mem_write(OUTPUT_SIZE_ADDR, struct.pack("<I", uncomp_size))
    # Trace hooks: log Init return, Read args/return, and final state
    trace = {'init_ret': None, 'read_args': None, 'read_ret': None,
             'coder_init_called': False, 'coder_read_called': False}
    def trace_hook(uc, address, size, user_data):
        if address == 0x51414:  # Init entry
            pass
        elif address == 0x51c90 + 8:  # right after Init returns (0x51c94)
            trace['init_ret'] = uc.reg_read(UC_ARM_REG_R0)
        elif address == 0x51a10:  # Read entry
            trace['read_args'] = {
                'r0(ctx)': uc.reg_read(UC_ARM_REG_R0),
                'r1': uc.reg_read(UC_ARM_REG_R1),
                'r2': uc.reg_read(UC_ARM_REG_R2),
                'r3': uc.reg_read(UC_ARM_REG_R3),
            }
        elif address == 0x50be4:  # coder init
            trace['coder_init_called'] = True
        elif address == 0x389f8:  # actual decoder
            trace['coder_read_called'] = True
    mu.hook_add(UC_HOOK_CODE, trace_hook, begin=0x51414, end=0x51418)
    mu.hook_add(UC_HOOK_CODE, trace_hook, begin=0x51c94, end=0x51c98)
    mu.hook_add(UC_HOOK_CODE, trace_hook, begin=0x51a10, end=0x51a14)
    mu.hook_add(UC_HOOK_CODE, trace_hook, begin=0x50be4, end=0x50be8)
    mu.hook_add(UC_HOOK_CODE, trace_hook, begin=0x389f8, end=0x389fc)
    sp = SP - 16
    mu.mem_write(sp, struct.pack("<I", 4))
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    mu.reg_write(UC_ARM_REG_R0, INPUT_BUF)
    mu.reg_write(UC_ARM_REG_R1, OUTPUT_BUF)
    mu.reg_write(UC_ARM_REG_R2, INPUT_SIZE_ADDR)
    mu.reg_write(UC_ARM_REG_R3, OUTPUT_SIZE_ADDR)
    try:
        mu.emu_start(ADDR_DECOMP, RET_ADDR, timeout=60*1000000, count=20000000)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        out = bytes(mu.mem_read(OUTPUT_BUF, uncomp_size))
        nonzero = sum(1 for b in out if b != 0)
        in_consumed = struct.unpack("<I", mu.mem_read(INPUT_SIZE_ADDR, 4))[0]
        out_produced = struct.unpack("<I", mu.mem_read(OUTPUT_SIZE_ADDR, 4))[0]
        if verbose:
            print(f"    r0=0x{r0:x} nonzero={nonzero}/{uncomp_size} in_consumed={in_consumed} out_produced={out_produced}")
            print(f"    trace: init_ret={trace['init_ret']!r} coder_init={trace['coder_init_called']} coder_read={trace['coder_read_called']}")
            if trace['read_args']:
                print(f"    read_args: {trace['read_args']}")
        return r0, out, in_consumed, out_produced
    except UcError as e:
        pc = mu.reg_read(UC_ARM_REG_PC)
        if verbose:
            print(f"    ERROR: {e} at PC=0x{pc:x}")
            for i in range(8):
                print(f"      r{i}=0x{mu.reg_read(UC_ARM_REG_R0+i):x}")
        return None, None, 0, 0

def try_read(mu, SP, RET_ADDR, dz_data, entry, ctx, verbose=False):
    """Decompress via s3eCompressionDecompRead(ctx, input, in_size, output, out_size, &bytes_read)."""
    name = entry['name']
    off = entry['offset']
    comp_size = entry['comp_size']
    uncomp_size = entry['uncomp_size']
    if verbose:
        print(f"  [{name}] offset=0x{off:x} comp={comp_size} uncomp={uncomp_size} ctx=0x{ctx:x}")
    INPUT_BUF = 0xA0000000
    OUTPUT_BUF = 0xB0000000
    BYTES_READ_ADDR = 0xC0000020
    _ensure_map(mu, INPUT_BUF, comp_size + 16)
    _ensure_map(mu, OUTPUT_BUF, uncomp_size + 16)
    _ensure_map(mu, BYTES_READ_ADDR, 0x1000)
    chunk = dz_data[off:off + comp_size]
    mu.mem_write(INPUT_BUF, chunk)
    mu.mem_write(OUTPUT_BUF, b'\x00' * (uncomp_size + 16))
    mu.mem_write(BYTES_READ_ADDR, struct.pack("<I", 0))
    # s3eCompressionDecompRead(ctx, input, in_size, output, out_size, &bytes_read)
    # r0=ctx, r1=input, r2=in_size, r3=output, [sp]=out_size, [sp+4]=&bytes_read
    sp = SP - 16
    mu.mem_write(sp, struct.pack("<II", uncomp_size, BYTES_READ_ADDR))
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, RET_ADDR)
    mu.reg_write(UC_ARM_REG_R0, ctx)
    mu.reg_write(UC_ARM_REG_R1, INPUT_BUF)
    mu.reg_write(UC_ARM_REG_R2, comp_size)
    mu.reg_write(UC_ARM_REG_R3, OUTPUT_BUF)
    try:
        mu.emu_start(ADDR_DECOMP_READ, RET_ADDR, timeout=60*1000000, count=20000000)
        r0 = mu.reg_read(UC_ARM_REG_R0)
        out = bytes(mu.mem_read(OUTPUT_BUF, uncomp_size))
        nonzero = sum(1 for b in out if b != 0)
        bytes_read = struct.unpack("<I", mu.mem_read(BYTES_READ_ADDR, 4))[0]
        if verbose:
            print(f"    r0=0x{r0:x} nonzero={nonzero}/{uncomp_size} bytes_read={bytes_read}")
        return r0, out, bytes_read
    except UcError as e:
        pc = mu.reg_read(UC_ARM_REG_PC)
        if verbose:
            print(f"    ERROR: {e} at PC=0x{pc:x}")
        return None, None, 0

def main():
    print("=== DZ type-4 decoder via Unicorn ===")
    print("Loading libs3e_android.so into Unicorn + running init_array...")
    mu, SP, RET_ADDR, state = setup_unicorn()
    print(f"  Loaded. SP=0x{SP:x}, RET=0x{RET_ADDR:x}")
    # Inspect coder table after init_array
    print("\n=== Coder table @ 0xc8514 after init_array ===")
    for i in range(4):
        base = CODER_TABLE + i * 0x88
        slot = bytes(mu.mem_read(base, 0x88))
        # key fields: +0x20 (registered flag), +0x64 (init fn), +0x68 (read fn)
        flag = slot[0x20]
        init_fn = struct.unpack_from("<I", slot, 0x64)[0]
        read_fn = struct.unpack_from("<I", slot, 0x68)[0]
        print(f"  slot {i}: flag@0x20={flag} init@0x64=0x{init_fn:x} read@0x68=0x{read_fn:x}")
    print("Parsing files.dz...")
    dz_data, entries = parse_dz(DZ_PATH)
    print(f"  {len(entries)} files")
    target = None
    for e in entries:
        if e['name'] == 'forge.xml':
            target = e; break
    print(f"\nTarget: {target['name']} (offset=0x{target['offset']:x}, comp={target['comp_size']}, uncomp={target['uncomp_size']})")
    gt_path = f"{ASSETS_DIR}/forge.xml"
    with open(gt_path, 'rb') as f:
        gt = f.read()
    print(f"Ground truth: {gt_path} ({len(gt)} bytes)")

    # If coder table empty, set DZ coder pointers manually [ORIGINAL]
    # coder init = 0x50be4, coder read = 0x51f60 (from prior disasm trace)
    needs_manual = True
    for i in range(4):
        base = CODER_TABLE + i * 0x88
        slot = bytes(mu.mem_read(base, 0x88))
        if slot[0x20] != 0 or struct.unpack_from("<I", slot, 0x64)[0] != 0:
            needs_manual = False
    if needs_manual:
        print("\nCoder table empty — setting DZ coder pointers manually [ORIGINAL]")
        CODER_INIT = 0x50be4
        CODER_READ = 0x51f60
        # Set ALL 4 slots (Init picks one based on counter; safest to fill all)
        for i in range(4):
            base = CODER_TABLE + i * 0x88
            mu.mem_write(base + 0x20, b'\x01')  # registered flag
            mu.mem_write(base + 0x64, struct.pack("<I", CODER_INIT))
            mu.mem_write(base + 0x68, struct.pack("<I", CODER_READ))
        # Counter at 0xc8510 + 0x244 = 0xc8754 (Init reads [ip+0x244] at 0x51438)
        mu.mem_write(0xc8754, struct.pack("<I", 0))
        # Verify
        s0 = bytes(mu.mem_read(CODER_TABLE, 0x88))
        print(f"  slot 0 after setup: flag={s0[0x20]} init=0x{struct.unpack_from('<I',s0,0x64)[0]:x} read=0x{struct.unpack_from('<I',s0,0x68)[0]:x}")

    print("\n--- Calling s3eCompressionDecomp directly ---")
    r0, out, inc, outp = try_decomp(mu, SP, RET_ADDR, dz_data, target, verbose=True)
    if out and out == gt:
        print("*** MATCH via s3eCompressionDecomp! ***")
        return
    if out:
        match_len = 0
        for i in range(min(len(out), len(gt))):
            if out[i] != gt[i]: break
            match_len += 1
        print(f"\nMISMATCH: {match_len}/{len(gt)} bytes match")
        if match_len > 0:
            print(f"  decoded[match-8:match+24]: {out[max(0,match_len-8):match_len+24].hex()}")
            print(f"  ground[match-8:match+24]:  {gt[max(0,match_len-8):match_len+24].hex()}")
        print(f"  decoded first 120: {out[:120]!r}")
        print(f"  ground first 120:  {gt[:120]!r}")

if __name__ == "__main__":
    main()
