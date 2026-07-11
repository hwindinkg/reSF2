# DZ (Marmalade derbh) decompression — RE notes

## Status: 90% complete (blocked on init_array constructor setup)

## What we know

### The .dz file format (DTRZ container)
- Header: `DTRZ` + u16 num_files + u16 num_dirs + u8 version
- Names list: null-terminated strings (files + dirs)
- Dir assignment table: 224 x 6 bytes (name_idx + sentinel + parent_dir)
- File table: 120 x 16 bytes (4 x u24 LE value + u8 type/CRC byte)
  - field 0: u24=0, u8=CRC
  - field 1: u24=offset, u8=CRC
  - field 2: u24=comp_size, u8=CRC
  - field 3: u24=uncomp_size, u8=0x04 (DZ compression type)
- Data section: DZ-compressed file payloads

### The DZ algorithm
- Located in `libs3e_android.so` at function 0x51f60 (read handler)
- Actual decode step at 0x389f8 (called from read handler)
- Uses arithmetic/range coding with:
  - 5-byte context window (last 5 decoded bytes)
  - 32-bit hash from window: `window[1]<<24 | window[2]<<16 | window[3]<<8 | window[4]`
  - CRC32 table (poly 0x04C11DB7, big-endian) for context hashing
  - Probability model with reference tables (RefOffsetTables, RefLengthTables)

### Function pointer table
- .data section at 0xc3000 contains compression coder function pointers:
  - type=1 → 0x000b3358 (Copy coder?)
  - type=2 → 0x000b3368 (ZLib coder)
  - type=3 → 0x000b3370 (BZip coder)
  - type=4 → 0x000b3378 (DZ coder)
  - type=5 → 0x000b3380 (LZMA coder)

### ARM emulation via Unicorn
- libs3e_android.so loads correctly into Unicorn ARM emulator
- ELF relocations applied (2095 relocations)
- init_array constructors: 8 functions at 0xd830-0xdac8
  - 5 succeed, 3 fail (need PLT stubs for __cxa_atexit etc.)
- s3eCompressionDecompInit returns a pointer (0xc8592) instead of type index (1-4)
  - This is because the init_array constructors don't fully set up the
    function pointer table at 0xc8578
  - The DZ coder init function pointer (0x000b3378) was manually set
  - But the full init path still fails

### Next steps to unblock
1. **Fix init_array constructors**: The 3 failing constructors (0xd830, 0xd86c, 0xd8d8)
   need PLT stubs for __cxa_atexit and potentially other C++ runtime functions.
   Once they run, the function pointer table should be properly initialized.

2. **Alternative: call DZ coder directly**: Instead of going through the s3eCompression
   dispatch layer, call the DZ coder init/read functions at 0x000b3378 directly
   with a properly set up context struct.

3. **Alternative: manual port**: Port the DZ arithmetic decoder (at 0x389f8, ~250
   ARM instructions) to Python/C++ by following the disassembly. The algorithm
   uses:
   - Range coding with 32-bit range
   - 5-byte context window for probability modeling
   - CRC32-derived hash for context table lookup
   - LZ77-style match references

## Files
- `dz_arm_emu.py` — First attempt at Unicorn ARM emulation (basic)
- `dz_arm_emu2.py` — With malloc stubs + crash debugging
- `dz_arm_emu3.py` — With auto-mapping for unmapped memory
- `dz_arm_emu4.py` — With ELF relocation processing
- `dz_arm_emu5.py` (to create) — With fixed init_array + direct DZ coder call

## Windows extraction workaround

Since the ARM emulation approach is blocked on the function pointer
table initialization, a simpler workaround is available:

1. Download `dzip.exe` and `extract_dz.bat` from the reSF2 download folder
2. Place both files in the same directory on your Windows PC
3. Run: `extract_dz.bat "C:\path\to\extracted\apk\assets"`
4. This creates subdirectories with the extracted XML and .bin files
5. Copy these back to your reSF2 assets directory

The extracted files can then be loaded directly by reSF2's AssetManager
without needing .dz support at all.

## Latest findings (session 5)

### Root cause identified
1. `s3eCompressionDecompInit` at 0x51414 OVERWRITES the function pointer
   at [context+0x64] with the context base address (0xc8514) at
   instruction 0x5152c: `str r1, [sb, #0x24]` where sb = context + 0x40.
   This clobbers any pre-set function pointer.

2. After the overwrite, at 0x518c0: `ldr r3, [r1, #0x64]` reads back
   0xc8514 (the context address itself, not a function pointer).
   Then `blx r3` branches to 0xc8514 which is in BSS — crash.

3. The DZ coder init function at 0x50be4 needs:
   - A proper global allocator (GOT[0xc20d8] = "t_Int32Ret_NoSS")
   - A properly initialized thread state
   - Multiple global structures

4. Even with the allocator fixed (GOT → s3eMallocBase), the coder
   init at 0x50be4 crashes because it needs more Marmalade runtime
   state that we haven't set up.

### Conclusion
Full DZ decompression via ARM emulation requires a complete Marmalade
runtime environment (thread state, allocator pool, config system).
This is beyond what we can reasonably set up in Unicorn.

### Recommended path
Use the Windows `dzip.exe` + `extract_dz.bat` workaround to extract
all .dz archives on a Windows PC. The extracted files can then be
loaded directly by reSF2's AssetManager without .dz support.
