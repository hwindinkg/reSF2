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
void fcn.101661d0 (int32_t arg1) {
        // CALL XREF from fcn.10164fa0 @ 0x10165124(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031dac0)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x58
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        esi = ecx     // arg3
        eax = dword [esi + 0x20]
        edi = dword [eax + 0x94]
        eax = dword [edi + 0xac]
        v = eax - 3   // 3
        if (((unsigned) v) > 0) goto case.default.0x1016620d // likely
    switch (eax) { // jump table of 4 cases at 0x10166648
        case 0: // 0x10166214
        case 1: // 0x10166214
            // CODE XREF from fcn.101661d0 @ 0x1016620d(x)
            dword [var_18h] = esi
            goto loc_0x1016623e
        case 2: // 0x10166221
            // CODE XREF from fcn.101661d0 @ 0x1016620d(x)
            eax = dword [esi + 0x48]
            dword [var_18h] = eax
            goto loc_0x1016623e
        case 3: // 0x10166219
            // CODE XREF from fcn.101661d0 @ 0x1016620d(x)
            eax = dword [esi + 0x44]
            dword [var_18h] = eax
            goto loc_0x1016623e
        default: // 0x10166229
            // CODE XREFS from fcn.101661d0 @ 0x1016620b(x), 0x1016620d(x)
            push (eax)
            push (str.ModelAnimation::getPlayerAnimation___unknown_type:__i) // 0x105b2228 // "ModelAnimation::getPlayerAnimation - unknown type: %i" // (pstr 0x105b2228) "ModelAnimation::getPlayerAnimation - unknown type: %i"
            fcn.101471b0 () // fcn.101471b0(0x0, 0x0)
            esp += 8
            dword [var_18h] = 0
            break;
    }
        switch (eax) { // jump table of 4 cases at 0x10166658
            case 0: // 0x10166250
            case 1: // 0x10166250
                // CODE XREF from fcn.101661d0 @ 0x10166249(x)
                dword [var_14h] = esi
                goto loc_0x10166272
            case 2: // 0x1016625a
                // CODE XREF from fcn.101661d0 @ 0x10166249(x)
                eax = dword [esi + 0x48]
                goto loc_0x1016626f
            case 3: // 0x10166255
                // CODE XREF from fcn.101661d0 @ 0x10166249(x)
                eax = dword [esi + 0x44]
                goto loc_0x1016626f
            default: // 0x1016625f
                // CODE XREFS from fcn.101661d0 @ 0x10166247(x), 0x10166249(x)
                push (eax)
                push (str.ModelAnimation::getPlayerAnimation___unknown_type:__i) // 0x105b2228 // "ModelAnimation::getPlayerAnimation - unknown type: %i" // (pstr 0x105b2228) "ModelAnimation::getPlayerAnimation - unknown type: %i"
                fcn.101471b0 () // fcn.101471b0(0x0, 0x0)
                esp += 8
                eax = 0
                break;
        }
        switch (eax) { // jump table of 4 cases at 0x10166668
            case 1: // 0x101662c3
                // CODE XREF from fcn.101661d0 @ 0x1016629a(x)
                eax = dword [var_18h]
                al = byte [eax + 0x7c]
                v = al & al
                if (!v) goto loc_0x101662d5 // unlikely
                break;
            case 2: // 0x101662ea
                // CODE XREF from fcn.101661d0 @ 0x1016629a(x)
                push (0x1065fb74)
                ecx = var_34h
                fcn.1028e490 () // fcn.1028e490(0x0, 0x0)
                al = byte [esi + 0x54]
                byte [var_dh] = al
                eax = edi + 0x88
                push (str.Back) // 0x103877fc // "Back" // (pstr 0x103877fc) "Back"
                push (eax)
                fcn.1000cc00 () // fcn.1000cc00(0x88, 0x0)
                ecx = al
                eax = 0
                esp += 8      // (pstr 0x103877fc) "Back"
                v = byte [var_dh] - 1
                al = v == 0
                v = eax - ecx
                if (v) goto loc_0x1016632b // likely
                break;
            case 3: // 0x101662db
                // CODE XREF from fcn.101661d0 @ 0x1016629a(x)
                push (0x1065fb74)
                ecx = var_34h
                fcn.1028e490 () // fcn.1028e490(0x0, 0x0)
                goto loc_0x1016633f
            case 4: // 0x101662a1
                // CODE XREF from fcn.101661d0 @ 0x1016629a(x)
                push (dword [esi + 0x58])
                break;
            default: // 0x10166342
                // CODE XREFS from fcn.101661d0 @ 0x10166294(x), 0x1016629a(x)
                eax = dword [edi + 0x6c]
                eax--
                v = eax - 3   // 3
                if (((unsigned) v) > 0) goto case.default.0x1016634f // likely
                break;
        }
        switch (eax) { // jump table of 4 cases at 0x10166678
            case 1: // 0x1016636c
                // CODE XREF from fcn.101661d0 @ 0x1016634f(x)
                al = byte [ecx + 0x7c]
                v = al & al
                if (!v) goto loc_0x1016637b // unlikely
                break;
            case 2: // 0x101663a7
                // CODE XREF from fcn.101661d0 @ 0x1016634f(x)
                push (0x1065fb74)
                ecx = var_28h
                fcn.1028e490 () // fcn.1028e490(0x0, 0x0)
                al = byte [esi + 0x54]
                byte [var_dh] = al
                eax = edi + 0x94
                push (str.Back) // 0x103877fc // "Back" // (pstr 0x103877fc) "Back"
                push (eax)
                fcn.1000cc00 () // fcn.1000cc00(0x94, 0x0)
                ecx = al
                eax = 0
                esp += 8      // (pstr 0x103877fc) "Back"
                v = byte [var_dh] - 1
                al = v == 0
                v = eax - ecx
                if (v) goto loc_0x101663e8 // likely
                break;
            case 3: // 0x10166396
                // CODE XREF from fcn.101661d0 @ 0x1016634f(x)
                eax = ecx + 0x98
                push (eax)
                ecx = var_28h
                fcn.1028e490 () // fcn.1028e490(0x98, 0x0)
                goto loc_0x101663f5 // case.default.0x1016634f // case.default.0x1016634f(0x98, 0x0, 0x177fd8, 0x0)
            case 4: // 0x10166356
                // CODE XREF from fcn.101661d0 @ 0x1016634f(x)
                ecx = dword [ecx + 0x5c]
                fcn.1016c5d0 () // fcn.1016c5d0(0x0)
                push (eax)
                ecx = var_28h
                fcn.1028e490 () // fcn.1028e490(0x0, 0x0)
                goto loc_0x101663f5 // case.default.0x1016634f // case.default.0x1016634f(0x0, 0x0, 0x177fd8, 0x0)
            default: // 0x101663f5
                // CODE XREFS from fcn.101661d0 @ 0x10166349(x), 0x1016634f(x), 0x10166367(x), 0x10166394(x), 0x101663a5(x)
                eax = byte [esi + 0x54]
                movd xmm1 eax
                cvtdq2ps xmm1 xmm1
                eax = var_34h
                xmm1 = xmm1 * dword [edi + 0xb4]
                push (eax)
                eax = var_4ch
                addss xmm1 dword [var_28h]
                push (eax)
                ecx = var_28h
                movss dword [var_28h] xmm1
                movss xmm0 dword [edi + 0xb8]
                addss xmm0 dword [var_24h]
                movss dword [esi + 0x80] xmm1
                movss dword [var_24h] xmm0
                fcn.1028e890 () // fcn.1028e890(0x177fb4, 0x0, 0x177fd8)
                ecx = var_4ch
                push (ecx)
                ecx = esi + 0x98
                fcn.1028e490 () // fcn.1028e490(0x177fb4, 0x0)
                v = byte [edi + 0x86] - 0
                xmm0 ^= xmm0
                if (!v) goto loc_0x10166461 // unlikely
                break;
        }
    loc_0x1016626f: // orphan
         // CODE XREFS from fcn.101661d0 @ 0x10166258(x), 0x1016625d(x)
         dword [var_14h] = eax

    loc_0x10166272: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166253(x)
         ecx = var_34h
         fcn.1028e470 ()          // fcn.1028e470(0x0)
         ecx = var_28h
         fcn.1028e470 ()          // fcn.1028e470(0x0)
         ecx = dword [var_14h]
         eax = dword [edi + 0x68]
         v = ecx & ecx
         if (!v) ecx = esi
         eax--
         dword [var_14h] = ecx
         v = eax - 3              // 3
         if (((unsigned) v) > 0) 
         goto loc_0x1016629a;
    loc_0x101662a4: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101662d9(x)
         ecx = esi + 0xe8
         push (2)                 // 2
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = eax
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         push (eax)
         ecx = var_34h
         fcn.1028e490 ()          // fcn.1028e490(0x0, 0x0)
         
         goto loc_0x101662c3;
    loc_0x101662cd: // orphan
         eax = dword [edi + 0x74]
         v = eax - 0xffffffff
         if (v > 0) 
         goto loc_0x101662d5;
    loc_0x101662d5: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101662cb(x)
         eax = dword [edi + 0x70]

    loc_0x101662d8: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101662d3(x)
         push (eax)
         
         goto loc_0x101662db;
    loc_0x10166321: // orphan
         movss xmm0 dword [esi + 0xe0]
         
         goto loc_0x1016632b;
    loc_0x1016632b: // orphan
         // CODE XREF from fcn.101661d0 @ 0x1016631f(x)
         movss xmm0 dword [esi + 0xe4]

    loc_0x10166333: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166329(x)
         xmm0 ^= xmmword [0x103743c0]
         movss dword [var_34h] xmm0

    loc_0x1016633f: // orphan
         // CODE XREFS from fcn.101661d0 @ 0x101662c1(x), 0x101662e8(x)
         ecx = dword [var_14h]

    loc_0x10166373: // orphan
         eax = dword [edi + 0x7c]
         v = eax - 0xffffffff
         if (v > 0) 
         goto loc_0x1016637b;
    loc_0x1016637b: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166371(x)
         eax = dword [edi + 0x78]

    loc_0x1016637e: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166379(x)
         push (eax)
         fcn.10164990 ()          // fcn.10164990(0x0, 0x0)
         ecx = eax
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         push (eax)
         ecx = var_28h
         fcn.1028e490 ()          // fcn.1028e490(0x0, 0x0)
         
         goto loc_0x10166396;
    loc_0x101663de: // orphan
         movss xmm0 dword [esi + 0xe0]
         
         goto loc_0x101663e8;
    loc_0x101663e8: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101663dc(x)
         movss xmm0 dword [esi + 0xe4]

    loc_0x101663f0: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101663e6(x)
         movss dword [var_28h] xmm0

    loc_0x10166457: // orphan
         movss xmm2 dword [esi + 0xa0]
         
         goto loc_0x10166461;
    loc_0x10166461: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166455(x)
         xmm2 = xmm0

    loc_0x10166464: // orphan
         // CODE XREF from fcn.101661d0 @ 0x1016645f(x)
         v = byte [edi + 0x85] - 0
         if (!v) 
         goto loc_0x1016646d;
    loc_0x1016646d: // orphan
         movss xmm1 dword [esi + 0x9c]
         
         goto loc_0x10166477;
    loc_0x10166477: // orphan
         // CODE XREF from fcn.101661d0 @ 0x1016646b(x)
         xmm1 = xmm0

    loc_0x1016647a: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166475(x)
         v = byte [edi + 0x84] - 0
         if (!v) 
         goto loc_0x10166483;
    loc_0x10166483: // orphan
         movss xmm0 dword [esi + 0x98]

    loc_0x1016648b: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166481(x)
         esp -= 0xc
         ecx = esi
         movss dword [var_8h] xmm2
         movss dword [var_4h] xmm1
         movss dword [esp] xmm0
         fcn.10166690 ()          // fcn.10166690(0x0, 0x0, 0x0, 0x0)
         eax = dword [edi + 0xa0]
         v = eax - dword [edi + 0xa4]
         if (!v) 
         goto loc_0x101664b8;
    loc_0x101664b8: // orphan
         ecx = dword [esi + 0xdc]
         push (eax)
         fcn.1016da00 ()          // fcn.1016da00(0x0, 0x0, -1)
         ecx = eax
         dword [var_1ch] = eax
         fcn.1002ce60 ()          // method.CCLabelBMFontExtended.2.virtual_4 // fcn.1002ce60(0x0)
         edi = dword [esi + 0xec]
         ecx = dword [esi + 0xf0]
         dword [var_18h] = eax
         ecx -= edi
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = eax - 2              // 2
         if (((unsigned) v) <= 0) 
         goto loc_0x101664f4;
    loc_0x101664f4: // orphan
         edi += 0x18              // 24
         
         goto loc_0x101664f9;
    loc_0x101664f9: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101664f2(x)
         push (2)                 // 2
         push (str.Subcontainer_index_error__i) // 0x105aca0c // "Subcontainer index error %i" // (pstr 0x105aca0c) "Subcontainer index error %i"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         edi = dword [esi + 0xec]
         esp += 8

    loc_0x1016650e: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101664f7(x)
         ecx = dword [edi + 8]
         ecx -= dword [edi + 4]
         eax = 0x2aaaaaab
         eax = eax * ecx
         ecx = dword [var_18h]
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = ecx - eax
         jae 0x10166536           // likely

         goto loc_0x1016652b;
    loc_0x1016652b: // orphan
         eax = ecx + ecx*2
         ecx = dword [edi + 4]
         eax = ecx + eax*4
         
         goto loc_0x10166536;
    loc_0x10166536: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166529(x)
         push (ecx)
         push (str.Subcontainer_index_error__i) // 0x105aca0c // "Subcontainer index error %i" // (pstr 0x105aca0c) "Subcontainer index error %i"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         eax = dword [edi + 4]
         esp += 8

    loc_0x10166547: // orphan
         // CODE XREF from fcn.101661d0 @ 0x10166534(x)
         push (eax)
         ecx = var_34h
         fcn.1028e490 ()          // fcn.1028e490(0x0, 0x0)
         ecx = dword [var_1ch]
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         push (eax)
         eax = var_58h
         push (eax)
         ecx = var_34h
         fcn.1028e890 ()          // fcn.1028e890(0x177fa8, 0x0, 0x177fcc)
         ecx = dword [var_1ch]
         fcn.1016c5f0 ()          // fcn.1016c5f0(0x177fa8)
         push (eax)
         eax = var_64h
         push (eax)
         ecx = var_34h
         fcn.1028e890 ()          // fcn.1028e890(0x177f9c, 0x0, 0x177fcc)
         ecx = dword [esi + 0xdc]
         fcn.10048b30 ()          // fcn.10048b30(0x177f9c)
         edi = eax
         eax = var_dh
         esi = dword [edi + 4]
         esi -= dword [edi]
         push (eax)
         ecx = edi
         esi >>= 2
         fcn.101ec1c0 ()          // fcn.101ec1c0(0x177ff3)
         push (eax)
         push (esi)
         ecx = var_40h
         fcn.101aefe0 ()          // fcn.101aefe0(0x177ff3, 0x0, 0x177fc0)
         esi = dword [edi + 4]
         eax = dword [edi]
         dword [var_bp_4h] = 0
         v = esi - eax
         if (v) 
         goto loc_0x101665b4;
    loc_0x101665b4: // orphan
         eax = dword [var_40h]
         edi = eax
         
         goto loc_0x101665bb;
    loc_0x101665bb: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101665b2(x)
         esi -= eax
         push (esi)
         push (eax)
         push (dword [var_40h])
         sub.MSVCR110.dll_memmove ()
         edi = esi + eax
         eax = dword [var_40h]
         esp += 0xc

    loc_0x101665d0: // orphan
         // CODE XREF from fcn.101661d0 @ 0x101665b9(x)
         dword [var_3ch] = edi
         ecx = edi
         ecx -= eax
         ecx >>= 2
         dword [var_bp_4h] = 1
         v = ecx & ecx
         if (!v) 
         goto loc_0x101665e5;
    loc_0x101665e5: // orphan
         ecx = dword [eax]
         dword [var_18h] = ecx
         esi = eax
         v = eax - edi
         jae 0x10166623           // likely

         goto loc_0x101665f0;
    loc_0x101665f0: // orphan
         // CODE XREF from fcn.101661d0 @ 0x1016661e(x)
         eax = var_58h
         push (eax)
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x177fa8)
         ecx = eax
         fcn.1028e6b0 ()          // fcn.1028e6b0(0x177fa8, 0x0)
         ecx = dword [var_18h]
         eax = var_64h
         push (eax)
         fcn.1016c5f0 ()          // fcn.1016c5f0(0x177f9c)
         ecx = eax
         fcn.1028e6b0 ()          // fcn.1028e6b0(0x177f9c, 0x0)
         ecx = dword [esi + 4]
         esi += 4
         dword [var_18h] = ecx
         v = esi - edi
         if (((unsigned) v) < 0) 
         goto loc_0x10166620;
    loc_0x10166620: // orphan
         eax = dword [var_40h]

    loc_0x10166623: // orphan
         // CODE XREFS from fcn.101661d0 @ 0x101665e3(x), 0x101665ee(x)
         dword [var_bp_4h] = 0xffffffff // -1
         v = eax & eax
         if (!v) 
         goto loc_0x1016662e;
    loc_0x1016662e: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10166637: // orphan
         // CODE XREFS from fcn.101661d0 @ 0x101664b2(x), 0x1016662c(x)
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         esp = ebp
         ebp = pop ()
         return

}

