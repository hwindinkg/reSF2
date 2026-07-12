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
void method.ConditionKeys.virtual_8 (int32_t arg1, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        eax = dword [arg_8h]
        push (esi)
        esi = ecx     // arg3
        ecx = dword [eax]
        v = byte [ecx + 0x34] - 0
        if (v) goto loc_0x100875ba // likely
        goto loc_0x100875b1;
    loc_0x100875ba:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875af(x)
        edx = esi + 0x14
        
    loc_0x100875bd:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875b8(x)
        v = byte [eax + 0x9c] - 0
        if (!v) goto loc_0x100875d2 // unlikely
        goto loc_0x100875c6;
        goto loc_0x100875ba;
        return eax;
        return eax;
    loc_0x100875c6:
        push (ecx)
        ecx = edx
        fcn.10121fa0 () // fcn.10121fa0(0x0, 0x0)
        cl = al
        goto loc_0x100875d4
        
    loc_0x100875d4:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875d0(x)
        v = byte [esi + 0xc] - 0
        esi = pop ()
        if (!v) goto loc_0x100875e6 // unlikely
        return eax;
        return eax;
    loc_0x100875db:
        eax = 0
        v = cl & cl
        al = v == 0
        ebp = pop ()
        return
    loc_0x100875d2: // orphan
         // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875c4(x)
         cl = 1

    loc_0x100875e6: // orphan
         // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875d9(x)
         eax = cl
         ebp = pop ()
         return

}

