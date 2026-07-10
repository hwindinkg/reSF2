#!/usr/bin/env python3
"""
DZ archive decompressor using Unicorn ARM emulation.

Loads libs3e_android.so into Unicorn, finds the DZ decompression functions,
and emulates them to decompress files from .dz archives.

This allows reSF2 to load game files natively from files.dz and animations.dz
without requiring the user to extract them first.
"""
import struct
import unicorn
from unicorn.arm_const import *
import subprocess
import sys
import os

SO_PATH = "/home/z/my-project/work/apk_extracted/apktool/lib/armeabi-v7a/libs3e_android.so"
DZ_PATH = "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"

def load_so(emu):
    """Load the .so into Unicorn memory and apply relocations."""
    with open(SO_PATH, "rb") as f:
        so_data = bytearray(f.read())
    
    PAGE = 0x1000
    mapped = ((len(so_data) + PAGE - 1) // PAGE) * PAGE
    emu.mem_map(0, mapped, unicorn.UC_PROT_ALL)
    emu.mem_write(0, bytes(so_data))
    
    # Apply R_ARM_RELATIVE relocations
    e_shoff = struct.unpack_from('<I', so_data, 0x20)[0]
    e_shentsize = struct.unpack_from('<H', so_data, 0x2e)[0]
    e_shnum = struct.unpack_from('<H', so_data, 0x30)[0]
    e_shstrndx = struct.unpack_from('<H', so_data, 0x32)[0]
    
    shstrtab_off = struct.unpack_from('<I', so_data, e_shoff + e_shstrndx * e_shentsize + 0x18)[0]
    
    reloc_count = 0
    for i in range(e_shnum):
        sh_off = e_shoff + i * e_shentsize
        sh_type = struct.unpack_from('<I', so_data, sh_off + 4)[0]
        sh_offset = struct.unpack_from('<I', so_data, sh_off + 0x18)[0]
        sh_size = struct.unpack_from('<I', so_data, sh_off + 0x20)[0]
        
        if sh_type == 9:  # SHT_REL
            n_relocs = sh_size // 8
            for j in range(n_relocs):
                r_off = sh_offset + j * 8
                r_offset, r_info = struct.unpack_from('<II', so_data, r_off)
                r_type = r_info & 0xff
                if r_type == 23:  # R_ARM_RELATIVE
                    if r_offset < len(so_data):
                        val = struct.unpack_from('<I', so_data, r_offset)[0]
                        struct.pack_into('<I', so_data, r_offset, val)
                        emu.mem_write(r_offset, struct.pack('<I', val))
                        reloc_count += 1
    
    print(f"  Loaded .so ({len(so_data)} bytes, {reloc_count} relocations)")
    return so_data

def find_symbol(name):
    """Find a symbol address in the .so using nm."""
    result = subprocess.run(['nm', '-D', SO_PATH], capture_output=True, text=True)
    for line in result.stdout.split('\n'):
        parts = line.strip().split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    return None

def setup_unicorn():
    """Create and configure a Unicorn emulator."""
    emu = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_ARM)
    
    # Load .so
    load_so(emu)
    
    # Stack
    STACK_BASE = 0x10000000
    emu.mem_map(STACK_BASE, 0x100000, unicorn.UC_PROT_ALL)
    emu.reg_write(UC_ARM_REG_SP, STACK_BASE + 0xFF000)
    
    # Heap
    HEAP_BASE = 0x20000000
    emu.mem_map(HEAP_BASE, 0x1000000, unicorn.UC_PROT_ALL)
    
    # I/O buffers
    IO_BASE = 0x30000000
    emu.mem_map(IO_BASE, 0x1000000, unicorn.UC_PROT_ALL)
    
    return emu, HEAP_BASE, IO_BASE

# Global state for hooks
heap_ptr = 0x20000000
malloc_addr = None
free_addr = None

def create_hooks(emu, heap_base):
    """Set up code hooks for malloc/free interception."""
    global heap_ptr, malloc_addr, free_addr
    heap_ptr = heap_base
    
    malloc_addr = find_symbol('s3eMallocBase')
    free_addr = find_symbol('s3eFreeBase')
    print(f"  malloc=0x{malloc_addr:x}, free=0x{free_addr:x}" if malloc_addr else "  malloc NOT FOUND")
    
    RETURN_MAGIC = 0xDEADDEAD
    
    def hook_code(uc, address, size, user_data):
        global heap_ptr
        if address == RETURN_MAGIC:
            uc.emu_stop()
            return
        
        # Check for BL/BLX instructions
        if size >= 4:
            code = struct.unpack('<I', bytes(uc.mem_read(address, 4)))[0]
            
            # BLX rN: 0x47XX where bit 7=1 → BLX
            if (code & 0xFF000000) == 0xFA000000:  # BLX rN (ARM encoding)
                reg = (insn >> 12) & 0xF
                target = uc.reg_read(UC_ARM_REG_R0 + reg)
                if malloc_addr and target == malloc_addr:
                    sz = uc.reg_read(UC_ARM_REG_R0)
                    ptr = heap_ptr
                    heap_ptr += (sz + 15) & ~15
                    uc.reg_write(UC_ARM_REG_R0, ptr)
                    lr = uc.reg_read(UC_ARM_REG_LR)
                    uc.reg_write(UC_ARM_REG_PC, lr)
                    return
            
            # BL: 0xEBXXXXXX
            if (code & 0xFF000000) == 0xEB000000:
                offset = code & 0x00FFFFFF
                if offset & 0x800000:
                    offset |= 0xFF000000
                target = address + 8 + (offset << 2)
                if malloc_addr and target == malloc_addr:
                    sz = uc.reg_read(UC_ARM_REG_R0)
                    ptr = heap_ptr
                    heap_ptr += (sz + 15) & ~15
                    uc.reg_write(UC_ARM_REG_R0, ptr)
                    lr = uc.reg_read(UC_ARM_REG_LR)
                    uc.reg_write(UC_ARM_REG_PC, lr)
                    return
    
    emu.hook_add(unicorn.UC_HOOK_CODE, hook_code)
    return RETURN_MAGIC

def parse_dz_header(data):
    """Parse DTRZ archive header and return file table."""
    pos = 9  # Skip magic(4) + fileCount(2) + folderCount(2) + unused(1)
    file_count = struct.unpack_from('<H', data, 4)[0]
    folder_count = struct.unpack_from('<H', data, 6)[0] - 1
    
    # Read filenames
    filenames = []
    for i in range(file_count):
        end = data.index(b'\x00', pos)
        filenames.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1
    
    # Read folder names
    folders = [""]
    for i in range(folder_count):
        end = data.index(b'\x00', pos)
        folders.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1
    
    # Skip file attributes (6 bytes per file)
    pos += file_count * 6
    
    # Skip lengths header (2 + 2 bytes)
    pos += 4
    
    # Read file entries: offset(u32) + uncomp_size(u32) + comp_size(u32) + type(u32)
    files = []
    for i in range(file_count):
        offset, uncomp, comp, ftype = struct.unpack_from('<IIII', data, pos)
        files.append({
            'name': filenames[i],
            'folder': folders[0],  # simplified
            'offset': offset,
            'uncomp_size': uncomp,
            'comp_size': comp,
            'type': ftype,
        })
        pos += 16
    
    return files

def try_decompress(emu, compressed, uncomp_size, io_base, return_magic):
    """Try to decompress data using the emulated DZ decompressor."""
    # Write compressed data to IO buffer
    emu.mem_write(io_base, compressed)
    
    # Call s3eCompressionDecompInit(type=4)
    INIT_ADDR = 0x51414
    emu.reg_write(UC_ARM_REG_R0, 4)  # type = DZ
    emu.reg_write(UC_ARM_REG_LR, return_magic)
    
    try:
        emu.emu_start(INIT_ADDR, return_magic, timeout=5000000, count=50000)
        ctx = emu.reg_read(UC_ARM_REG_R0)
        print(f"  Init returned context: 0x{ctx:x}")
        
        if ctx == 0:
            print("  ERROR: Init returned NULL")
            return None
        
        # Call s3eCompressionDecompRead
        # Signature: s3eCompressionDecompRead(ctx, input, input_size, output, output_size, bytes_read)
        READ_ADDR = 0x51a10
        OUTPUT_ADDR = io_base + 0x100000  # output buffer after input
        
        emu.reg_write(UC_ARM_REG_R0, ctx)        # context
        emu.reg_write(UC_ARM_REG_R1, io_base)    # input
        emu.reg_write(UC_ARM_REG_R2, len(compressed))  # input_size
        emu.reg_write(UC_ARM_REG_R3, OUTPUT_ADDR)  # output
        
        # Push output_size and bytes_read onto stack (args 5,6)
        sp = emu.reg_read(UC_ARM_REG_SP)
        sp -= 8
        emu.mem_write(sp, struct.pack('<II', uncomp_size, 0))  # output_size, bytes_read_ptr
        emu.reg_write(UC_ARM_REG_SP, sp)
        
        emu.reg_write(UC_ARM_REG_LR, return_magic)
        
        emu.emu_start(READ_ADDR, return_magic, timeout=10000000, count=500000)
        
        # Read result
        result = emu.reg_read(UC_ARM_REG_R0)
        print(f"  Read returned: {result}")
        
        # Read output
        output = bytes(emu.mem_read(OUTPUT_ADDR, uncomp_size))
        return output
        
    except Exception as e:
        pc = emu.reg_read(UC_ARM_REG_PC)
        print(f"  Emulation error: {e} at PC=0x{pc:x}")
        return None

def main():
    print("=== DZ Archive Decompressor (Unicorn ARM Emulation) ===")
    
    # Load DZ archive
    with open(DZ_PATH, "rb") as f:
        dz_data = f.read()
    
    files = parse_dz_header(dz_data)
    print(f"  Found {len(files)} files in DZ archive")
    print(f"  First 5 files:")
    for i in range(min(5, len(files))):
        f = files[i]
        print(f"    {f['name']}: offset={f['offset']}, comp={f['comp_size']}, uncomp={f['uncomp_size']}, type={f['type']}")
    
    # Set up Unicorn
    print("\nSetting up Unicorn emulator...")
    emu, heap_base, io_base = setup_unicorn()
    return_magic = create_hooks(emu, heap_base)
    
    # Try to decompress first file
    print(f"\nDecompressing first file: {files[0]['name']}")
    compressed = dz_data[files[0]['offset']:files[0]['offset'] + files[0]['comp_size']]
    print(f"  Compressed: {len(compressed)} bytes, first 8: {compressed[:8].hex()}")
    
    output = try_decompress(emu, compressed, files[0]['uncomp_size'], io_base, return_magic)
    
    if output:
        print(f"  Decompressed: {len(output)} bytes")
        print(f"  Content preview: {output[:200]}")
        
        # Save to file
        out_path = "/tmp/dz_test_output.xml"
        with open(out_path, "wb") as f:
            f.write(output)
        print(f"  Saved to: {out_path}")
    else:
        print("  Decompression failed!")

if __name__ == "__main__":
    main()
