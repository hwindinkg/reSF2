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
void fcn.10121fa0 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        // CALL XREF from fcn.10059150 @ 0x10059264(x)
        // CALL XREF from method.ConditionKeys.virtual_8 @ 0x100875c9(x)
        // CALL XREF from method.ConditionKeys.virtual_28 @ 0x10087602(x)
        push (ebp)
        ebp = esp
        push (esi)
        push (edi)
        edi = dword [arg_8h]
        esi = ecx     // arg3
        push (esi)
        push (edi)
        fcn.10121e10 () // fcn.10121e10(0x0, 0x0, 0x0)
        esp += 8
        v = al & al
        if (!v) goto loc_0x10121fd4 // likely
        goto loc_0x10121fb8;
    loc_0x10121fd4:
        // CODE XREFS from fcn.10121fa0 @ 0x10121fb6(x), 0x10121fca(x)
        edi = pop ()
        al = 0
        esi = pop ()
        ebp = pop ()
        return
        return eax;
        return eax;
    loc_0x10121fcc:
        edi = pop ()
        al = 1
        esi = pop ()
        ebp = pop ()
        return
}

