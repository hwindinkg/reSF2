#!/usr/bin/env python
"""
correlate_runtime_dump.py

Correlates the live game-code dump (game_region_runtime.bin, captured from the
rwxs /dev/zero mapping) against the unpacked static S3E image
(ShadowFight2_android.bin).

Answers three things the project needs before reversing the main loop:
  1. What is the exact file<->runtime offset mapping? (derived, not assumed)
  2. Which bytes did the S3E loader change = the relocations / GOT fixups?
  3. Where does the runtime PLT live and what does it point to?

Run:  python correlate_runtime_dump.py
"""

import os
import struct
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, '..', 'binaries', 'ShadowFight2_android.bin')
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')

RUNTIME_CODE_START = 0x8F057000     # first byte of the dump
RUNTIME_IMAGE_BASE = 0x8F056000     # code start - fileOff(0x1000)

# S3E header fields (little endian) read straight from the file.
def read_header(data):
    magic = data[0:4]
    fields = struct.unpack_from('<11I', data, 4)
    return magic, fields


def find_alignment(dump, static):
    """Slide s such that dump[i] == static[i + s] for long stretches."""
    # Use several long probes from different parts of the dump.
    best = Counter()
    probe_len = 64
    for frac in (0.30, 0.40, 0.50, 0.60, 0.70, 0.80):
        off = int(len(dump) * frac) & ~3
        probe = dump[off:off + probe_len]
        if probe.count(b'\x00') > probe_len // 2:
            continue
        idx = static.find(probe)
        while idx >= 0:
            best[idx - off] += 1
            idx = static.find(probe, idx + 1)
    return best


def main():
    static = open(BIN, 'rb').read()
    dump = open(DUMP, 'rb').read()

    magic, f = read_header(static)
    print('=== S3E header (%s) ===' % magic.decode('latin1'))
    names = ['?', 'flags?', 'ver?', 'sig?', 'codeOffset?', 'dataOffset?',
             'a', 'b', 'c', 'd', 'e']
    for i, v in enumerate(f):
        print('  +0x%02X  0x%08X  %-12s %d' % (4 + i * 4, v, names[i] if i < len(names) else '', v))
    print()

    print('static image : %d bytes (%.2f MB)' % (len(static), len(static) / 1048576.0))
    print('runtime dump : %d bytes (%.2f MB)' % (len(dump), len(dump) / 1048576.0))
    print('dump covers  : 0x%08X - 0x%08X' % (RUNTIME_CODE_START,
                                              RUNTIME_CODE_START + len(dump)))
    print()

    slides = find_alignment(dump, static)
    print('=== candidate slides (dump_off + slide = file_off) ===')
    for slide, votes in slides.most_common(5):
        print('  slide 0x%X (%d)  votes=%d' % (slide & 0xFFFFFFFF, slide, votes))
    if not slides:
        print('  none found -- dump may be fully relocated/compressed')
        return
    slide = slides.most_common(1)[0][0]
    print('\nusing slide 0x%X' % (slide & 0xFFFFFFFF))
    print('  file_off  = dump_off + 0x%X' % (slide & 0xFFFFFFFF))
    print('  runtime   = dump_off + 0x%08X' % RUNTIME_CODE_START)
    print('  static    = file_off - 0x%X + 0x4A000000  (if notes are right)'
          % (slide & 0xFFFFFFFF))
    print()

    # Byte-level diff over the overlapping span.
    n = min(len(dump), len(static) - slide) if slide >= 0 else min(len(dump) + slide, len(static))
    diffs = []
    run_start = None
    same = 0
    for i in range(0, n, 4):
        fi = i + slide
        if fi < 0 or fi + 4 > len(static):
            continue
        a = dump[i:i + 4]
        b = static[fi:fi + 4]
        if a == b:
            same += 4
            if run_start is not None:
                diffs.append((run_start, i))
                run_start = None
        else:
            if run_start is None:
                run_start = i
    if run_start is not None:
        diffs.append((run_start, n))

    changed = sum(e - s for s, e in diffs)
    print('=== diff over 0x%X bytes ===' % n)
    print('  identical : %d bytes (%.2f%%)' % (same, 100.0 * same / max(n, 1)))
    print('  changed   : %d bytes (%.2f%%) in %d runs'
          % (changed, 100.0 * changed / max(n, 1), len(diffs)))
    print()

    print('=== largest changed runs (loader relocations / live state) ===')
    for s, e in sorted(diffs, key=lambda r: r[1] - r[0], reverse=True)[:25]:
        rt = RUNTIME_CODE_START + s
        print('  dump 0x%06X len 0x%-6X runtime 0x%08X  file 0x%06X'
              % (s, e - s, rt, s + slide))
    print()

    # Look for the runtime 8-byte PLT stub pattern:
    #   LDR R12, [PC, #0]   E59FC000
    #   LDR PC,  [R12]      E59FF000 (per notes)
    pat = struct.pack('<II', 0xE59FC000, 0xE59FF000)
    hits = []
    start = 0
    while True:
        i = dump.find(pat, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    print('=== runtime PLT stubs (E59FC000 / E59FF000) : %d found ===' % len(hits))
    for i in hits[:12]:
        target = struct.unpack_from('<I', dump, i + 8)[0] if i + 12 <= len(dump) else 0
        fixup = struct.unpack_from('<I', dump, i + 12)[0] if i + 16 <= len(dump) else 0
        print('  dump 0x%06X runtime 0x%08X -> target 0x%08X  fixup 0x%08X'
              % (i, RUNTIME_CODE_START + i, target, fixup))
    if len(hits) > 12:
        print('  ... and %d more' % (len(hits) - 12))


if __name__ == '__main__':
    main()
