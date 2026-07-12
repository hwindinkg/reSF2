INFO: Analyze all flags starting with sym. and entry0 (aa)
INFO: Analyze imports (af@@@i)
INFO: Analyze entrypoint (af@ entry0)
INFO: Analyze symbols (af@@@s)
INFO: Running plugin pre-analysis hooks
INFO: Analyze all functions arguments/locals (afva@@F)
INFO: Analyze function calls (aac)
INFO: Analyze len bytes of instructions for references (aar)
INFO: Finding and parsing C++ vtables (avrr)
INFO: Analyzing methods (af @@ method.*)
WARN: Function already defined in 0x1001b5a0
WARN: Function already defined in 0x100817d0
WARN: Function already defined in 0x10199a70
WARN: Function already defined in 0x10061260
WARN: Function already defined in 0x10159c80
INFO: Recovering local variables (afva@@@F)
INFO: Type matching analysis for all functions (aaft)
INFO: Propagate noreturn information (aanr)
INFO: Use -AA or aaaa to perform additional experimental analysis
// callconv: eax reg (eax, ebx, ecx, edx);
void fcn.102ca260 (void) {
        // CALL XREF from sub.MSVCR110.dll__initterm_e @ +0x349d8(x)
        push (ebp)
        ebp = esp
        esp -= 0x84
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_4h] = eax
        v = byte [0x10667290] - 0 // [0x10667290:1]=0
        if (v) goto loc_0x102ca2be // unlikely
        goto loc_0x102ca27c;
    loc_0x102ca2be:
        // CODE XREFS from fcn.102ca260 @ 0x102ca27a(x), 0x102ca29e(x), 0x102ca2af(x)
        ecx = dword [var_4h] // ebp
        ecx ^= ebp
        fcn.102fff32 () // fcn.102fff32(0x0)
        leave         // ebp
        return
        goto loc_0x102ca2a0;
        return eax;
    loc_0x102ca2a0:
        eax = var_84h
        push (eax)
        fcn.102fc030 () // fcn.102fc030(0x177f7c, 0x0)
        ecx = pop ()
        v = al & al
        if (!v) goto loc_0x102ca2be // unlikely
        goto loc_0x102ca2b1;
    loc_0x102ca2b1:
        eax = var_84h
        push (eax)
        fcn.102ca2cf () // fcn.102ca2cf(0x177f7c, 0x0)
        ecx = pop ()
        break;
}

