#!/usr/bin/env python3
"""Find writes to moveInside+0x68 using typed register tracking.

Strategy: moveInside is accessed via animInfo[0x94] (verified in Step 1,
fcn.10165c10). We:
1. Disassemble the entire .text section with capstone.
2. Find all instructions that read [reg+0x94] — these load moveInside ptr.
3. For each such load, track the register forward (until overwritten) and
   check if [reg+0x68] is written within the same function.
4. Report candidate write sites with surrounding context.

This is a poor-man's Ghidra "References to typed field" — it filters by
register data flow rather than raw offset matching.
"""
import capstone
import struct
import sys
from collections import defaultdict

SO_PATH = "/home/z/reSF2/reverse/binaries/ShadowFight2.s86"

def load_text():
    with open(SO_PATH, "rb") as f:
        data = f.read()
    # PE32: .text section vaddr=0x10001000, rawoff=0x400, size=0x36d2dc
    # (from objdump -h)
    text_va = 0x10001000
    text_off = 0x400
    text_size = 0x36d2dc
    text = data[text_off:text_off + text_size]
    return text, text_va

def disasm_all(text, va):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    # We need instruction addresses, so disasm with the correct base VA
    insns = []
    for ins in md.disasm(text, va):
        insns.append(ins)
    return insns

def main():
    text, text_va = load_text()
    print(f"Disassembling .text ({len(text)} bytes at VA 0x{text_va:08x})...", file=sys.stderr)
    insns = disasm_all(text, text_va)
    print(f"  {len(insns)} instructions", file=sys.stderr)

    # Build address → instruction index for quick lookup
    addr_to_idx = {}
    for i, ins in enumerate(insns):
        addr_to_idx[ins.address] = i

    # Step 1: Find all reads of [reg+0x94] — candidate moveInside loads.
    # Pattern: mov reg, [base+0x94] where base could be eax, ecx, edx, etc.
    moveinside_loads = []
    for i, ins in enumerate(insns):
        if ins.mnemonic != "mov":
            continue
        # Check operands for [reg+0x94] memory access
        for op in ins.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp == 0x94:
                # This reads/writes [reg+0x94]
                base_reg = op.mem.base
                if base_reg != capstone.x86.X86_REG_INVALID:
                    # Only care about reads (destination is a register)
                    if len(ins.operands) == 2 and ins.operands[0].type == capstone.x86.X86_OP_REG:
                        dst_reg = ins.operands[0].reg
                        moveinside_loads.append((i, ins.address, dst_reg, base_reg))

    print(f"Found {len(moveinside_loads)} candidate [reg+0x94] loads", file=sys.stderr)

    # Step 2: For each load, find the function boundary (scan back for prologue)
    # and track the destination register forward to find [reg+0x68] writes.
    results = []

    for load_idx, load_addr, dst_reg, base_reg in moveinside_loads:
        # Find function start: scan back for 'push ebp; mov ebp, esp' or similar
        func_start = None
        for j in range(load_idx, max(0, load_idx - 2000), -1):
            ins = insns[j]
            # Common prologue: push ebp (0x55) followed by mov ebp, esp (0x8b 0xec)
            if ins.mnemonic == "push" and len(ins.operands) == 1:
                if ins.operands[0].type == capstone.x86.X86_OP_REG and \
                   ins.operands[0].reg == capstone.x86.X86_REG_EBP:
                    # Check if next is mov ebp, esp
                    if j + 1 < len(insns):
                        nxt = insns[j+1]
                        if nxt.mnemonic == "mov" and len(nxt.operands) == 2:
                            if nxt.operands[0].type == capstone.x86.X86_OP_REG and \
                               nxt.operands[0].reg == capstone.x86.X86_REG_EBP and \
                               nxt.operands[1].type == capstone.x86.X86_OP_REG and \
                               nxt.operands[1].reg == capstone.x86.X86_REG_ESP:
                                func_start = j
                                break

        if func_start is None:
            func_start = max(0, load_idx - 500)

        # Find function end: scan forward for 'ret' or next prologue
        func_end = len(insns)
        for j in range(load_idx + 1, min(len(insns), load_idx + 3000)):
            ins = insns[j]
            if ins.mnemonic in ("ret", "retn"):
                func_end = j + 1
                break

        # Track dst_reg forward from load to func_end
        # Look for: mov [dst_reg+0x68], val  OR  mov [dst_reg+0x68], other_reg
        reg_tracked = dst_reg
        for j in range(load_idx + 1, func_end):
            ins = insns[j]
            # Check if dst_reg is overwritten
            if ins.mnemonic == "mov" and len(ins.operands) == 2:
                if ins.operands[0].type == capstone.x86.X86_OP_REG and \
                   ins.operands[0].reg == reg_tracked:
                    # Register overwritten — stop tracking
                    break

            # Check for write to [reg_tracked+0x68]
            if ins.mnemonic == "mov" and len(ins.operands) == 2:
                dst_op = ins.operands[0]
                if dst_op.type == capstone.x86.X86_OP_MEM and \
                   dst_op.mem.disp == 0x68 and \
                   dst_op.mem.base == reg_tracked:
                    # Found a write to moveInside+0x68!
                    results.append({
                        'load_addr': load_addr,
                        'write_addr': ins.address,
                        'write_insn': f"{ins.mnemonic} {ins.op_str}",
                        'func_start': insns[func_start].address,
                        'dst_reg_at_load': dst_reg,
                    })

    print(f"\n=== RESULTS: {len(results)} candidate writes to moveInside+0x68 ===", file=sys.stderr)
    for r in results:
        print(f"  load @ 0x{r['load_addr']:08x} (reg={r['dst_reg_at_load']})")
        print(f"    WRITE @ 0x{r['write_addr']:08x}: {r['write_insn']}")
        print(f"    func_start ~ 0x{r['func_start']:08x}")
        print()

    # Also print for grep-friendly output
    for r in results:
        print(f"WRITE 0x{r['write_addr']:08x} {r['write_insn']}")

if __name__ == "__main__":
    main()
