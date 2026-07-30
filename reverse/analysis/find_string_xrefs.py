#!/usr/bin/env python
"""
find_string_xrefs.py

Finds code that references a string in the relocated dump.

Plain literal-pool search fails for this binary: string addresses are formed
PC-relatively, e.g.

    LDR  Rn, [PC, #imm]     ; loads a *PC-relative offset*, not the address
    ADD  Rn, PC, Rn         ; Rn = string address

so the literal holds (target - pc_of_add - 8). This script scans every
32-bit word, treats it as such an offset against each possible ADD site, and
reports matches. It also catches the direct `ADD Rn, PC, #imm` form.

Usage:
    python find_string_xrefs.py 0x8F799A6C
    python find_string_xrefs.py 0x8F799A6C --window 0x2000
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
BASE = 0x8F057000


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    target = int(sys.argv[1], 16)

    data = open(DUMP, 'rb').read()
    n = len(data) & ~3
    end = BASE + len(data)

    hits = []

    # Case 1: literal L at address A holds (target - (A_use + 8)) where the
    # ADD that consumes it sits somewhere after the LDR. We do not know the
    # ADD site a priori, so invert: for every literal value v, the implied ADD
    # site is  add_pc = target - v - 8. If that address is inside the dump and
    # decodes as ADD Rd, PC, Rd/Rn, it is a real xref.
    for off in range(0, n, 4):
        v = struct.unpack_from('<I', data, off)[0]
        if v == 0 or v > 0x00FFFFFF and v < 0xFF000000:
            # offsets in this image are small positive or small negative
            pass
        add_pc = (target - v - 8) & 0xFFFFFFFF
        if not (BASE <= add_pc < end):
            continue
        ao = add_pc - BASE
        if ao + 4 > len(data):
            continue
        w = struct.unpack_from('<I', data, ao)[0]
        # ADD Rd, PC, Rm  : cond 0000 100 0 1111 dddd 00000000 mmmm
        #                   opcode 0x008F0000 mask 0x0FEF0000
        if (w & 0x0FEF0000) != 0x008F0000:
            continue
        # Require the register pairing to be consistent: the ADD must consume
        # the same register the LDR loaded, i.e. ADD Rd, PC, Rd with Rd == Rm.
        rd = (w >> 12) & 0xF
        rm = w & 0xF
        if rd != rm:
            continue
        # And a matching `LDR Rd, [PC, #x]` must actually target this literal
        # within a short window before the ADD.
        ok = False
        for back in range(4, 0x1000, 4):
            lo = ao - back
            if lo < 0:
                break
            lw = struct.unpack_from('<I', data, lo)[0]
            # LDR Rd, [PC, #imm12] : cond 0101 1001 1111 dddd imm12
            if (lw & 0x0FFF0000) != (0x059F0000 | (rd << 12)):
                continue
            lit = (BASE + lo + 8 + (lw & 0xFFF))
            if lit == BASE + off:
                ok = True
                break
        if ok:
            hits.append(('LDR+ADD Rd,PC,Rd', BASE + off, v, add_pc, w))

    # Case 2: ADD Rd, PC, #imm12 (rotated immediate) reaching the string
    for off in range(0, n, 4):
        w = struct.unpack_from('<I', data, off)[0]
        if (w & 0x0FFF0000) != 0x028F0000:      # ADD Rd, PC, #imm
            continue
        imm = w & 0xFF
        rot = ((w >> 8) & 0xF) * 2
        val = ((imm >> rot) | (imm << (32 - rot))) & 0xFFFFFFFF if rot else imm
        pc = BASE + off + 8
        if (pc + val) & 0xFFFFFFFF == target:
            hits.append(('ADD Rd,PC,#imm', BASE + off, val, BASE + off, w))

    print('xrefs to 0x%08X : %d' % (target, len(hits)))
    for kind, site, val, add_pc, w in hits[:40]:
        print('  %-16s literal@0x%08X val=0x%08X  ADD@0x%08X (game+0x%X) insn=%08x'
              % (kind, site, val, add_pc, add_pc - BASE, w))


if __name__ == '__main__':
    main()
