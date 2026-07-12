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
void fcn.10103d50 (int32_t arg1, int32_t arg2, int32_t arg_ch) {
        // CALL XREF from fcn.10161ad0 @ 0x10161c5d(x)
        push (ebp)
        ebp = esp
        eax = dword [arg_ch]
        push (esi)
        esi = word [ecx + 0x64] // arg3
        eax--
        esi++
        cdq
        esi /=
        edx = dword [ecx + 0x74] // arg3
        edx--
        esi = pop ()
        eax += edx
        dword [arg_ch] = eax
        ebp = pop ()
        goto loc_0x10103c70 // fcn.10103c70 // fcn.10103c70 // fcn.10103c70(0xfffffffe, 0x0, 0x0, 0xfffffffe)
        // chop
}

