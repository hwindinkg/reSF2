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
void method.WeaponModel.virtual_8 (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_8h, int32_t arg_ch, int32_t arg_10h, int32_t arg_14h) {
        push (ebp)
        ebp = esp
        push (ebx)    // arg2
        push (esi)
        esi = arg_ch  // arg3
        al = byte [esi + 0x670]
        v = al & al
        bh = v == 0
        v = al & al
        if (v) goto loc_0x10296b99 // likely
        goto loc_0x10296b86;
    loc_0x10296b99:
        // CODE XREF from method.WeaponModel.virtual_8 @ 0x10296b84(x)
        push (dword [arg_14h])
        ecx = esi     // int32_t arg_ch
        push (dword [arg_10h])
        push (dword [arg_ch])
        push (dword [arg_8h])
        fcn.1015a220 () // method.Model.virtual_8 // fcn.1015a220(0x0, 0x0, 0x0, 0x0, 0x0)
        bl = al
        v = bh & bh
        if (!v) goto loc_0x10296bbd // likely
        goto loc_0x10296bb2;
    loc_0x10296bbd:
        // CODE XREF from method.WeaponModel.virtual_8 @ 0x10296bb0(x)
        esi = pop ()
        al = bl
        ebx = pop ()
        ebp = pop ()
        return
        return eax;
    loc_0x10296bb2: // orphan
         ecx = dword [esi + 0x598]
         fcn.101654c0 ()          // fcn.101654c0(0x0)

}

