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
void fcn.102ca91f (int32_t arg1, int32_t arg2) {
        // CALL XREF from fcn.102c9618 @ 0x102c96bf(x)
        // CODE XREFS from sub.MSVCR110.dll__initterm_e @ +0x2f89f(x), +0x2f8ef(x)
        push (ebp)
        ebp = esp
        push (ecx)    // arg3
        push (esi)
        esi = ecx     // arg3
        v = byte [esi + 0x18] - 0
        if (!v) goto loc_0x102caa07 // unlikely
        goto loc_0x102ca930;
    loc_0x102caa07:
        // CODE XREF from fcn.102ca91f @ 0x102ca92a(x)
        esi = pop ()
        leave         // ebp
        return
        goto loc_0x102ca957;
        return eax;
    loc_0x102ca957:
        ebx = edi
        
    do {
        // CODE XREF from fcn.102ca91f @ 0x102ca972(x)
        eax = dword [esi + 0xe4]
        push (dword [ebx + eax])
        fcn.102c96f0 ()
        eax = byte [esi + 0x2a]
        edi++
        ecx = pop ()
        ebx = ebx + 0x48
        v = edi - eax
        jl 0x102ca959 // likely
    } while (/* 0x102ca959 */);
    loc_0x102ca974:
        edi = 0
        
    loc_0x102ca976:
        // CODE XREF from fcn.102ca91f @ 0x102ca955(x)
        push (dword [esi + 0xe4])
        fcn.102c96f0 ()
        v = byte [esi + 0x2c] - 0
        ecx = pop ()
        if (((unsigned) v) <= 0) goto 0x102ca9a5 // unlikely
        goto loc_0x102ca988;
    loc_0x102ca988: // orphan
         ebx = edi

    loc_0x102ca98a: // orphan
         // CODE XREF from fcn.102ca91f @ 0x102ca9a3(x)
         eax = dword [esi + 0xe8]
         push (dword [eax + ebx])
         fcn.102c96f0 ()
         eax = byte [esi + 0x2c]
         edi++
         ecx = pop ()
         ebx = ebx + 0x48
         v = edi - eax
         jl 0x102ca98a            // likely

         goto loc_0x102ca9a5;
    loc_0x102ca9a5: // orphan
         // CODE XREF from fcn.102ca91f @ 0x102ca986(x)
         push (dword [esi + 0xe8])
         fcn.102c96f0 ()
         push (dword [esi + 0xec])
         fcn.102c96f0 ()
         push (dword [esi + 0x134])
         fcn.102c96f0 ()
         esp += 0xc
         eax = esi + 0x17c        // int32_t arg1
         push (eax)
         ecx = esi                // int32_t arg_8h
         fcn.102cb033 ()          // fcn.102cb033(0x17c, 0x0, 0x0)
         push (3)                 // 3
         eax = pop ()
         ebx = esi + 0x1e4
         edi = esi + 0x31c
         dword [var_4h] = eax

    loc_0x102ca9e9: // orphan
         // CODE XREF from fcn.102ca91f @ 0x102caa03(x)
         v = dword [edi] - 0xffffffff
         if (!v) 
         goto loc_0x102ca9ee;
    loc_0x102ca9ee: // orphan
         push (ebx)
         ecx = esi                // int32_t arg_8h
         fcn.102cb033 ()          // fcn.102cb033(0x0, 0x0, 0x0)
         eax = dword [var_4h]

    loc_0x102ca9f9: // orphan
         // CODE XREF from fcn.102ca91f @ 0x102ca9ec(x)
         edi += 4
         ebx += 0x68              // 104
         eax--
         dword [var_4h] = eax
         if (v) 
         goto loc_0x102caa05;
    loc_0x102caa05: // orphan
         edi = pop ()
         ebx = pop ()

}

