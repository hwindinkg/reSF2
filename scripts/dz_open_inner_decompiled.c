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
void fcn.102c9778 (int32_t arg1, int32_t arg2, int32_t arg_8h, uint32_t arg_ch, int32_t arg_10h) {
        // CALL XREF from fcn.102c9bfc @ 0x102c9c0c(x)
        push (ebp)
        ebp = esp
        esp -= 0x10c
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_4h] = eax
        eax = dword [arg_10h]
        push (ebx)    // arg2
        ebx = dword [arg_8h]
        push (edi)
        edi = arg_ch  // arg3
        ecx = 0
        dword [var_108h] = ebx
        dword [edi + 0x7c] = ecx
        dword [edi + 0x78] = ecx
        dword [edi + 0x84] = ecx
        dword [edi + 0x8c] = ecx
        dword [edi + 0x9c] = ecx
        v = byte [arg_ch] - cl
        if (!v) goto loc_0x102c97cf // likely
        goto loc_0x102c97ba;
    loc_0x102c97cf:
        // CODE XREF from fcn.102c9778 @ 0x102c97b8(x)
        al = byte [ebx]
        ecx = var_104h
        byte [var_104h] = al
        
    while (al & al) {
        // CODE XREF from fcn.102c9778 @ 0x102c97ef(x)
        ebx++
        v = byte [ecx] - 0x2f // '/'
        if (v) goto loc_0x102c97e8 // likely
        byte [ecx] = 0x5c // '\\' // [0x5c:1]=255 // 92
        // CODE XREF from fcn.102c9778 @ 0x102c97e3(x)
        al = byte [ebx]
        ecx++
        byte [ecx] = al
    }
    loc_0x102c97f1:
        ebx = var_104h
        eax = ebx
        push (0x10375008) // "rb" // (pstr 0x10375008) "rb"
        push (eax)
        dword [var_108h] = ebx
        byte [0x10667292] = 1 // [0x10667292:1]=0
        fcn.102fbf50 () // fcn.102fbf50(0x177efc, 0x177efc)
        dword [edi + 0x9c] = eax
        eax = 0
        byte [0x10667292] = al // [0x10667292:1]=1
        
    loc_0x102c981e:
        // CODE XREF from fcn.102c9778 @ 0x102c97cd(x)
        eax = dword [edi + 0x9c]
        ecx = pop ()
        ecx = pop ()
        v = eax & eax
        if (v) goto loc_0x102c9832 // likely
        goto loc_0x102c982a;
    loc_0x102c97ba:
        push (eax)
        push (ebx)
        dword [edi + 0x88] = ebx
        fcn.102fc060 () // fcn.102fc060(0x0, 0x0)
        dword [edi + 0x9c] = eax
        goto loc_0x102c981e
    loc_0x102c9832: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9828(x)
         push (esi)
         push (eax)
         push (1)                 // 1
         esi = edi + 0x68
         push (9)                 // 9 // "\t\x01"
         push (esi)               // "h\t\x01"
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
         esp += 0x10
         v = eax - 1              // 1
         if (!v) 
         goto loc_0x102c9849;
    loc_0x102c9849: // orphan
         // CODE XREFS from fcn.102c9778 @ 0x102c9908(x), 0x102c9975(x), 0x102c99ad(x), 0x102c9a17(x)
         push (2)                 // 2
         
         goto loc_0x102c9850;
    loc_0x102c9850: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9847(x)
         v = byte [edi + 0x70] - 0
         if (((unsigned) v) > 0) 
         goto loc_0x102c985a;
    loc_0x102c985a: // orphan
         v = dword [esi] - 0x5a525444 // 'DTRZ'
         if (v) 
         goto loc_0x102c9866;
    loc_0x102c9866: // orphan
         ecx = 0
         v = word [edi + 0x6e] - cx
         if (!v) 
         goto loc_0x102c9872;
    loc_0x102c9872: // orphan
         eax = word [edi + 0x6c]
         esi = ecx
         v = eax & eax
         if (!v) 
         goto loc_0x102c987c;
    loc_0x102c987c: // orphan
         ebx = eax

    loc_0x102c987e: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9892(x)
         push (dword [edi + 0x9c])
         esi++
         fcn.102ff730 ()          // fcn.102ff730(0x0)
         ecx = pop ()
         v = eax & eax
         if (v) 
         goto loc_0x102c988f;
    loc_0x102c988f: // orphan
         ebx--

    loc_0x102c9890: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c988d(x)
         v = ebx & ebx
         if (v) 
         goto loc_0x102c9894;
    loc_0x102c9894: // orphan
         ebx = dword [var_108h]

    loc_0x102c989a: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c987a(x)
         ecx = ebx
         edx = ecx + 1

    loc_0x102c989f: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c98a4(x)
         al = byte [ecx]
         ecx++
         v = al & al
         if (v) 
         goto loc_0x102c98a6;
    loc_0x102c98a6: // orphan
         ecx -= edx
         eax = ecx + 1
         push (eax)
         fcn.102c96db ()
         ecx = pop ()
         ecx = eax
         dword [edi + 0x84] = ecx
         edx = ebx

    loc_0x102c98bc: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c98c4(x)
         al = byte [edx]
         edx++
         byte [ecx] = al
         ecx++
         v = al & al
         if (v) 
         goto loc_0x102c98c6;
    loc_0x102c98c6: // orphan
         push (esi)
         fcn.102c96db ()
         ecx = pop ()
         dword [edi + 0x78] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x102c98d8;
    loc_0x102c98d8: // orphan
         eax = esi
         push (1)                 // 1
         eax ~= eax
         push (eax)
         push (dword [edi + 0x9c])
         fcn.102ff760 ()          // fcn.102ff760(0x0, 0x0, 0x0)
         esp += 0xc
         v = esi & esi
         if (!v) 
         goto loc_0x102c98f1;
    loc_0x102c98f1: // orphan
         push (dword [edi + 0x9c])
         push (1)                 // 1
         push (esi)
         push (dword [edi + 0x78])
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
         esp += 0x10
         v = eax - 1              // 1
         if (v) 
         goto loc_0x102c990e;
    loc_0x102c990e: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c98ef(x)
         eax = word [edi + 0x6e]
         push (0)
         esi = pop ()             // ebp
         eax--
         if (!v) 
         goto loc_0x102c9918;
    loc_0x102c9918: // orphan
         ebx = eax

    loc_0x102c991a: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c992e(x)
         push (dword [edi + 0x9c])
         esi++
         fcn.102ff730 ()          // fcn.102ff730(0x0)
         ecx = pop ()
         v = eax & eax
         if (v) 
         goto loc_0x102c992b;
    loc_0x102c992b: // orphan
         ebx--

    loc_0x102c992c: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9929(x)
         v = ebx & ebx
         if (v) 
         goto loc_0x102c9930;
    loc_0x102c9930: // orphan
         ebx = dword [var_108h]
         v = esi & esi
         if (!v) 
         goto loc_0x102c993a;
    loc_0x102c993a: // orphan
         push (esi)
         fcn.102c96db ()
         ecx = pop ()
         dword [edi + 0x7c] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x102c994c;
    loc_0x102c994c: // orphan
         eax = esi
         push (1)                 // 1
         eax ~= eax
         push (eax)
         push (dword [edi + 0x9c])
         fcn.102ff760 ()          // fcn.102ff760(0x0, 0x0, 0x0)
         push (dword [edi + 0x9c])
         push (1)                 // 1
         push (esi)
         push (dword [edi + 0x7c])
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
         esp += 0x1c
         v = eax - 1              // 1
         if (v) 
         goto loc_0x102c997b;
    loc_0x102c997b: // orphan
         // CODE XREFS from fcn.102c9778 @ 0x102c9916(x), 0x102c9938(x)
         esi = word [edi + 0x6c]
         eax = 0
         dword [var_108h] = eax
         v = esi & esi
         if (!v) 
         goto loc_0x102c998b;
    loc_0x102c998b: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c99ca(x)
         push (dword [edi + 0x9c])
         eax++
         push (1)                 // 1
         dword [var_108h] = eax
         eax = var_10ch
         push (2)                 // 2
         push (eax)
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x177ef4, 0x0, 0x0, 0x0)
         esp += 0x10
         v = al - 1               // 1
         if (v) 
         goto loc_0x102c99b3;
    loc_0x102c99b3: // orphan
         eax = 0xffff
         v = word [var_10ch] - ax
         if (v) 
         goto loc_0x102c99c1;
    loc_0x102c99c1: // orphan
         esi--

    loc_0x102c99c2: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c99bf(x)
         eax = dword [var_108h]
         v = esi & esi
         if (v) 
         goto loc_0x102c99cc;
    loc_0x102c99cc: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9989(x)
         eax ~= eax
         push (1)                 // 1
         eax += eax
         push (eax)
         push (dword [edi + 0x9c])
         fcn.102ff760 ()          // fcn.102ff760(0x0, 0x0, 0x0)
         eax = dword [var_108h]
         esi = eax + eax
         push (esi)
         fcn.102c96db ()
         esp += 0x10
         dword [edi + 0x8c] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c99fa;
    loc_0x102c99fa: // orphan
         // CODE XREFS from fcn.102c9778 @ 0x102c98d2(x), 0x102c9946(x)
         eax = 0
         eax++
         
         goto loc_0x102c9a02;
    loc_0x102c9a02: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c99f8(x)
         push (dword [edi + 0x9c])
         push (1)                 // 1
         push (esi)
         push (eax)
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
         esp += 0x10
         v = eax - 1              // 1
         if (v) 
         goto loc_0x102c9a1d;
    loc_0x102c9a1d: // orphan
         eax = edi + 0xa0
         esi = edi + 4
         push (eax)
         ecx = esi
         fcn.102ca5a9 ()          // fcn.102ca5a9(0xa0, 0x0)
         dword [edi] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c9a38;
    loc_0x102c9a38: // orphan
         eax = edi + 0x414
         push (eax)
         ecx = esi
         fcn.102ca5a9 ()          // fcn.102ca5a9(0x414, 0x0)
         dword [edi] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c9a50;
    loc_0x102c9a50: // orphan
         eax = edi + 0x440
         push (eax)
         ecx = esi
         fcn.102ca5a9 ()          // fcn.102ca5a9(0x440, 0x0)
         dword [edi] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c9a64;
    loc_0x102c9a64: // orphan
         eax = edi + 0x45c
         push (eax)
         ecx = esi
         fcn.102ca5a9 ()          // fcn.102ca5a9(0x45c, 0x0)
         dword [edi] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c9a78;
    loc_0x102c9a78: // orphan
         push (0x102c9b50)
         push (dword [edi + 0x9c])
         ecx = esi
         fcn.102ca66b ()          // fcn.102ca66b(0x0, 0x0, 0x0, 0x0)
         dword [edi] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102c9a90;
    loc_0x102c9a90: // orphan
         push (0x5c)              // '\\' // 92 // "\"
         push (ebx)
         sub.MSVCR110.dll_strrchr ()
         ecx = pop ()
         ecx = pop ()             // "\"
         dword [var_108h] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x102c9aa4;
    loc_0x102c9aa4: // orphan
         esi = eax
         esi -= ebx
         ecx = esi + 1
         push (ecx)
         fcn.102c96db ()
         push (esi)
         push (ebx)
         push (eax)
         dword [edi + 0x80] = eax
         sub.MSVCR110.dll_strncpy ()
         eax = dword [edi + 0x80]
         ecx = dword [var_108h]
         esp += 0x10
         eax -= ebx
         byte [eax + ecx] = 0

    loc_0x102c9ad4: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c9aa2(x)
         byte [edi + 0x74] = 1
         eax = 0
         
         goto loc_0x102c9adc;
    loc_0x102c9adc: // orphan
         // CODE XREFS from fcn.102c9778 @ 0x102c9854(x), 0x102c9860(x), 0x102c986c(x)
         push (3)                 // 3

    loc_0x102c9ade: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c984b(x)
         eax = pop ()

    loc_0x102c9adf: // orphan
         // XREFS: CODE 0x102c99fd  CODE 0x102c9a32  CODE 0x102c9a4a   // XREFS: CODE 0x102c9a62  CODE 0x102c9a76  CODE 0x102c9a8e   // XREFS: CODE 0x102c9ada  
         esi = pop ()

    loc_0x102c9ae0: // orphan
         // CODE XREF from fcn.102c9778 @ 0x102c982d(x)
         ecx = dword [var_4h]
         edi = pop ()
         ecx ^= ebp
         ebx = pop ()
         fcn.102fff32 ()          // fcn.102fff32(0x0)
         leave                    // ebp
         return

}

