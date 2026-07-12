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
void fcn.10103c70 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch) {
        // CODE XREF from fcn.10103d50 @ 0x10103d6b(x)
        push (ebp)
        ebp = esp
        push (ecx)    // arg3
        ecx = dword [ecx + 0x94] // arg3
        push (ebx)    // arg2
        eax = dword [ecx + 0x2c] // arg3
        eax -= dword [ecx + 0x28] // arg3
        push (esi)
        eax >>= 2
        push (edi)
        v = eax & eax
        if (!v) goto loc_0x10103d32 // likely
        goto loc_0x10103c8e;
    loc_0x10103d32:
        // CODE XREFS from fcn.10103c70 @ 0x10103c88(x), 0x10103c9b(x)
        edi = pop ()
        esi = pop ()
        al = 0
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x10103ca1;
        return eax;
    loc_0x10103ca1:
        esi = dword [arg_8h]
        
    loc_0x10103ca4:
        // CODE XREF from fcn.10103c70 @ 0x10103d2c(x)
        eax = esi
        edx = eax + 1
        esp = esp     // ebp
        return eax;
    loc_0x10103cb0: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103cb5(x)
         cl = byte [eax]
         eax++
         v = cl & cl
         if (v) 
         goto loc_0x10103cb7;
    loc_0x10103cb7: // orphan
         ecx = dword [ebx + 0x10]
         eax -= edx
         edx = dword [ebx + 0xc]
         ecx -= edx
         v = ecx - eax
         if (v) 
         goto loc_0x10103cc5;
    loc_0x10103cc5: // orphan
         eax -= 4
         if (((unsigned) v) < 0) 
         goto loc_0x10103cca;
    loc_0x10103cca: // orphan
         ebx = ebx

    loc_0x10103cd0: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103cdf(x)
         ecx = dword [edx]
         v = ecx - dword [esi]
         if (v) 
         goto loc_0x10103cd6;
    loc_0x10103cd6: // orphan
         edx += 4
         esi += 4
         eax -= 4
         jae 0x10103cd0           // unlikely

         goto loc_0x10103ce1;
    loc_0x10103ce1: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103cc8(x)
         v = eax - 0xfffffffc
         if (!v) 
         goto loc_0x10103ce6;
    loc_0x10103ce6: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103cd4(x)
         cl = byte [edx]
         v = cl - byte [esi]
         if (v) 
         goto loc_0x10103cec;
    loc_0x10103cec: // orphan
         v = eax - 0xfffffffd
         if (!v) 
         goto loc_0x10103cf1;
    loc_0x10103cf1: // orphan
         cl = byte [edx + 1]
         v = cl - byte [esi + 1]
         if (v) 
         goto loc_0x10103cf9;
    loc_0x10103cf9: // orphan
         v = eax - 0xfffffffe
         if (!v) 
         goto loc_0x10103cfe;
    loc_0x10103cfe: // orphan
         cl = byte [edx + 2]
         v = cl - byte [esi + 2]
         if (v) 
         goto loc_0x10103d06;
    loc_0x10103d06: // orphan
         v = eax - 0xffffffff
         if (!v) 
         goto loc_0x10103d0b;
    loc_0x10103d0b: // orphan
         al = byte [edx + 3]
         v = al - byte [esi + 3]
         if (v) 
         goto loc_0x10103d13;
    loc_0x10103d13: // orphan
         // CODE XREFS from fcn.10103c70 @ 0x10103ce4(x), 0x10103cef(x), 0x10103cfc(x), 0x10103d09(x)
         eax = dword [arg_ch]
         v = dword [ebx + 4] - eax
         if (v > 0) 
         goto loc_0x10103d1b;
    loc_0x10103d1b: // orphan
         v = eax - dword [ebx + 8]
         if (v <= 0) 
         goto loc_0x10103d20;
    loc_0x10103d20: // orphan
         // CODE XREFS from fcn.10103c70 @ 0x10103cea(x), 0x10103cf7(x), 0x10103d04(x), 0x10103d11(x), 0x10103d19(x)
         esi = dword [arg_8h]

    loc_0x10103d23: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103cc3(x)
         ebx = dword [edi + 4]
         edi += 4
         v = edi - dword [var_4h]
         if (((unsigned) v) < 0) 
         return eax;
    loc_0x10103d3d: // orphan
         // CODE XREF from fcn.10103c70 @ 0x10103d1e(x)
         edi = pop ()
         esi = pop ()
         al = 1
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

