#!/usr/bin/env python
"""
find_runtime_offset.py

Establishes the mapping between the runtime game-code region and the
unpacked S3E binary on disk, empirically.

Earlier sessions assumed  static 0x4A000000 -> file offset 0 (or 0x45251,
the S3E codeOffset).  Both were unverified.  This script takes byte
snippets dumped from the live process and locates them in the file, so the
slide is derived instead of guessed.

Usage:
    python find_runtime_offset.py            # uses the built-in samples
    python find_runtime_offset.py dump.bin   # correlate a full region dump
"""

import sys
import os

BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   '..', 'binaries', 'ShadowFight2_android.bin')

RUNTIME_IMAGE_BASE = 0x8F056000
STATIC_IMAGE_BASE = 0x4A000000

# (label, runtime address, first 16 bytes as seen in the live process)
SAMPLES = [
    ('main loop        0x4A679F54', 0x8F6CFF54,
     '012082e2 0000d1e5 021081e2 5554a0e1'),
    ('frame callback   0x4A679914', 0x8F6CF914,
     '0c909de5 949827e0 277887e0 2778a0e1'),
    ('cb dispatch      0x4A6798B0', 0x8F6CF8B0,
     '0140c2e5 b440d3e1 2444a0e1 0240c2e5'),
    ('yield wrapper    0x4A67A1E0', 0x8F6D01E0,
     '0330a0e3 046080e5 0a30c0e5 f083bde8'),
    ('secondary init   0x4A686A1C', 0x8F6DCA1C,
     '90219fe5 022094e7 14208de5 002092e5'),
]


def parse(hexstr):
    return bytes(bytearray.fromhex(hexstr.replace(' ', '')))


def main():
    data = open(BIN, 'rb').read()
    print('binary: %s (%d bytes / %.2f MB)' % (BIN, len(data), len(data) / 1048576.0))
    print('runtime image base: 0x%08X' % RUNTIME_IMAGE_BASE)
    print()

    slides = {}
    for label, rt_addr, hexbytes in SAMPLES:
        pat = parse(hexbytes)
        hits = []
        start = 0
        while True:
            i = data.find(pat, start)
            if i < 0:
                break
            hits.append(i)
            start = i + 1
            if len(hits) > 8:
                break

        rt_off = rt_addr - RUNTIME_IMAGE_BASE
        print('%s  runtime=0x%08X  region_off=0x%06X' % (label, rt_addr, rt_off))
        if not hits:
            print('    NOT FOUND in file')
        for i in hits:
            slide = i - rt_off
            slides.setdefault(slide, []).append(label)
            print('    file 0x%06X   =>  file_off - region_off = 0x%X (%d)'
                  % (i, slide & 0xFFFFFFFF, slide))
        print()

    print('=== consistent slides ===')
    for slide, labels in sorted(slides.items(), key=lambda kv: -len(kv[1])):
        print('  slide %+d (0x%X): %d/%d samples' % (slide, slide & 0xFFFFFFFF,
                                                     len(labels), len(SAMPLES)))
        if len(labels) == len(SAMPLES):
            print('    -> CONFIRMED. file_offset = (runtime - 0x%08X) + %d'
                  % (RUNTIME_IMAGE_BASE, slide))
            print('    -> static_addr  = (file_offset - %d) + 0x%08X'
                  % (slide, STATIC_IMAGE_BASE))


if __name__ == '__main__':
    main()
