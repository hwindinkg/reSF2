#!/usr/bin/env python
"""
resolve_dats.py

Ghidra renders the PC-relative string loads in this image as

    DAT_8f43bc0c + -0x70bc4f08

which is unreadable. The real address is formed as

    LDR Rn, [PC, #x]      ; Rn = *DAT_addr   (a *relative* offset)
    ADD Rn, PC, Rn        ; Rn = pc_of_ADD + 8 + offset

so Ghidra's `DAT_<addr> + <bias>` pair encodes exactly that: the DAT holds the
offset, and the bias is `-(pc_of_ADD + 8) + something` folded by the decompiler.
Rather than reverse Ghidra's algebra, resolve it directly from the bytes:

    target = *DAT_addr + (DAT_addr's consuming ADD pc + 8)

We locate the consuming ADD by scanning forward from the LDR that references
the DAT. That is deterministic here because the pattern is always
`LDR Rn,[PC,#x]` followed (within a few instructions) by `ADD Rn,PC,Rn`.

NOTE the PC+8 trap: the PC belongs to the ADD, not the LDR. Using the LDR's
address yields a plausible but wrong pointer (this cost us a wrong
DamageDoublingRange read earlier -- 0x8F8780A4 vs 0x8F8780A8).

Usage:
    python resolve_dats.py 0x8F43B0D0          # every string a function loads
    python resolve_dats.py 0x8F43B0D0 --raw    # include unresolved words
"""

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
BASE = 0x8F057000


def load():
    return open(DUMP, 'rb').read()


def cstr(data, addr, maxlen=160):
    o = addr - BASE
    if o < 0 or o >= len(data):
        return None
    out = bytearray()
    while o < len(data) and data[o] != 0 and len(out) < maxlen:
        out.append(data[o])
        o += 1
    if not out:
        return None
    try:
        s = out.decode('utf-8')
    except UnicodeDecodeError:
        return None
    # Only accept printable text; anything else is not a string constant.
    if not all(32 <= c < 127 or c in (9, 10, 13) for c in out):
        return None
    return s


def func_end(data, start, limit=0x8000):
    """Find the function's extent by looking for the matching POP {...,pc}."""
    for off in range(0, limit, 4):
        o = start - BASE + off
        if o + 4 > len(data):
            break
        w = struct.unpack_from('<I', data, o)[0]
        # LDMIA SP!, {..., pc}  (pop with pc)
        if (w & 0x0FFF8000) == 0x08BD8000:
            return start + off + 4
    return start + limit


def scan(data, start, end):
    """Yield (ldr_addr, add_addr, target, text) for each PC-relative string."""
    results = []
    for off in range(0, end - start, 4):
        a = start + off
        o = a - BASE
        if o + 4 > len(data):
            break
        w = struct.unpack_from('<I', data, o)[0]
        # LDR Rd, [PC, #imm12] : cond 0101 1001 1111 dddd imm12
        if (w & 0x0FFF0000) != 0x059F0000:
            continue
        rd = (w >> 12) & 0xF
        lit_addr = a + 8 + (w & 0xFFF)
        lo = lit_addr - BASE
        if lo + 4 > len(data):
            continue
        rel = struct.unpack_from('<I', data, lo)[0]

        # Find the consuming `ADD Rd, PC, Rd` within a short window.
        for fwd in range(4, 0x80, 4):
            b = a + fwd
            bo = b - BASE
            if bo + 4 > len(data):
                break
            w2 = struct.unpack_from('<I', data, bo)[0]
            # ADD Rd, PC, Rm with Rd == Rm == rd
            if (w2 & 0x0FEF0000) == 0x008F0000:
                d2 = (w2 >> 12) & 0xF
                m2 = w2 & 0xF
                if d2 == rd and m2 == rd:
                    target = (b + 8 + rel) & 0xFFFFFFFF
                    txt = cstr(data, target)
                    results.append((a, b, target, txt))
                    break
    return results


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    start = int(sys.argv[1], 16)
    show_raw = '--raw' in sys.argv

    data = load()
    end = func_end(data, start)
    print('function 0x%08X (game+0x%X), scanning to 0x%08X'
          % (start, start - BASE, end))

    hits = scan(data, start, end)
    named = 0
    for ldr, add, target, txt in hits:
        if txt is None:
            if show_raw:
                print('  ADD@0x%08X -> 0x%08X  <not a string>' % (add, target))
            continue
        named += 1
        print('  ADD@0x%08X (game+0x%-7X) -> 0x%08X  %r'
              % (add, add - BASE, target, txt))
    print('%d string references (%d resolved)' % (len(hits), named))


if __name__ == '__main__':
    main()
