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
void fcn.102c9bfc (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_8h, int32_t arg_ch, int32_t arg_10h) {
        // CALL XREF from fcn.102ca2cf @ 0x102ca333(x)
        push (ebp)
        ebp = esp
        push (esi)
        push (edi)
        push (dword [arg_10h])
        esi = arg_ch  // arg3
        push (dword [arg_ch])
        push (dword [arg_8h])
        fcn.102c9778 () // fcn.102c9778(0x0, 0x0, 0x0, 0x0, 0x0)
        edi = eax
        v = edi & edi
        if (!v) goto loc_0x102c9c4b // likely
        goto loc_0x102c9c17;
    loc_0x102c9c4b:
        // CODE XREF from fcn.102c9bfc @ 0x102c9c15(x)
        eax = edi
        edi = pop ()
        esi = pop ()
        ebp = pop ()
        return
        return eax;
}

