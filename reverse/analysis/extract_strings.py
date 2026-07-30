#!/usr/bin/env python
"""
extract_strings.py

Pulls the C-string tables out of the relocated runtime dump and groups them
by neighbourhood, so config schemas (tacticSettings, internalSettings,
animation/movement keys) can be read off directly.

The engine currently guesses several of these key sets -- see the
[HEURISTIC-TODO] markers in engine/game/tactic_settings.cpp and
engine/game/game.cpp. The binary has the authoritative list.

Usage:
    python extract_strings.py                     # summary of dense clusters
    python extract_strings.py --grep Damage       # search
    python extract_strings.py --at 0x8f797b60     # dump around an address
    python extract_strings.py --run 0x8f797574 400  # N consecutive strings
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
BASE = 0x8F057000

MIN_LEN = 4


def all_strings(data):
    """[(runtime_addr, text)] for NUL-terminated printable runs."""
    out = []
    for m in re.finditer(rb'[\x20-\x7E]{%d,200}\x00' % MIN_LEN, data):
        out.append((BASE + m.start(), m.group()[:-1].decode('latin1')))
    return out


def main():
    data = open(DUMP, 'rb').read()
    strs = all_strings(data)

    if '--grep' in sys.argv:
        pat = sys.argv[sys.argv.index('--grep') + 1]
        rx = re.compile(pat, re.I)
        n = 0
        for a, s in strs:
            if rx.search(s):
                print('  0x%08X  %s' % (a, s))
                n += 1
        print('%d matches' % n)
        return

    if '--at' in sys.argv:
        a = int(sys.argv[sys.argv.index('--at') + 1], 16)
        for addr, s in strs:
            if abs(addr - a) < 0x400:
                mark = ' <==' if abs(addr - a) < 8 else ''
                print('  0x%08X  %s%s' % (addr, s, mark))
        return

    if '--run' in sys.argv:
        i = sys.argv.index('--run')
        a = int(sys.argv[i + 1], 16)
        count = int(sys.argv[i + 2]) if len(sys.argv) > i + 2 else 200
        start = None
        for idx, (addr, s) in enumerate(strs):
            if addr >= a:
                start = idx
                break
        if start is None:
            print('address beyond table')
            return
        for addr, s in strs[start:start + count]:
            print('  0x%08X  %s' % (addr, s))
        return

    print('total strings: %d' % len(strs))
    print('\n=== dense clusters (likely config/schema tables) ===')
    # cluster by gap
    clusters = []
    cur = [strs[0]]
    for prev, nxt in zip(strs, strs[1:]):
        if nxt[0] - prev[0] < 0x80:
            cur.append(nxt)
        else:
            clusters.append(cur)
            cur = [nxt]
    clusters.append(cur)
    clusters.sort(key=len, reverse=True)
    for c in clusters[:20]:
        print('  0x%08X .. 0x%08X : %d strings   e.g. %s'
              % (c[0][0], c[-1][0], len(c),
                 ', '.join(s for _, s in c[:4])))


if __name__ == '__main__':
    main()
