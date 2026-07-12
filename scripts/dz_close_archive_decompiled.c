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
void fcn.102c9618 (int32_t arg1) {
        // CALL XREF from fcn.102ca2cf @ 0x102ca351(x)
        // CALL XREF from fcn.102ca2cf @ +0xc5(x)
        // CALL XREF from fcn.102ca3bc @ 0x102ca45b(x)
        push (4)      // 4
        eax = 0x10330c66 // 'f\f3\x10'
        fcn.10300831 () // fcn.10300831(0x10330c66, 0x0, 0x0)
        edi = ecx
        dword [var_10h] = edi
        v = byte [edi + 0x74] - 0
        dword [var_4h] = 4
        if (!v) goto loc_0x102c9688 // unlikely
        goto loc_0x102c9636;
    loc_0x102c9688:
        // CODE XREF from fcn.102c9618 @ 0x102c9634(x)
        ecx = edi + 0x45c
        byte [var_4h] = 3
        fcn.102cc060 ()
        ecx = edi + 0x440
        byte [var_4h] = 2
        fcn.102cc060 ()
        ecx = edi + 0x414
        byte [var_4h] = 1
        fcn.102cc1b2 () // fcn.102cc1b2(0x0)
        ecx = edi + 0xa0
        byte [var_4h] = 0
        fcn.102ca91f () // fcn.102ca91f(0x0, 0x0)
        dword [var_4h] |= 0xffffffff // [0xffffffff:4]=-1 // -1
        ecx = edi + 4
        fcn.102ca54b () // fcn.102ca54b(0x0)
        fcn.1030080e ()
        return
        goto loc_0x102c9640;
        return eax;
    do {
        // CODE XREF from fcn.102c9618 @ 0x102c964e(x)
        esi = dword [eax + 8]
        push (eax)
        fcn.102c96f0 ()
        ecx = pop ()
        eax = esi
        v = esi & esi
    } while (esi & esi);
    loc_0x102c9650:
        // CODE XREF from fcn.102c9618 @ 0x102c963e(x)
        push (dword [edi + 0x78])
        fcn.102c96f0 ()
        push (dword [edi + 0x7c])
        fcn.102c96f0 ()
        push (dword [edi + 0x8c])
        fcn.102c96f0 ()
        push (dword [edi + 0x80])
        fcn.102c96f0 ()
        push (dword [edi + 0x84])
        fcn.102c96f0 ()
        esp += 0x14
        byte [edi + 0x74] = 0
        break;
}

