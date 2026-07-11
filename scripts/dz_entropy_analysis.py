#!/usr/bin/env python3
"""DZ archive entropy analysis and block inspection.

Per the user's instructions (Task 3 methodology):
  1. Extract several small compressed blocks from a .dz archive
  2. Compute Shannon entropy (8 bits/byte = real compression; lower = XOR/obfuscation)
  3. Inspect the first bytes of multiple blocks in hex
  4. Look for patterns (uncompressed size fields, control bytes like 0x1D)

This script extends the existing parse_dz.py — it reuses the container parser
and adds entropy analysis on the compressed payloads.
"""
import math
import os
import struct
import sys
from collections import Counter
from pathlib import Path

# Reuse the existing parser
sys.path.insert(0, str(Path(__file__).parent))
from parse_dz import parse_dz


def shannon_entropy(data: bytes) -> float:
    """Compute Shannon entropy in bits/byte (0..8)."""
    if not data:
        return 0.0
    counts = Counter(data)
    total = len(data)
    entropy = 0.0
    for count in counts.values():
        p = count / total
        if p > 0:
            entropy -= p * math.log2(p)
    return entropy


def hexdump(data: bytes, length: int = 32) -> str:
    """Return a hex dump of the first `length` bytes."""
    return ' '.join(f'{b:02x}' for b in data[:length])


def analyze_block(name: str, payload: bytes, uncomp_size: int, comp_type: int):
    """Analyze a single DZ compressed block."""
    print(f"\n--- {name} ---")
    print(f"  comp_size={len(payload)}, uncomp_size={uncomp_size}, type={comp_type}")
    print(f"  ratio={len(payload)/uncomp_size:.3f}" if uncomp_size > 0 else "  ratio=N/A")

    ent = shannon_entropy(payload)
    print(f"  entropy={ent:.4f} bits/byte ({'HIGH — real compression' if ent > 7.5 else 'MEDIUM' if ent > 6 else 'LOW — possibly XOR/obfuscation'})")

    print(f"  first 32 bytes: {hexdump(payload, 32)}")
    print(f"  last 16 bytes:  {hexdump(payload[-16:], 16)}")

    # Check if first byte is always 0x1D (as noted in docs)
    if payload:
        print(f"  first byte: 0x{payload[0]:02x} ({'matches 0x1D' if payload[0] == 0x1D else 'does NOT match 0x1D'})")

    # Look for potential uncompressed-size field in first 4-8 bytes
    if len(payload) >= 8:
        for offset in range(0, 8, 2):
            val32 = struct.unpack_from('<I', payload, offset)[0]
            val16 = struct.unpack_from('<H', payload, offset)[0]
            if val32 == uncomp_size:
                print(f"  FOUND uncomp_size at offset {offset} (u32 LE)")
            if val16 == uncomp_size & 0xFFFF:
                print(f"  FOUND uncomp_size low 16 bits at offset {offset} (u16 LE)")

    # Byte frequency analysis — if XOR with a fixed key, some bytes will dominate
    if len(payload) > 100:
        counts = Counter(payload)
        most_common = counts.most_common(5)
        total = len(payload)
        print(f"  top 5 bytes: {[(f'0x{b:02x}', f'{c} ({100*c/total:.1f}%)') for b, c in most_common]}")

    return ent


def main():
    dz_path = sys.argv[1] if len(sys.argv) > 1 else \
        "/home/z/my-project/work/sf2_data/sf2/assets/assets/files.dz"

    if not os.path.exists(dz_path):
        print(f"ERROR: {dz_path} not found")
        return 1

    print(f"=== DZ Entropy Analysis: {dz_path} ===\n")
    data, data_start, file_table, _ = parse_dz(dz_path)

    # Analyze all type=4 (DZ) blocks, sorted by comp_size (smallest first)
    dz_blocks = [ft for ft in file_table if ft['type'] == 4]
    dz_blocks.sort(key=lambda ft: ft['comp_size'])

    print(f"Total files: {len(file_table)}")
    print(f"DZ-compressed (type=4): {len(dz_blocks)}")
    print(f"Other types: {Counter(ft['type'] for ft in file_table if ft['type'] != 4)}")

    # Analyze the 10 smallest DZ blocks (easier to spot patterns)
    print(f"\n=== Analyzing 10 smallest DZ blocks ===")
    entropies = []
    for ft in dz_blocks[:10]:
        start = ft['offset']
        end = start + ft['comp_size']
        payload = data[start:end]
        ent = analyze_block(ft['name'], payload, ft['uncomp_size'], ft['type'])
        entropies.append(ent)

    # Summary
    print(f"\n=== SUMMARY ===")
    print(f"Entropy range: {min(entropies):.4f} .. {max(entropies):.4f}")
    print(f"Average entropy: {sum(entropies)/len(entropies):.4f}")

    # Check first-byte distribution across ALL DZ blocks
    first_bytes = Counter()
    for ft in dz_blocks:
        start = ft['offset']
        if start < len(data):
            first_bytes[data[start]] += 1
    print(f"\nFirst byte distribution across {len(dz_blocks)} DZ blocks:")
    for byte, count in first_bytes.most_common(10):
        print(f"  0x{byte:02x}: {count} ({100*count/len(dz_blocks):.1f}%)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
