#!/usr/bin/env python3
"""
DZ type-4 decompression using dzip.exe (official Marmalade tool).
Falls back to ARM emulation if dzip.exe is not available.
"""
import struct
import subprocess
import os
import sys
import tempfile

DZIP_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 
                         "tools", "dzip.exe")

def decompress_with_dzip(compressed_data, output_size_hint=102953):
    """
    Decompress DZ type-4 data using dzip.exe.
    
    dzip.exe works on .dz archive files, not on individual compressed blocks.
    We need to wrap the data in a minimal DZ container.
    """
    if not os.path.exists(DZIP_PATH):
        raise FileNotFoundError(f"dzip.exe not found at {DZIP_PATH}")
    
    # Create a minimal DZ container
    # Format: DTRZ header + filenames + file entries + compressed data
    with tempfile.TemporaryDirectory() as tmpdir:
        dz_path = os.path.join(tmpdir, "temp.dz")
        outdir = os.path.join(tmpdir, "out")
        os.makedirs(outdir, exist_ok=True)
        
        with open(dz_path, "wb") as f:
            # DTRZ header
            f.write(b"DTRZ")
            # file count = 1, dir count = 1 (root)
            f.write(struct.pack("<HH", 1, 1))
            f.write(b"\x00")  # version
            
            # filename
            f.write(b"decoded.bin\x00")
            
            # dir name (root)
            f.write(b"\x00")
            
            # file attributes
            f.write(struct.pack("<HHH", 0, 0, 0))  # dir=0, number=0, flags=0
            
            # lengths header
            f.write(b"\x01\x00")  # unknown, always 1?
            f.write(struct.pack("<H", 1))  # count = 1
            
            # file offset entry
            f.write(struct.pack("<IIII", 0, len(compressed_data), len(compressed_data), 8))
            
            # compressed data
            f.write(compressed_data)
        
        # Decompress
        result = subprocess.run(
            [DZIP_PATH, "-d", dz_path, "-D", outdir],
            capture_output=True, timeout=30
        )
        
        if result.returncode != 0:
            print(f"dzip.exe error: {result.stderr.decode(errors='replace')}")
            return None
        
        # Find output file
        out_file = os.path.join(outdir, "decoded.bin")
        if os.path.exists(out_file):
            with open(out_file, "rb") as f:
                return f.read()
        
        # Try alternate location
        for root, dirs, files in os.walk(tmpdir):
            for fn in files:
                if fn == "decoded.bin":
                    with open(os.path.join(root, fn), "rb") as f:
                        return f.read()
        
        print(f"dzip.exe output not found")
        return None


def decompress_dz_block(block_data, is_type4=True):
    """Decompress a single DZ type-4 compressed block."""
    if not is_type4:
        raise ValueError(f"Unsupported compression type")
    
    try:
        return decompress_with_dzip(block_data)
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        print(f"dzip.exe failed: {e}")
        return None


if __name__ == "__main__":
    # Test with forge.xml from files.dz
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dz_path = os.path.join(repo, "assets", "files.dz")
    gt_path = os.path.join(repo, "assets", "forge.xml")
    
    with open(dz_path, "rb") as f:
        dz = f.read()
    
    # Parse DZ
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
    f0, f1, f2, f3 = struct.unpack_from('<IIII', dz, pos + idx * 16)
    print(f"forge.xml: off={f0} comp={f1} uncomp={f2} type={f3}")
    
    comp = dz[f0:f0+f1]
    result = decompress_dz_block(comp)
    
    if result:
        with open(gt_path, "rb") as f:
            gt = f.read()
        print(f"Decompressed: {len(result)} bytes (expected {len(gt)})")
        if result == gt:
            print("*** PERFECT MATCH! ***")
        else:
            for i in range(min(len(result), len(gt))):
                if result[i] != gt[i]:
                    print(f"First difference at byte {i}")
                    break
