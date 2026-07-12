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
void fcn.100b9ff0 (int32_t arg1, int32_t arg2) {
        // CALL XREF from method.Fight.virtual_328 @ 0x100b6165(x)
        // CALL XREF from fcn.100b9e50 @ 0x100b9e5c(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031ed98)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x28
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch  // int32_t arg1
        dword fs:[0] = eax
        edi = ecx     // arg3
        dword [var_28h] = edi
        ebx = 0       // int32_t arg2
        esi = 0
        dword [var_34h] = ebx
        dword [var_30h] = esi
        dword [var_2ch] = ebx
        ecx = edi + 0x194
        dword [var_4h] = esi
        byte [edi + 0x6a4] = 0 // [0x6a4:1]=255
        fcn.101a0b30 () // fcn.101a0b30(0x177ff0, 0x0)
        eax = dword [edi + 0x1fc]
        eax -= dword [edi + 0x1f8]
        eax >>= 2
        v = eax & eax
        if (!v) goto loc_0x100ba181 // likely
        goto loc_0x100ba056;
    loc_0x100ba181:
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba050(x), 0x100ba06f(x)
        v = byte [edi + 0x6a4] - 0
        if (!v) goto loc_0x100ba190 // likely
        goto loc_0x100ba18a;
    loc_0x100ba190:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba188(x)
        esi -= ebx
        esi >>= 2
        v = esi - 1   // 1
        if (((unsigned) v) <= 0) goto 0x100ba1a5 // likely
        goto loc_0x100ba19a;
    loc_0x100ba1a5:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba198(x)
        eax = dword [edi + 0x208]
        eax -= dword [edi + 0x204]
        eax >>= 2
        v = eax & eax
        if (!v) goto loc_0x100ba2f6 // likely
        goto loc_0x100ba1bc;
    loc_0x100ba2f6:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba1b6(x)
        v = byte [edi + 0x728] - 0
        if (!v) goto loc_0x100ba337 // unlikely
        goto loc_0x100ba2ff;
    loc_0x100ba337:
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba2fd(x), 0x100ba306(x)
        eax = dword [edi + 0x46c]
        al = byte [eax + 0xd5]
        v = al & al
        if (v) goto loc_0x100ba392 // likely
        goto loc_0x100ba347;
    loc_0x100ba392:
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba345(x), 0x100ba36a(x), 0x100ba37c(x)
        v = byte [edi + 0x710] - 0
        if (!v) goto loc_0x100ba3b5 // unlikely
        goto loc_0x100ba39b;
    loc_0x100ba3b5:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba399(x)
        ecx = edi + 0x12c
        fcn.102087f0 () // fcn.102087f0(0x0, 0x0)
        v = byte [edi + 0x457] - 0
        if (!v) goto loc_0x100ba4b2 // unlikely
        goto loc_0x100ba3cd;
    loc_0x100ba4b2:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba3c7(x)
        v = byte [edi + 0x690] - 0
        if (!v) goto loc_0x100ba4c9 // unlikely
        goto loc_0x100ba4bb;
    loc_0x100ba4c9:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba4b9(x)
        ecx = dword [edi + 0x128]
        fcn.100d4fa0 () // fcn.100d4fa0(0x0)
        ecx = eax
        fcn.10047110 () // fcn.10047110(0x0)
        ecx = edi
        fcn.100bafe0 () // fcn.100bafe0(0x0, 0x0)
        ecx = dword [edi + 0x124]
        fcn.101a6c20 () // fcn.101a6c20(0x0, 0x0)
        ecx = edi
        fcn.100ba610 () // fcn.100ba610(0x0, 0x0)
        eax = dword [edi + 0x214]
        eax -= dword [edi + 0x210]
        eax >>= 2
        v = eax & eax
        if (!v) goto loc_0x100ba535 // likely
        goto loc_0x100ba507;
    loc_0x100ba535:
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba505(x), 0x100ba51a(x)
        eax = dword [edi + 0x214]
        ecx = dword [edi + 0x210]
        v = eax - eax
        if (!v) goto loc_0x100ba557 // likely
        goto loc_0x100ba545;
    loc_0x100ba557:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba543(x)
        dword [edi + 0x214] = ecx
        eax = dword [edi + 0x1fc]
        eax -= dword [edi + 0x1f8]
        eax >>= 2
        v = eax & eax
        if (!v) goto loc_0x100ba599 // likely
        goto loc_0x100ba570;
    loc_0x100ba599:
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba56e(x), 0x100ba583(x)
        dword [edi + 0x708]++ // [0x708:4]=-1 // 1
        fcn.10177380 () // fcn.10177380(0x0)
        ecx = eax
        fcn.10176df0 ()
        fstp dword [ebp - 0x28]
        movd xmm1 dword [edi + 0x708]
        movss xmm2 dword [edi + 0x70c]
        cvtdq2ps xmm1 xmm1
        xmm0 = xmm2
        divss xmm0 xmm1
        dword [var_4h] = 0xffffffff // -1
        subss xmm2 xmm0
        movss xmm0 dword [var_28h]
        divss xmm0 xmm1
        addss xmm2 xmm0
        movss dword [edi + 0x70c] xmm2 // [0x70c:4]=-1 // 1804
        v = ebx & ebx
        if (!v) goto loc_0x100ba5f5 // likely
        goto loc_0x100ba5ec;
    loc_0x100ba5f5:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba5ea(x)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x100ba585;
        goto loc_0x100ba51c;
        goto loc_0x100ba3e0;
        goto loc_0x100ba36c;
        goto loc_0x100ba308;
        goto loc_0x100ba1db;
        goto loc_0x100ba075;
        return eax;
    loc_0x100ba075:
        // CODE XREF from fcn.100b9ff0 @ 0x100ba178(x)
        ecx = eax
        fcn.10159df0 () // fcn.10159df0(0x0)
        ecx = dword [var_10h]
        bl = al
        fcn.1015aab0 () // fcn.1015aab0(0x0)
        v = bl & bl
        if (!v) goto loc_0x100ba167 // likely
        goto loc_0x100ba08e;
    while (((unsigned) v) < 0) {
        ebx = dword [var_10h]
        ecx = ebx
        fcn.10159770 () // fcn.10159770(0x0)
        ecx = eax
        fcn.1002c4c0 () // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
        v = eax & eax
        ecx = eax
        fcn.10293140 () // fcn.10293140(0x0)
        v = eax - dword [edi + 0x120]
        v = esi - dword [var_2ch]
        if (!v) goto loc_0x100ba0cd // likely
        v = esi & esi
        if (!v) goto loc_0x100ba0c5 // likely
        dword [esi] = ebx
        // CODE XREF from fcn.100b9ff0 @ 0x100ba0c1(x)
        esi += 4
        goto loc_0x100ba164
        // CODE XREF from fcn.100b9ff0 @ 0x100ba0bd(x)
        edx = esi
        edx -= dword [var_34h]
        eax = var_1ch
        ecx = edx
        ecx >>= 2
        v = ecx - 1   // 1
        ebx = var_20h
        cmovae eax ebx
        dword [var_1ch] = 1
        dword [var_20h] = ecx
        eax = dword [eax]
        eax += ecx
        dword [var_14h] = edx
        dword [var_1ch] = eax
        if (!v) goto loc_0x100ba120 // unlikely
        eax <<<= 2
        push (eax)
        dword [var_20h] = eax
        fcn.102f7820 ()
        ebx = eax
        esp += 4
        v = ebx & ebx
        if (v) goto loc_0x100ba11b // unlikely
        push (dword [var_20h])
        fcn.102e3ef0 () // fcn.102e3ef0(0x0)
        esp += 4
        ebx = eax
        // CODE XREF from fcn.100b9ff0 @ 0x100ba10c(x)
        edx = dword [var_14h]
        goto loc_0x100ba122
        // CODE XREF from fcn.100b9ff0 @ 0x100ba0f7(x)
        ebx = 0
        // CODE XREF from fcn.100b9ff0 @ 0x100ba11e(x)
        eax = dword [var_34h]
        v = esi - eax
        if (v) goto loc_0x100ba12d // unlikely
        eax = ebx
        goto loc_0x100ba140
        // CODE XREF from fcn.100b9ff0 @ 0x100ba127(x)
        push (edx)
        push (eax)
        push (ebx)
        sub.MSVCR110.dll_memmove ()
        esp += 0xc
        eax += dword [var_14h]
        goto loc_0x100ba140
        // CODE XREFS from fcn.100b9ff0 @ 0x100ba12b(x), 0x100ba13b(x)
        ecx = dword [var_10h]
        dword [eax] = ecx
        esi = eax + 4
        eax = dword [var_34h]
        v = eax & eax
        if (!v) goto loc_0x100ba158 // likely
        push (eax)
        fcn.102f7780 () // fcn.102f7780(0x0)
        esp += 4
        // CODE XREF from fcn.100b9ff0 @ 0x100ba14d(x)
        eax = dword [var_1ch]
        dword [var_34h] = ebx
        eax = ebx + eax*4
        dword [var_2ch] = eax
        // CODE XREF from fcn.100b9ff0 @ 0x100ba0c8(x)
        dword [var_30h] = esi
    }
    loc_0x100ba17e:
        ebx = dword [var_34h]
            goto loc_0x100ba08e;
        goto loc_0x100ba0a7;
        return eax;
    loc_0x100ba18a: // orphan
         dword [edi + 0x720]++    // [0x720:4]=0 // 1

    loc_0x100ba19a: // orphan
         eax = var_34h            // int32_t arg1
         push (eax)
         ecx = edi                // int32_t arg_8h
         fcn.100b5310 ()          // fcn.100b5310(0x177fcc, 0x0, 0x0)

    loc_0x100ba1bc: // orphan
         ebx = dword [edi + 0x204]
         ecx = dword [edi + 0x208]
         eax = dword [ebx]
         dword [var_14h] = ebx
         dword [var_10h] = eax
         dword [var_1ch] = ecx
         v = ebx - ecx
         jae 0x100ba2cb           // likely

         goto loc_0x100ba1db;
    loc_0x100ba1db: // orphan
         
    loc_0x100ba1e0: // orphan
         // CODE XREFS from fcn.100b9ff0 @ 0x100ba1db(x), 0x100ba2c5(x)
         ecx = eax
         fcn.1015aab0 ()          // fcn.1015aab0(0x0)
         esi = dword [edi + 0x1fc]
         v = esi - dword [edi + 0x200]
         if (!v) 
         goto loc_0x100ba1f5;
    loc_0x100ba1f5: // orphan
         v = esi & esi
         if (!v) 
         goto loc_0x100ba1f9;
    loc_0x100ba1f9: // orphan
         ecx = dword [var_10h]
         dword [esi] = ecx

    loc_0x100ba1fe: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba1f7(x)
         dword [edi + 0x1fc] += 4
         
         goto loc_0x100ba20a;
    loc_0x100ba20a: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba1f3(x)
         ecx = esi
         ecx -= dword [edi + 0x1f8]
         eax = var_24h
         ecx >>= 2
         v = ecx - 1              // 1
         edx = var_20h
         cmovae eax edx
         dword [var_24h] = 1
         dword [var_20h] = ecx
         eax = dword [eax]
         eax += ecx
         dword [var_18h] = eax
         if (!v) 
         goto loc_0x100ba234;
    loc_0x100ba234: // orphan
         eax <<<= 2
         push (eax)
         fcn.102f7820 ()
         ebx = eax
         esp += 4
         v = ebx & ebx
         if (v) 
         goto loc_0x100ba246;
    loc_0x100ba246: // orphan
         ecx = dword [var_18h]
         eax = ecx*4
         push (eax)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x4)
         esp += 4
         ebx = eax
         
         goto loc_0x100ba25d;
    loc_0x100ba25d: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba232(x)
         ebx = 0

    loc_0x100ba25f: // orphan
         // CODE XREFS from fcn.100b9ff0 @ 0x100ba244(x), 0x100ba25b(x)
         eax = dword [edi + 0x1f8]
         v = esi - eax
         if (v) 
         goto loc_0x100ba269;
    loc_0x100ba269: // orphan
         eax = ebx
         
         goto loc_0x100ba26d;
    loc_0x100ba26d: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba267(x)
         esi -= eax
         push (esi)
         push (eax)
         push (ebx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += esi
         esp = esp

    loc_0x100ba280: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba26b(x)
         ecx = dword [var_10h]
         dword [eax] = ecx
         esi = eax + 4
         eax = dword [edi + 0x1f8]
         v = eax & eax
         if (!v) 
         goto loc_0x100ba292;
    loc_0x100ba292: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100ba29b: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba290(x)
         eax = dword [var_18h]
         dword [edi + 0x1f8] = ebx
         eax = ebx + eax*4
         ebx = dword [var_14h]
         dword [edi + 0x1fc] = esi
         dword [edi + 0x200] = eax

    loc_0x100ba2b6: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba205(x)
         eax = dword [ebx + 4]
         ebx += 4
         dword [var_14h] = ebx
         dword [var_10h] = eax
         v = ebx - dword [var_1ch]
         if (((unsigned) v) < 0) 
         goto loc_0x100ba2cb;
    loc_0x100ba2cb: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba1d5(x)
         eax = dword [edi + 0x208]
         ecx = dword [edi + 0x204]
         v = eax - eax
         if (!v) 
         goto loc_0x100ba2db;
    loc_0x100ba2db: // orphan
         esi = eax
         esi -= eax
         push (esi)
         push (eax)
         push (ecx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         ecx = esi + eax

    loc_0x100ba2ed: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba2d9(x)
         ebx = dword [var_34h]
         dword [edi + 0x208] = ecx

    loc_0x100ba2ff: // orphan
         v = byte [edi + 0x725] - 1
         if (!v) 
         goto loc_0x100ba308;
    loc_0x100ba308: // orphan
         byte [edi + 0x725] = 1   // [0x725:1]=255 // 1
         fcn.100edc50 ()
         push (dword [eax])
         fcn.100ed700 ()          // fcn.100ed700(0x0)
         esp += 4
         v = byte [edi + 0x725] - 0
         push (0)
         ecx = edi
         if (!v) 
         goto loc_0x100ba32b;
    loc_0x100ba32b: // orphan
         byte [edi + 0x728] = 0   // [0x728:1]=255

    loc_0x100ba332: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba329(x)
         fcn.100b5490 ()          // fcn.100b5490(0x0, 0x0, 0x0)

    loc_0x100ba347: // orphan
         ecx = edi
         fcn.100b9ef0 ()          // fcn.100b9ef0(0x0, 0x0)
         ecx = edi + 0x12c
         fcn.10209400 ()          // fcn.10209400(0x0, 0x0)
         eax = dword [edi + 0x1fc]
         eax -= dword [edi + 0x1f8]
         eax >>= 2
         v = eax & eax
         if (!v) 
         goto loc_0x100ba36c;
    loc_0x100ba36c: // orphan
         esi = dword [edi + 0x1f8]
         eax = dword [edi + 0x1fc]
         ecx = dword [esi]
         v = esi - eax
         jae 0x100ba392           // likely

         goto loc_0x100ba37e;
    loc_0x100ba37e: // orphan
         ebx = eax

    loc_0x100ba380: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba38d(x)
         fcn.1015acd0 ()          // fcn.1015acd0(0x0, 0x0)
         ecx = dword [esi + 4]
         esi += 4
         v = esi - ebx
         if (((unsigned) v) < 0) 
         goto loc_0x100ba38f;
    loc_0x100ba38f: // orphan
         ebx = dword [var_34h]

    loc_0x100ba39b: // orphan
         push (dword [edi + 0x604])
         ecx = edi
         fcn.100b5690 ()          // fcn.100b5690(0x0, 0x0)
         push (dword [edi + 0x608])
         ecx = edi
         fcn.100b5690 ()          // fcn.100b5690(0x0, 0x0)

    loc_0x100ba3cd: // orphan
         push (1)                 // 1
         ecx = edi
         fcn.100bd2e0 ()          // fcn.100bd2e0(0x0, 0x0)
         ecx = dword [edi + 0x190]
         v = ecx & ecx
         if (!v) 
         goto loc_0x100ba3e0;
    loc_0x100ba3e0: // orphan
         eax = edi + 0x614        // int32_t arg1
         push (eax)
         push (3)                 // 3
         push (1)                 // 1
         fcn.101facb0 ()          // fcn.101facb0(0x614, 0x0, 0x0, 0x0, 0x0)

    loc_0x100ba3f0: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba3de(x)
         esi = edi + 0x250
         ecx = esi
         dword [var_14h] = esi
         fcn.10070840 ()          // fcn.10070840(0x0, 0x0)
         ecx = esi
         fcn.10070960 ()          // fcn.10070960(0x0, 0x0)
         eax = dword [edi + 0x1fc]
         eax -= dword [edi + 0x1f8]
         eax >>= 2
         v = eax & eax
         if (!v) 
         goto loc_0x100ba41a;
    loc_0x100ba41a: // orphan
         eax = dword [edi + 0x1f8]
         edx = dword [edi + 0x1fc]
         ecx = dword [eax]
         dword [var_18h] = ecx
         dword [var_20h] = edx
         v = eax - edx
         jae 0x100ba47a           // likely

         goto loc_0x100ba432;
    loc_0x100ba432: // orphan
         ebx = eax
         edi = esi

    loc_0x100ba436: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba46f(x)
         fcn.10159770 ()          // fcn.10159770(0x0)
         ecx = eax
         fcn.10047600 ()          // fcn.10047600(0x0)
         ecx = dword [var_18h]
         esi = eax
         fcn.10159800 ()          // fcn.10159800(0x0)
         push (eax)
         push (esi)
         ecx = edi
         fcn.10070980 ()          // fcn.10070980(0x0)
         ecx = eax
         fcn.101e1cd0 ()          // fcn.101e1cd0(0x0)
         ecx = eax                // int32_t arg_ch
         fcn.10293760 ()          // fcn.10293760(0x0, 0x0, 0x0)
         ecx = dword [ebx + 4]
         ebx += 4
         dword [var_18h] = ecx
         v = ebx - dword [var_20h]
         if (((unsigned) v) < 0) 
         goto loc_0x100ba471;
    loc_0x100ba471: // orphan
         edi = dword [var_28h]
         ebx = dword [var_34h]
         esi = dword [var_14h]

    loc_0x100ba47a: // orphan
         // CODE XREFS from fcn.100b9ff0 @ 0x100ba418(x), 0x100ba430(x)
         ecx = esi
         fcn.10070980 ()          // fcn.10070980(0x0)
         ecx = eax
         fcn.101e1ca0 ()          // fcn.101e1ca0(0x0)
         ecx = eax
         fcn.100a98c0 ()          // fcn.100a98c0(0x0, 0x0)
         ecx = esi
         fcn.10070980 ()          // fcn.10070980(0x0)
         ecx = eax
         fcn.101e1cb0 ()          // fcn.101e1cb0(0x0)
         ecx = eax
         fcn.100a98c0 ()          // fcn.100a98c0(0x0, 0x0)
         ecx = esi
         fcn.10070980 ()          // fcn.10070980(0x0)
         ecx = eax
         fcn.101e2840 ()          // fcn.101e2840(0x0)

    loc_0x100ba4bb: // orphan
         ecx = edi
         byte [edi + 0x690] = 0   // [0x690:1]=0
         fcn.100bc6a0 ()          // fcn.100bc6a0(0x0, 0x0)

    loc_0x100ba507: // orphan
         esi = dword [edi + 0x210]
         ecx = dword [edi + 0x214]
         eax = dword [esi]
         dword [var_28h] = ecx
         v = esi - ecx
         jae 0x100ba535           // likely

         goto loc_0x100ba51c;
    loc_0x100ba51c: // orphan
         ebx = ecx
         edi = edi

    loc_0x100ba520: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba530(x)
         push (eax)
         ecx = edi                // int32_t arg_8h
         fcn.100b9bc0 ()          // fcn.100b9bc0(0x0, 0x0, 0x0)
         eax = dword [esi + 4]
         esi += 4
         v = esi - ebx
         if (((unsigned) v) < 0) 
         goto loc_0x100ba532;
    loc_0x100ba532: // orphan
         ebx = dword [var_34h]

    loc_0x100ba545: // orphan
         esi = eax
         esi -= eax
         push (esi)
         push (eax)
         push (ecx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         ecx = esi + eax

    loc_0x100ba570: // orphan
         esi = dword [edi + 0x1f8]
         eax = dword [edi + 0x1fc]
         ecx = dword [esi]
         dword [var_28h] = eax
         v = esi - eax
         jae 0x100ba599           // likely

         goto loc_0x100ba585;
    loc_0x100ba585: // orphan
         ebx = eax

    loc_0x100ba587: // orphan
         // CODE XREF from fcn.100b9ff0 @ 0x100ba594(x)
         fcn.1015b040 ()          // fcn.1015b040(0x0)
         ecx = dword [esi + 4]
         esi += 4
         v = esi - ebx
         if (((unsigned) v) < 0) 
         goto loc_0x100ba596;
    loc_0x100ba596: // orphan
         ebx = dword [var_34h]

    loc_0x100ba5ec: // orphan
         push (ebx)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

