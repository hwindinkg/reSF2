# Ghidra Decompilation Guide for Shadow Fight 2 s86 Binary

The `Shadow Fight 2.s86` binary is **heavily obfuscated** with anti-tamper protection. Standard Ghidra decompilation produces garbage (overlapping instructions, bad control flow, unreachable blocks). This guide explains how to get readable decompilation.

## Problem: Anti-Tamper Obfuscation

The s86 binary uses several anti-reverse-engineering techniques:

1. **Overlapping instructions** — instructions at address N overlap with instructions at N+1, confusing disassemblers
2. **Opaque predicates** — conditional branches that always go one way but Ghidra can't prove it
3. **Dead code injection** — unreachable blocks that waste analysis time
4. **Control flow flattening** — switch-based dispatch that obscures real control flow
5. **Junk byte insertion** — random bytes between functions that break linear sweep

## Solution: Multi-Step Approach

### Step 1: Install Ghidra + Required Extensions

```bash
# Download Ghidra 11.x from https://ghidra-sre.org/
# Requires JDK 17+

# Install these Ghidra extensions (File → Configure → Extensions):
# - GnuDisassembler (for alternate disassembly)
# - FunctionID (for library function identification)
```

### Step 2: Load the Binary Correctly

1. **File → Import Binary** → select `Shadow Fight 2.s86`
2. Format: **PE** (auto-detected)
3. Language: **x86:LE:32:default** (auto-detected)
4. Click OK, then **Analyze**

### Step 3: Configure Analysis Options

When the analysis dialog appears, enable these options:

```
✓ Aggressive Instruction Finder     — finds code in data sections
✓ Basic Constant Reference          — find string/data xrefs
✓ Call Convention ID                — identify calling conventions
✓ Call-Fixup Installer              — fix function calls
✓ Control Flow Reference            — build CFG
✓ Decompiler Parameter ID           — recover function parameters
✓ Decompiler Switch Analysis        — fix switch statements
✓ Demangler                         — C++ symbol demangling
✓ Disassemble Entry Points          — start from entry
✓ ELF Scalar Operand Analyzer       — (harmless for PE)
✓ External Entry References         — find DLL imports
✓ Function Start Search             — find function boundaries
✓ Non-Return Functions              — mark noreturn functions
✓ Reference                         — find all references
✓ Stack                             — analyze stack frames
✓ Subroutine References             — find call targets
✓ Symbolic Propagator               — propagate values
✓ Windows PE CLRB                   — (harmless)
✓ WindowsResourceReference          — find .rsrc refs
```

**Disable** these (they conflict with obfuscated code):
```
✗ Subroutine References (can produce false positives on junk bytes)
```

### Step 4: Apply STLport Type Signatures

The binary uses STLport (not MSVC STL). Apply STLport type info for better decompilation:

1. **File → Parse C Source...**
2. Create a new C file with STLport type definitions:
```c
// STLport basic_string layout (used heavily in the binary)
typedef struct _STLport_string {
    void* _M_alloc;        // allocator
    char* _M_start;        // string data start
    char* _M_finish;       // string data end
    char* _M_end_of_storage; // buffer end
} STLport_string;

// Cocos2d-x CCObject (base class for most game objects)
typedef struct _CCObject {
    void* vtable;
    int m_nTag;
    int m_nZOrder;
    void* m_pParent;
} CCObject;
```
3. Parse and apply

### Step 5: Fix Overlapping Instructions

This is the KEY step for readable decompilation:

1. **Window → Disassembly**
2. Search for the "WARNING: Instruction at X overlaps instruction at Y" messages
3. For each overlap:
   - Right-click the address → **Clear Code Bytes**
   - Right-click → **Disassemble...** → start from the correct instruction boundary
   - This forces Ghidra to re-disassemble from the correct alignment

4. **Batch fix**: Use Ghidra Script (Window → Script Manager → New Script):
```python
# Ghidra Python script: fix_overlaps.py
# Fixes overlapping instruction issues by re-disassembling
from ghidra.program.model.listing import CodeUnit

listing = currentProgram.getListing()
mem = currentProgram.getMemory()

# Get all functions
fm = currentProgram.getFunctionManager()
for func in fm.getFunctions(True):
    body = func.getBody()
    for addr in body.getAddresses(True):
        cu = listing.getCodeUnitAt(addr)
        if cu is None:
            listing.createInstruction(addr)
            print(f"Created instruction at {addr}")
```

### Step 6: Fix Control Flow Flattening

The binary uses switch-based control flow flattening. Ghidra's decompiler can sometimes recover this automatically, but you can help:

1. **Edit → Tool Options → Decompiler → Analysis**
2. Set **"Max Switch Table Entries"** to 10000
3. Set **"Aggressive Constant Propagation"** to true
4. Set **"Eliminate unreachable code"** to true

### Step 7: Use the S3ELoader Plugin for Android ARM Binary

For the **Android ARM binary** (ShadowFight2.bin), use the S3ELoader plugin:
1. Clone `https://github.com/knot126/S3ELoader`
2. Build: `cd S3ELoader && gradle build` (requires Ghidra dev kit)
3. Copy the .zip to `ghidra/Ghidra/Extensions/`
4. Restart Ghidra, import ShadowFight2.bin as "S3ELoader" format
5. This applies relocations and creates proper memory blocks

### Step 8: Manual Function Identification

Use string xrefs to find functions (the method that worked for us):

1. **Search → For Strings** → search for `ModelAnimation::mirrorNodes`
2. Find the string address (e.g., 0x105b228c)
3. **Search → For Address Tables** → search for the string VA
4. Find xrefs (push instructions that reference the string)
5. The function containing the push is the function that uses that string

### Step 9: Alternative — Use IDA Pro with Plugins

If Ghidra still produces garbage, try IDA Pro with:
- **Hex-Rays Decompiler** (better at handling obfuscated code)
- **Tigress Deobfuscator plugin** (for control flow unflattening)
- **VMProtect devirtualizer** (if the binary uses VM-based protection)

### Step 10: Alternative — Dynamic Analysis

Instead of static decompilation, use **dynamic analysis**:

1. Run the game on Windows with **x64dbg** (32-bit mode)
2. Set breakpoints at the string xref addresses (0x10164093, 0x1016622a, etc.)
3. When the breakpoint hits, trace the function in the debugger
4. Use **Scylla** to dump the unpacked/deobfuscated binary from memory
5. Load the dumped binary into Ghidra — it will be much cleaner

## Key Function Addresses (Already Found)

| Function | String Address | Xref Address | Purpose |
|----------|---------------|-------------|---------|
| ModelAnimation::mirrorNodes | 0x105b228c | 0x10164093 | Skeleton mirroring for facing |
| ModelAnimation::getPlayerAnimation | 0x105b2228 | 0x1016622a | Animation selection + position math |
| Model::setNearestEnemy | 0x105b19cc | 0x1015870f | Auto-facing enemy |
| IntervalAttack::getFactors | 0x105ad6a0 | 0x10115921 | Attack damage calc |
| Model::startAction | 0x105b1a04 | 0x1015c4fc | Start model action |
| NPivot (string) | 0x1038b6a4 | 5 xrefs | Pivot node references |

## What the Decompiled Code Reveals (Partial)

From `ModelAnimation::getPlayerAnimation` (0x1016622a), even with obfuscation, we can see:
- Float arithmetic: `pos = direction * speed + old_pos` (position update)
- Switch statements on model type (offset +0xb0, +0x68, +0x6c)
- Subcontainer index checks (animation container access)
- Function calls to `FUN_10164990` (likely animation update) and `FUN_1016c5d0` (likely node update)

From `Model::setNearestEnemy` (0x1015870f):
- Simple pointer copy: `*(model+0x120) = *(model+0x190)` (enemy pointer stored at offset 0x120)

## Summary

The s86 binary's anti-tamper makes static decompilation very difficult. The most productive approaches are:
1. **Dynamic analysis** (x64dbg + memory dump) — best results
2. **String xref analysis** — finds function locations but not internal logic
3. **Pattern matching** — identify code patterns (float math, switch statements) even in obfuscated output
4. **Android ARM binary** — may be less obfuscated (different protection scheme)
