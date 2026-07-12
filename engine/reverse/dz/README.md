# DZ (Marmalade derbh) decompression — RE notes

## Status: Container format fully decoded. Compression algorithm identified (arithmetic/range coding). Decompressor implementation blocked on ARM emulation setup.

## Corrected container format (this session)

The previous parser (parse_dz.py) had the file table field order wrong. The correct format, verified by dumping raw bytes and checking offset/size consistency:

### DTRZ header
- 4 bytes: `DTRZ` magic
- u16 LE: num_files
- u16 LE: num_dirs
- u8: version (0)

### Names section
- num_files null-terminated UTF-8 strings (file names, in order)
- num_dirs null-terminated UTF-8 strings (directory names)

### File attribute table
- num_files × 6 bytes (purpose unknown — possibly folder index + flags + CRC)

### Lengths header
- 4 bytes (two u16 LE values — possibly total comp/uncomp sizes or block counts)

### File table (num_files × 16 bytes)
Each 16-byte entry is 4 fields of (u24 LE value + u8):

| Field | Bytes | u24 value    | u8 byte     |
|-------|-------|--------------|-------------|
| 0     | 0-3   | uncomp_size  | CRC         |
| 1     | 4-7   | data_offset  | CRC         |
| 2     | 8-11  | comp_size    | **type**    |
| 3     | 12-15 | reserved (0) | CRC         |

**Type values observed:**
- `4` — all 120 files in files.dz use this type
- `8` — all 557 files in animations.dz use this type
- Both are DZ compression variants (the algorithm is the same; the type byte may indicate a parameter/variant)

### Data section
Starts immediately after the file table. All offsets in the file table are relative to the start of this section.

**Key finding: DZ is a STREAMING compressor.** File offsets overlap:
- files_list.xml: off=3, comp=23, uncomp=25
- settings.xml: off=3, comp=23, uncomp=28 (same offset/comp, different uncomp!)
- localization.xml: off=4, comp=27, uncomp=348

This means the entire data section is one continuous compressed stream. Each file's "offset" is the position in the stream where that file's decoding STARTS, and the decompressor state carries over between files. To decompress file N, you must first decode all previous files 0..N-1.

## Entropy analysis (this session)

Shannon entropy of DZ-compressed blocks:
- Small files (comp < 50 bytes): 4.2–4.7 bits/byte
- Medium files (comp 100–500 bytes): 6.8–7.6 bits/byte
- Large files (comp > 500 bytes): 7.5–7.9 bits/byte

**Conclusion:** High entropy for larger blocks confirms this is real compression (arithmetic/range coding as documented), NOT XOR obfuscation. The low entropy for tiny files is expected — arithmetic coding has fixed overhead for initialization.

## The DZ algorithm (from ARM disassembly)

- Located in `libs3e_android.so`:
  - Read handler: function 0x51f60
  - Actual decode step: function 0x389f8 (called from read handler)
- Uses arithmetic/range coding with:
  - 5-byte context window (last 5 decoded bytes)
  - 32-bit hash from window: `window[1]<<24 | window[2]<<16 | window[3]<<8 | window[4]`
  - CRC32 table (poly 0x04C11DB7, big-endian) for context hashing
  - Probability model with reference tables (RefOffsetTables, RefLengthTables)
  - LZ77-style match references

### Function pointer table
.data section at 0xc3000 contains compression coder function pointers:
- type=1 → 0x000b3358 (Copy coder)
- type=2 → 0x000b3368 (ZLib coder)
- type=3 → 0x000b3370 (BZip coder)
- type=4 → 0x000b3378 (DZ coder)
- type=5 → 0x000b3380 (LZMA coder)

(Type 8 observed in animations.dz is not in this table — it may be a variant of type 4, or a different coder pointer that's set up at runtime.)

## ARM emulation via Unicorn — blocked

- libs3e_android.so loads correctly into Unicorn ARM emulator
- ELF relocations applied (2095 relocations)
- init_array constructors: 8 functions at 0xd830-0xdac8
  - 5 succeed, 3 fail (need PLT stubs for __cxa_atexit etc.)
- s3eCompressionDecompInit returns a pointer (0xc8592) instead of type index (1-4)
  - This is because the init_array constructors don't fully set up the
    function pointer table at 0xc8578
- Full DZ decompression via ARM emulation requires a complete Marmalade
  runtime environment (thread state, allocator pool, config system).

## Recommended paths forward

1. **Fallback directory approach** (IMPLEMENTED in dz_reader.cpp):
   - The DZ registry now supports `add_fallback_dir()` to register
     directories containing pre-extracted files.
   - When a file can't be decompressed from .dz (e.g. type=4 DZ custom),
     the registry searches fallback directories for the file.
   - Search paths tried (in order):
     - `<dir>/<name>`
     - `<dir>/files/<name>` (files.dz extracted contents)
     - `<dir>/animations/<name>` (animations.dz XML files)
     - `<dir>/animations/binary/<name>` (.bin animation files)
     - `<dir>/files/assets/<name>` (deeper nesting)
     - `<dir>/assets/files/<name>`
     - `<dir>/assets/animations/<name>`
     - `<dir>/assets/animations/binary/<name>`
   - This allows the engine to work with pre-extracted assets while
     still supporting .dz archives for files that can be decompressed
     (gzip type=8).
   - For type=4 DZ custom compression, users should pre-extract files.dz
     using `dzip.exe -d files.dz` on Windows.

2. **Manual port of DZ decoder** (for in-engine .dz support):
   - Port the ~250 ARM instructions at 0x389f8 to C++/Python
   - The algorithm is documented above (arithmetic coding + 5-byte context + CRC32 hash + LZ77 matches)
   - This is clean-room (re-implementing from algorithm description, not copying code)
   - The streaming nature (overlapping offsets) must be handled: decompress the entire archive as one stream
   - Helper functions to port:
     - 0x3751c: bit/model decode (range coder with context lookup)
     - 0x37adc: range coder decode (the main decode loop)
     - 0xc94c: memcpy helper

3. **Alternative: Ghidra analysis** — load libs3e_android.so into Ghidra with the S3ELoader plugin, decompile the DZ decode function at 0x389f8, and port the decompiled C to Python/C++.

## Files
- `dz_parse_correct.py` (in scripts/) — corrected container parser with entropy analysis
- `dz_dump_format.py` (in scripts/) — raw hex dump of DTRZ header for format verification
- `dz_entropy_analysis.py` (in scripts/) — Shannon entropy analysis of DZ blocks
- `parse_dz.py` (in scripts/) — old parser with incorrect field order (kept for reference)
- `dz_final.py` (in scripts/) — Unicorn ARM emulation attempt (blocked on init_array)
- `dz_decode_v2.py` (in scripts/) — manual port attempt (incomplete, algorithm not fully correct)
