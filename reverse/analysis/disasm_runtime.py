#!/usr/bin/env python
"""
disasm_runtime.py -- disassemble the live game-code dump at a runtime address.

The dump (game_region_runtime.bin) starts at runtime 0x8F057000 and is the
fully-relocated image, so branch targets and PLT calls resolve correctly --
unlike the static file, where the loader's fixups are absent.

PLT stub calls are annotated with the resolved S3E symbol name from
plt_map.json, so `BL 0x8f059010` shows up as `-> s3eDeviceYield`.

Usage:
    python disasm_runtime.py 0x8f6cff54 [instruction_count]
    python disasm_runtime.py main_loop
"""

import json
import os
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, '..', 'binaries', 'game_region_runtime.bin')
PLT = os.path.join(HERE, 'plt_map.json')

DUMP_BASE = 0x8F057000

NAMED = {
    'main_loop':      0x8F6CFF54,
    'frame_callback': 0x8F6CF914,
    'cb_dispatch':    0x8F6CF8B0,
    'yield_wrapper':  0x8F6D01E0,
    'secondary_init': 0x8F6DCA1C,
}


def load_plt():
    """runtime stub address -> symbol name"""
    m = {}
    if not os.path.exists(PLT):
        return m
    d = json.load(open(PLT))
    for e in d['entries']:
        if e.get('name'):
            m[int(e['stub'], 16)] = e['name']
        elif e.get('module'):
            m[int(e['stub'], 16)] = '%s+%s' % (e['module'], e.get('moduleOffset', '?'))
    return m


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    arg = sys.argv[1]
    addr = NAMED[arg] if arg in NAMED else int(arg, 16)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60

    data = open(DUMP, 'rb').read()
    plt = load_plt()

    off = addr - DUMP_BASE
    if off < 0 or off >= len(data):
        print('address 0x%X outside dump (0x%X - 0x%X)'
              % (addr, DUMP_BASE, DUMP_BASE + len(data)))
        return

    md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    md.detail = True

    print('=== 0x%08X (%s) dump+0x%X ===' % (addr, arg, off))
    n = 0
    for insn in md.disasm(data[off:off + count * 4 + 32], addr):
        line = '  0x%08X  %-8s %-32s' % (insn.address, insn.mnemonic, insn.op_str)

        # Annotate branches into PLT stubs and known functions.
        note = ''
        if insn.mnemonic.startswith(('bl', 'b')) and insn.op_str.startswith('#'):
            try:
                t = int(insn.op_str[1:], 0)
                if t in plt:
                    note = '   -> %s' % plt[t]
                else:
                    for k, v in NAMED.items():
                        if v == t:
                            note = '   -> %s' % k
                            break
                    else:
                        if DUMP_BASE <= t < DUMP_BASE + len(data):
                            note = '   -> game+0x%X' % (t - DUMP_BASE)
            except ValueError:
                pass

        # Annotate PC-relative loads with the loaded value.
        if insn.mnemonic == 'ldr' and '[pc' in insn.op_str:
            try:
                disp = insn.op_str.split('#')[-1].rstrip(']')
                d = int(disp, 0)
                ea = insn.address + 8 + d
                eo = ea - DUMP_BASE
                if 0 <= eo + 4 <= len(data):
                    val = int.from_bytes(data[eo:eo + 4], 'little')
                    note = '   [0x%08X] = 0x%08X' % (ea, val)
                    if val in plt:
                        note += ' (%s)' % plt[val]
            except (ValueError, IndexError):
                pass

        print(line + note)
        n += 1
        if n >= count:
            break


if __name__ == '__main__':
    main()
