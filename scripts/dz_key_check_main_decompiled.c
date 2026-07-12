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
void fcn.10121e10 (int32_t arg1, int32_t arg_8h, int32_t arg_ch) {
        // CALL XREFS from fcn.10121fa0 @ 0x10121fac(x), 0x10121fc0(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x10328c58)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x10
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        edi = dword [arg_ch]
        ecx = dword [arg_8h]
        eax = dword [edi + 4]
        esi = dword [ecx + 4]
        eax -= dword [edi]
        esi -= dword [ecx]
        eax >>= 2
        esi >>= 2
        v = eax - esi
        if (((unsigned) v) > 0) goto 0x10121f1f // unlikely
        goto loc_0x10121e56;
    loc_0x10121f1f:
        // CODE XREF from fcn.10121e10 @ 0x10121e50(x)
        al = 0
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x10121e5a;
        return eax;
    loc_0x10121e5a:
        push (esi)
        fcn.102f7820 ()
        ebx = eax
        esp += 4
        dword [arg_ch] = ebx
        v = ebx & ebx
        if (v) goto loc_0x10121e7e // unlikely
        goto loc_0x10121e6c;
    loc_0x10121e7e:
        // CODE XREF from fcn.10121e10 @ 0x10121e6a(x)
        push (esi)
        push (0)
        push (ebx)
        sub.MSVCR110.dll_memset ()
        push (esi)
        push (0)
        push (ebx)
        sub.MSVCR110.dll_memset ()
        esi = dword [edi]
        eax = dword [edi + 4]
        eax -= esi
        eax >>= 2
        esp += 0x18
        v = eax & eax
        if (!v) goto loc_0x10121ef2 // likely
        goto loc_0x10121ea1;
    loc_0x10121ef2:
        // CODE XREFS from fcn.10121e10 @ 0x10121e9f(x), 0x10121ea9(x)
        bl = 1
        
    loc_0x10121ef4:
        // CODE XREF from fcn.10121e10 @ 0x10121ee5(x)
        eax = dword [arg_ch]
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x10121f0b // likely
        goto loc_0x10121f02;
        goto loc_0x10121eab;
    loc_0x10121e6c: // orphan
         push (esi)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ebx = eax
         
         goto loc_0x10121e79;
    loc_0x10121e79: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121e58(x)
         ebx = 0

    loc_0x10121e7b: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121e77(x)
         dword [arg_ch] = ebx

    loc_0x10121ea1: // orphan
         eax = esi + eax*4
         dword [var_10h] = eax
         v = esi - eax
         jae 0x10121ef2           // likely

         goto loc_0x10121eab;
    loc_0x10121eab: // orphan
         
    loc_0x10121eb0: // orphan
         // CODE XREFS from fcn.10121e10 @ 0x10121eab(x), 0x10121ef0(x)
         ecx = dword [arg_8h]
         edx = ebx
         eax = dword [ecx]
         ecx = dword [ecx + 4]
         ecx -= eax
         ecx >>= 2
         v = ecx & ecx
         if (!v) 
         goto loc_0x10121ec3;
    loc_0x10121ec3: // orphan
         edi = eax + ecx*4
         v = eax - edi
         jae 0x10121ee3           // likely

         goto loc_0x10121eca;
    loc_0x10121eca: // orphan
         ebx = ebx

    loc_0x10121ed0: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121ee1(x)
         v = byte [edx] - 0
         if (v) 
         goto loc_0x10121ed5;
    loc_0x10121ed5: // orphan
         ecx = dword [eax]
         v = ecx - dword [esi]
         if (!v) 
         goto loc_0x10121edb;
    loc_0x10121edb: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121ed3(x)
         eax += 4
         edx++
         v = eax - edi
         if (((unsigned) v) < 0) 
         goto loc_0x10121ee3;
    loc_0x10121ee3: // orphan
         // CODE XREFS from fcn.10121e10 @ 0x10121ec1(x), 0x10121ec8(x)
         bl = 0
         
         goto loc_0x10121ee7;
    loc_0x10121ee7: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121ed9(x)
         esi += 4
         byte [edx] = 1
         v = esi - dword [var_10h]
         if (((unsigned) v) < 0) 
         goto loc_0x10121ef2;
    loc_0x10121f02: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10121f0b: // orphan
         // CODE XREF from fcn.10121e10 @ 0x10121f00(x)
         al = bl
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

