#!/usr/bin/env python
"""
resolve_thunks.py

Between the PLT table (ends at game+0x6B60) and the game's own code sits a
band of tiny import thunks. Each is typically:

    LDR R12, [PC, #x]      ; or a direct B to a PLT stub
    ...
    B <plt_stub>

Game code calls these thunks, never the PLT stubs directly, which is why
scanning for branches to s3eDeviceYield found nothing. This resolves a thunk
address to the S3E API name behind it.

Usage:
    python resolve_thunks.py 0x6D00 0x7B94 0x73FC
    python resolve_thunks.py --band          # dump the whole thunk band
"""

import json
import os
import struct
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
PLT = os.path.join(HERE, 'plt_map.json')
BASE = 0x8F057000
PLT_END = 0x6B60


def load_plt():
    m = {}
    d = json.load(open(PLT))
    for e in d['entries']:
        a = int(e['stub'], 16)
        m[a] = e.get('name') or ('%s+%s' % (e.get('module'), e.get('moduleOffset', '?')))
    return m


def resolve(data, plt, off, maxi=8):
    """Follow a thunk at game+off to the PLT stub / import it jumps to.

    Handles the 12-byte 'static style' stub used by this band:
        ADD IP, PC, #hi        ; IP = pc+8+hi
        ADD IP, IP, #mid       ; IP += mid
        LDR PC, [IP, #lo]!     ; GOT slot = IP+lo
    plus plain B/BL and LDR PC,[PC,#x] forms.
    """
    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = True
    trail = []
    ip = None            # tracked IP value for the ADD/ADD/LDR pattern
    for insn in md.disasm(data[off:off + maxi * 4], BASE + off):
        trail.append('%s %s' % (insn.mnemonic, insn.op_str))
        ops = insn.op_str.replace(' ', '')

        # ADD IP, PC, #imm  -> start of a stub
        if insn.mnemonic == 'add' and ops.startswith('ip,pc,#'):
            ip = insn.address + 8 + int(ops.split('#')[1], 0)
            continue
        # ADD IP, IP, #imm  -> accumulate
        if insn.mnemonic == 'add' and ops.startswith('ip,ip,#') and ip is not None:
            ip += int(ops.split('#')[1], 0)
            continue
        # LDR PC, [IP, #imm]  -> GOT slot holds the real target
        if insn.mnemonic == 'ldr' and ops.startswith('pc,[ip') and ip is not None:
            lo = 0
            if '#' in ops:
                lo = int(ops.split('#')[1].rstrip(']!'), 0)
            got = ip + lo
            try:
                val = struct.unpack_from('<I', data, got - BASE)[0]
            except Exception:
                return None, trail
            name = plt.get(val)
            gotinfo = 'GOT 0x%08X -> 0x%08X' % (got, val)
            return ('%s  [%s]' % (name, gotinfo)) if name else ('UNKNOWN [%s]' % gotinfo), trail

        # direct branch to stub
        if insn.mnemonic in ('b', 'bl', 'bx') and insn.op_str.startswith('#'):
            t = int(insn.op_str[1:], 0)
            if t in plt:
                return plt[t], trail
        # LDR PC, [PC, #imm] style indirection
        if insn.mnemonic == 'ldr' and '[pc' in insn.op_str:
            try:
                d = int(insn.op_str.split('#')[-1].rstrip(']'), 0)
                ea = insn.address + 8 + d - BASE
                val = struct.unpack_from('<I', data, ea)[0]
                if val in plt:
                    return plt[val], trail
            except Exception:
                pass
        if insn.mnemonic.startswith('pop'):
            break
    return None, trail


def main():
    data = open(DUMP, 'rb').read()
    plt = load_plt()

    if '--band' in sys.argv:
        print('=== scanning thunk band game+0x%X .. +0x8000 ===' % PLT_END)
        off = PLT_END
        while off < 0x8000:
            name, trail = resolve(data, plt, off, 6)
            if name:
                print('  game+0x%-6X -> %s' % (off, name))
            off += 4
        return

    for a in sys.argv[1:]:
        off = int(a, 16)
        name, trail = resolve(data, plt, off)
        print('game+0x%-6X -> %s' % (off, name or 'UNRESOLVED'))
        for t in trail:
            print('      %s' % t)


if __name__ == '__main__':
    main()
