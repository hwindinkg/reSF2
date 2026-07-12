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
void method.ModelContainer.virtual_328 (int32_t arg1, int32_t arg2) {
        push (ebp)
        ebp = esp
        esp -= 0x14
        push (esi)
        esi = ecx     // arg3
        v = byte [esi + 0x2c8] - 0
        if (!v) goto loc_0x10167a01 // unlikely
        goto loc_0x10167816;
    loc_0x10167a01:
        // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167810(x)
        esi = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x1016782d;
        return eax;
    loc_0x1016782d:
        eax = dword [esi + 0x33c]
        ebx = dword [edi]
        dword [var_ch] = eax
        v = edi - eax
        jae 0x1016787e // unlikely
        goto loc_0x1016783c;
    loc_0x1016787e:
        // CODE XREFS from method.ModelContainer.virtual_328 @ 0x1016782b(x), 0x1016783a(x)
        ebx = dword [esi + 0x344]
        eax = dword [esi + 0x348]
        eax -= ebx
        eax >>= 2
        dword [var_ch] = ebx
        v = eax & eax
        if (!v) goto loc_0x101679c2 // likely
        goto loc_0x1016789a;
    loc_0x101679c2:
        // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167894(x)
        ecx = esi + 0x2cc
        fcn.102087f0 () // fcn.102087f0(0x0, 0x0)
        ecx = dword [esi + 0x334]
        fcn.101e31b0 () // fcn.101e31b0(0x0)
        ecx = eax
        fcn.100a98c0 () // fcn.100a98c0(0x0, 0x0)
        ecx = dword [esi + 0x334]
        fcn.101e31c0 () // fcn.101e31c0(0x0)
        ecx = eax
        fcn.100a98c0 () // fcn.100a98c0(0x0, 0x0)
        ecx = esi
        fcn.101683c0 () // fcn.101683c0(0x0, 0x0)
        ecx = esi
        fcn.10168300 () // fcn.10168300(0x0, 0x0)
        edi = pop ()
        ebx = pop ()
            goto loc_0x1016789a;
    loc_0x1016789a:
        ecx = dword [esi + 0x348]
        eax = dword [ebx]
        dword [var_4h] = eax
        dword [var_14h] = ecx
        v = ebx - ecx
        jae 0x1016799b // unlikely
        goto loc_0x101678b0;
        return eax;
    loc_0x1016783c: // orphan
         esp = esp                // ebp

    loc_0x10167840: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x1016787c(x)
         v = byte [esi + 0x2c9] - 0
         if (v) 
         goto loc_0x10167849;
    loc_0x10167849: // orphan
         ecx = ebx
         fcn.10159310 ()          // fcn.10159310(0x0, 0x0)
         v = eax - 0xffffffff
         if (!v) 
         goto loc_0x10167855;
    loc_0x10167855: // orphan
         ecx = dword [esi + 0x334]
         byte [esi + 0x2c9] = 1
         eax = dword [ecx]
         push (1)                 // 1
         dword [eax + 0x84] ()    // 132 // 0x84(-1, 0x0, -1, 0x0)

    loc_0x1016786c: // orphan
         // CODE XREFS from method.ModelContainer.virtual_328 @ 0x10167847(x), 0x10167853(x)
         ecx = ebx
         fcn.1015aab0 ()          // fcn.1015aab0(0x0)
         ebx = dword [edi + 4]
         edi += 4
         v = edi - dword [var_ch]
         if (((unsigned) v) < 0) 
         goto loc_0x1016787e;
    loc_0x101678b0: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167995(x)
         ecx = eax
         fcn.1015aab0 ()          // fcn.1015aab0(0x0)
         edi = dword [esi + 0x33c]
         v = edi - dword [esi + 0x340]
         if (!v) 
         goto loc_0x101678c5;
    loc_0x101678c5: // orphan
         v = edi & edi
         if (!v) 
         goto loc_0x101678c9;
    loc_0x101678c9: // orphan
         ecx = dword [var_4h]
         dword [edi] = ecx

    loc_0x101678ce: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x101678c7(x)
         dword [esi + 0x33c] += 4
         
         goto loc_0x101678da;
    loc_0x101678da: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x101678c3(x)
         ecx = edi
         ecx -= dword [esi + 0x338]
         eax = var_8h
         ecx >>= 2
         v = ecx - 1              // 1
         edx = var_10h
         cmovae eax edx
         dword [var_8h] = 1
         dword [var_10h] = ecx
         eax = dword [eax]
         eax += ecx
         dword [var_8h] = eax
         if (!v) 
         goto loc_0x10167904;
    loc_0x10167904: // orphan
         eax <<<= 2
         push (eax)
         fcn.102f7820 ()
         ebx = eax
         esp += 4
         v = ebx & ebx
         if (v) 
         goto loc_0x10167916;
    loc_0x10167916: // orphan
         ecx = dword [var_8h]
         eax = ecx*4
         push (eax)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x4059e434)
         esp += 4
         ebx = eax
         
         goto loc_0x1016792d;
    loc_0x1016792d: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167902(x)
         ebx = 0

    loc_0x1016792f: // orphan
         // CODE XREFS from method.ModelContainer.virtual_328 @ 0x10167914(x), 0x1016792b(x)
         eax = dword [esi + 0x338]
         v = edi - eax
         if (v) 
         goto loc_0x10167939;
    loc_0x10167939: // orphan
         eax = ebx
         
         goto loc_0x1016793d;
    loc_0x1016793d: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167937(x)
         edi -= eax
         push (edi)
         push (eax)
         push (ebx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += edi
         esp = esp

    loc_0x10167950: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x1016793b(x)
         ecx = dword [var_4h]
         dword [eax] = ecx
         edi = eax + 4
         eax = dword [esi + 0x338]
         v = eax & eax
         if (!v) 
         goto loc_0x10167962;
    loc_0x10167962: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1016796b: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x10167960(x)
         eax = dword [var_8h]
         dword [esi + 0x338] = ebx
         eax = ebx + eax*4
         ebx = dword [var_ch]
         dword [esi + 0x33c] = edi
         dword [esi + 0x340] = eax

    loc_0x10167986: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x101678d5(x)
         eax = dword [ebx + 4]
         ebx += 4
         dword [var_ch] = ebx
         dword [var_4h] = eax
         v = ebx - dword [var_14h]
         if (((unsigned) v) < 0) 
         goto loc_0x1016799b;
    loc_0x1016799b: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x101678aa(x)
         ecx = dword [esi + 0x348]
         eax = dword [esi + 0x344]
         v = ecx - ecx
         if (!v) 
         goto loc_0x101679ab;
    loc_0x101679ab: // orphan
         edi = ecx
         edi -= ecx
         push (edi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += edi

    loc_0x101679bc: // orphan
         // CODE XREF from method.ModelContainer.virtual_328 @ 0x101679a9(x)
         dword [esi + 0x348] = eax

}

