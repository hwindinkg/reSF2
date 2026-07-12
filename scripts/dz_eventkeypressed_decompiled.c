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
void method.EventKeyPressed.virtual_0 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        push (esi)
        esi = ecx     // arg3
        dword [esi] = vtable.EventKeyPressed.0 // [0x10388f84:4]=0x100ae850 eip // "P\xe8\n\x100,\x02\x10@\xcc\x1d\x10\x14\xa6`\x10\x10\xec\n\x10p\xfa\n\x10\xc0\xf3\n\x10@\xcc\x1d\x10\xc0\xfb\n\x10statistics/events.json"
        fcn.100ace20 () // fcn.100ace20(0x0)
        v = byte [arg_8h] & 1
        if (!v) goto loc_0x100ae870 // likely
        goto loc_0x100ae867;
    loc_0x100ae870:
        // CODE XREF from method.EventKeyPressed.virtual_0 @ 0x100ae865(x)
        eax = esi
        esi = pop ()
        ebp = pop ()
        return
        return eax;
}

