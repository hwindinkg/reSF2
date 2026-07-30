#!/usr/bin/env python
"""
find_callers.py -- find every BL/B that targets a given address in the
relocated runtime game-code dump.

This replaces the guesswork in the old notes. Because the dump is the
*relocated* image, ARM BL displacements resolve to real addresses, so the
call graph can be recovered by scanning for branch encodings directly.

ARM (A1) branch encoding:
    cond 101 L imm24      ; L=1 -> BL, L=0 -> B
    target = pc + 8 + sign_extend(imm24 << 2)

Usage:
    python find_callers.py s3eDeviceYield
    python find_callers.py 0x8f059010
    python find_callers.py 0x8f059010 --recurse 2
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
PLT = os.path.join(HERE, 'plt_map.json')
DUMP_BASE = 0x8F057000


def load_plt():
    byname, byaddr = {}, {}
    if os.path.exists(PLT):
        d = json.load(open(PLT))
        for e in d['entries']:
            a = int(e['stub'], 16)
            if e.get('name'):
                byname[e['name']] = a
                byaddr[a] = e['name']
    return byname, byaddr


def scan_branches(data):
    """Yield (site_addr, target_addr, is_bl) for every ARM B/BL in the dump."""
    out = []
    n = len(data) & ~3
    for off in range(0, n, 4):
        w = struct.unpack_from('<I', data, off)[0]
        if (w & 0x0E000000) != 0x0A000000:
            continue
        cond = w >> 28
        if cond == 0xF:                      # BLX imm / unconditional special
            continue
        imm = w & 0x00FFFFFF
        if imm & 0x00800000:
            imm -= 0x01000000
        site = DUMP_BASE + off
        target = site + 8 + (imm << 2)
        out.append((site, target, bool(w & 0x01000000)))
    return out


def function_start(data, addr, limit=0x600):
    """Walk backwards to the nearest PUSH {..., lr} -- the function prologue."""
    off = addr - DUMP_BASE
    for back in range(0, limit, 4):
        o = off - back
        if o < 0:
            break
        w = struct.unpack_from('<I', data, o)[0]
        # STMFD SP!, {...} with LR set: cond 100100101101 rlist, bit14 = LR
        if (w & 0x0FFF0000) == 0x092D0000 and (w & 0x4000):
            return DUMP_BASE + o
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    data = open(DUMP, 'rb').read()
    byname, byaddr = load_plt()

    arg = sys.argv[1]
    target = byname.get(arg) or int(arg, 16)
    label = byaddr.get(target, arg)

    depth = 1
    if '--recurse' in sys.argv:
        depth = int(sys.argv[sys.argv.index('--recurse') + 1])

    print('scanning %.2f MB for branches...' % (len(data) / 1048576.0))
    branches = scan_branches(data)
    print('%d B/BL instructions found\n' % len(branches))

    index = {}
    for site, tgt, is_bl in branches:
        index.setdefault(tgt, []).append((site, is_bl))

    level = {target}
    seen = set()
    for d in range(depth):
        print('=== level %d: callers of %s ===' % (d + 1,
              ', '.join(byaddr.get(t, hex(t)) for t in sorted(level))))
        nxt = set()
        for t in sorted(level):
            callers = index.get(t, [])
            if not callers:
                print('  %s : no direct B/BL callers (registered via pointer?)'
                      % byaddr.get(t, hex(t)))
            for site, is_bl in callers:
                fs = function_start(data, site)
                fstr = ('func 0x%08X (game+0x%X)' % (fs, fs - DUMP_BASE)) if fs else 'func ?'
                print('  %s 0x%08X (game+0x%-7X) in %s'
                      % ('BL' if is_bl else 'B ', site, site - DUMP_BASE, fstr))
                if fs and fs not in seen:
                    seen.add(fs)
                    nxt.add(fs)
        print()
        level = nxt
        if not level:
            break


if __name__ == '__main__':
    main()
