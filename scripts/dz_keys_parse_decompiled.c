// callconv: eax reg (eax, ebx, ecx, edx);
void fcn.10086f80 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        // CALL XREF from fcn.10089560 @ 0x10089603(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1030ab1e)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x64
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        ebx = ecx     // arg3
        dword [var_20h] = ebx
        push (4)      // 4
        fcn.10083160 () // fcn.10083160(0x177ff0, 0x0)
        esi = ebx + 0x14
        ecx = esi
        dword [var_4h] = 0
        dword [ebx] = vtable.ConditionKeys.0 // [0x103827c4:4]=0x10087570 method.ConditionKeys.virtual_0 // "pu\b\x10\x102\b\x10\xa0u\b\x1002\b\x10@\xcc\x1d\x10\xd01\b\x10\x902\b\x10\xf0u\b\x10PressType"
        fcn.10087440 () // fcn.10087440(0x177ff0)
        edi = ebx + 0x4c
        ecx = edi
        fcn.10087440 () // fcn.10087440(0x177ff0)
        ecx = dword [arg_8h]
        eax = var_10h
        push (eax)
        byte [var_4h] = 2
        fcn.101bc850 () // fcn.101bc850(0x177fec, 0x0)
        ecx = var_10h
        fcn.101ba9a0 () // fcn.101ba9a0(0x177fec)
        v = eax & eax
        if (!v) goto loc_0x10087293 // unlikely
        goto loc_0x10086ff5;
    loc_0x10087293:
        // CODE XREF from fcn.10086f80 @ 0x10086fef(x)
        ecx = esi
        fcn.10122070 () // fcn.10122070(0x0, 0x0)
        push (esi)
        ecx = var_70h
        fcn.10087330 () // fcn.10087330(0x0, 0x0, 0x177f90)
        esi = eax
        push (esi)
        ecx = edi
        byte [var_4h] = 5
        fcn.10206460 () // fcn.10206460(0x0, 0x0, 0x0)
        ecx = esi + 0xc
        push (ecx)
        ecx = edi + 0xc
        fcn.10206460 () // fcn.10206460(0x0, 0x0, 0xc)
        xmm0 = qword [esi + 0x18]
        qword [edi + 0x18] = xmm0
        xmm0 = qword [esi + 0x20]
        qword [edi + 0x20] = xmm0
        xmm0 = qword [esi + 0x28]
        qword [edi + 0x28] = xmm0
        eax = dword [esi + 0x30]
        dword [edi + 0x30] = eax
        al = byte [esi + 0x34]
        byte [edi + 0x34] = al
        eax = dword [var_64h]
        byte [var_4h] = 6
        v = eax & eax
        if (!v) goto loc_0x100872fb // likely
        goto loc_0x100872f2;
    loc_0x100872fb:
        // CODE XREF from fcn.10086f80 @ 0x100872f0(x)
        eax = dword [var_70h]
        byte [var_4h] = 2
        v = eax & eax
        if (!v) goto loc_0x1008730f // likely
        goto loc_0x10087306;
    loc_0x1008730f:
        // CODE XREF from fcn.10086f80 @ 0x10087304(x)
        push (0xffffffffffffffff)
        ecx = edi
        fcn.10122100 () // fcn.10122100(0x0, 0x0)
        eax = ebx
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()  // ebp
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
    loc_0x10087000: // orphan
         // CODE XREFS from fcn.10086f80 @ 0x10086ff5(x), 0x10087287(x)
         push (0x10374b40)        // '@K7\x10'
         push (0x1037895c)        // (pstr 0x1037895c) "Type"
         eax = var_24h
         push (eax)
         ecx = var_10h
         fcn.101bb0e0 ()          // fcn.101bb0e0(0x177fdc, 0x0, 0x177ff0)
         ecx = eax
         fcn.101bb070 ()          // fcn.101bb070(0x177fdc, 0x0)
         push (eax)
         push (0)
         fcn.1017cb40 ()          // fcn.1017cb40(0x177fdc, 0x0, 0x177fdc)
         esp += 8
         dword [arg_8h] = eax
         push (0x10374b40)        // '@K7\x10'
         push (str.PressType)     // 0x103827e4 // "PressType" // (pstr 0x103827e4) "PressType"
         eax = var_28h
         push (eax)
         ecx = var_10h
         fcn.101bb0e0 ()          // fcn.101bb0e0(0x177fd8, 0x0, 0x177ff0)
         ecx = eax
         fcn.101bb070 ()          // fcn.101bb070(0x177fd8, 0x0)
         esi = 0
         edx = eax
         dword [var_14h] = edx
         dword [var_38h] = esi
         dword [var_34h] = esi
         dword [var_30h] = esi
         ecx = edx
         byte [var_4h] = 3
         edi = ecx + 1

    loc_0x10087061: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087066(x)
         al = byte [ecx]
         ecx++
         v = al & al
         if (v) 
         goto loc_0x10087068;
    loc_0x10087068: // orphan
         ecx -= edi
         eax = ecx + edx
         edi = eax
         edi -= edx
         dword [var_18h] = eax
         ecx = edi + 1
         eax = ecx - 1
         v = eax - 0xfffffffe
         if (((unsigned) v) > 0) 
         goto loc_0x1008707f;
    loc_0x1008707f: // orphan
         push (ecx)
         fcn.102f7820 ()
         esi = eax
         esp += 4
         v = esi & esi
         if (v) 
         goto loc_0x1008708e;
    loc_0x1008708e: // orphan
         eax = edi + 1
         push (eax)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x1)
         esp += 4
         esi = eax

    loc_0x1008709c: // orphan
         // CODE XREF from fcn.10086f80 @ 0x1008708c(x)
         eax = edi + 1
         eax += esi
         dword [var_38h] = esi
         dword [var_34h] = esi
         dword [var_30h] = eax
         
         goto loc_0x100870ac;
    loc_0x100870ac: // orphan
         // CODE XREF from fcn.10086f80 @ 0x1008707d(x)
         push (str.basic_string)  // 0x10374404 // "basic_string" // (pstr 0x10374404) "basic_string"
         fcn.102e3f60 ()          // fcn.102e3f60(0x0)
         esp += 4                 // (pstr 0x10374404) "basic_string"

    loc_0x100870b9: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100870aa(x)
         eax = dword [var_14h]
         v = dword [var_18h] - eax
         if (v) 
         goto loc_0x100870c1;
    loc_0x100870c1: // orphan
         eax = esi
         
         goto loc_0x100870c5;
    loc_0x100870c5: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100870bf(x)
         push (edi)
         push (eax)
         push (esi)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += edi

    loc_0x100870d2: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100870c3(x)
         dword [var_34h] = eax
         byte [eax] = 0
         eax -= esi
         byte [var_4h] = 4
         v = eax - 4              // 4
         if (v) 
         goto loc_0x100870e7;
    loc_0x100870e7: // orphan
         v = dword [esi] - 0x646c6f48 // 'Hold'
         if (v) 
         goto loc_0x100870f3;
    loc_0x100870f3: // orphan
         edi = dword [ebx + 0x24]
         v = edi - dword [ebx + 0x28]
         if (!v) 
         goto loc_0x100870fb;
    loc_0x100870fb: // orphan
         v = edi & edi
         if (!v) 
         goto loc_0x100870ff;
    loc_0x100870ff: // orphan
         ecx = dword [arg_8h]
         dword [edi] = ecx

    loc_0x10087104: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100870fd(x)
         dword [ebx + 0x24] += 4
         
         goto loc_0x1008710d;
    loc_0x1008710d: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100870f9(x)
         ecx = edi
         ecx -= dword [ebx + 0x20]
         eax = var_18h
         ecx >>= 2
         v = ecx - 1
         edx = var_14h
         cmovae eax edx
         dword [var_18h] = 1
         dword [var_14h] = ecx
         eax = dword [eax]
         eax += ecx
         dword [var_1ch] = eax
         if (!v) 
         goto loc_0x10087134;
    loc_0x10087134: // orphan
         eax <<<= 2
         push (eax)
         dword [var_18h] = eax
         fcn.102f7820 ()
         esp += 4
         dword [var_14h] = eax
         v = eax & eax
         if (v) 
         goto loc_0x1008714a;
    loc_0x1008714a: // orphan
         push (dword [var_18h])
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         
         goto loc_0x10087157;
    loc_0x10087157: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087132(x)
         eax = 0

    loc_0x10087159: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087155(x)
         dword [var_14h] = eax

    loc_0x1008715c: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087148(x)
         ecx = dword [ebx + 0x20]
         v = edi - ecx
         if (!v) 
         goto loc_0x10087163;
    loc_0x10087163: // orphan
         edi -= ecx
         push (edi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += edi

    loc_0x10087172: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087161(x)
         ecx = dword [arg_8h]
         dword [eax] = ecx
         edi = eax + 4
         eax = dword [ebx + 0x20]
         v = eax & eax
         if (!v) 
         goto loc_0x10087181;
    loc_0x10087181: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1008718a: // orphan
         // CODE XREF from fcn.10086f80 @ 0x1008717f(x)
         eax = dword [var_14h]
         ecx = dword [var_1ch]
         dword [ebx + 0x20] = eax
         eax = eax + ecx*4
         dword [ebx + 0x24] = edi
         dword [ebx + 0x28] = eax
         
         goto loc_0x100871a1;
    loc_0x100871a1: // orphan
         // CODE XREFS from fcn.10086f80 @ 0x100870e1(x), 0x100870ed(x)
         edi = dword [ebx + 0x18]
         v = edi - dword [ebx + 0x1c]
         if (!v) 
         goto loc_0x100871a9;
    loc_0x100871a9: // orphan
         v = edi & edi
         if (!v) 
         goto loc_0x100871ad;
    loc_0x100871ad: // orphan
         ecx = dword [arg_8h]
         dword [edi] = ecx

    loc_0x100871b2: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100871ab(x)
         dword [ebx + 0x18] += 4
         
         goto loc_0x100871bb;
    loc_0x100871bb: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100871a7(x)
         ecx = edi
         ecx -= dword [ebx + 0x14]
         edx = var_18h
         ecx >>= 2
         v = ecx - 1
         eax = var_1ch
         cmovae eax edx
         dword [var_18h] = ecx
         dword [var_1ch] = 1
         eax = dword [eax]
         eax += ecx
         dword [var_18h] = eax
         edx = ebx + 0x14
         if (!v) 
         goto loc_0x100871e5;
    loc_0x100871e5: // orphan
         eax <<<= 2
         push (eax)
         dword [var_1ch] = eax
         fcn.102f7820 ()
         esp += 4
         dword [var_14h] = eax
         v = eax & eax
         if (v) 
         goto loc_0x100871fb;
    loc_0x100871fb: // orphan
         push (dword [var_1ch])
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         dword [var_14h] = eax

    loc_0x10087209: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100871f9(x)
         edx = ebx + 0x14
         
         goto loc_0x1008720e;
    loc_0x1008720e: // orphan
         // CODE XREF from fcn.10086f80 @ 0x100871e3(x)
         eax = 0
         dword [var_14h] = eax

    loc_0x10087213: // orphan
         // CODE XREF from fcn.10086f80 @ 0x1008720c(x)
         ecx = dword [edx]
         v = edi - ecx
         if (!v) 
         goto loc_0x10087219;
    loc_0x10087219: // orphan
         edi -= ecx
         push (edi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += edi
         edx = ebx + 0x14
         
    loc_0x10087230: // orphan
         // CODE XREFS from fcn.10086f80 @ 0x10087217(x), 0x1008722b(x)
         ecx = dword [arg_8h]
         dword [eax] = ecx
         edi = eax + 4
         eax = dword [edx]
         v = eax & eax
         if (!v) 
         goto loc_0x1008723e;
    loc_0x1008723e: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4
         edx = ebx + 0x14

    loc_0x1008724a: // orphan
         // CODE XREF from fcn.10086f80 @ 0x1008723c(x)
         eax = dword [var_14h]
         ecx = dword [var_18h]
         dword [edx] = eax
         eax = eax + ecx*4
         dword [edx + 4] = edi
         dword [edx + 8] = eax

    loc_0x1008725b: // orphan
         // CODE XREFS from fcn.10086f80 @ 0x10087108(x), 0x1008719c(x), 0x100871b6(x)
         byte [var_4h] = 2
         v = esi & esi
         if (!v) 
         goto loc_0x10087263;
    loc_0x10087263: // orphan
         push (esi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1008726c: // orphan
         // CODE XREF from fcn.10086f80 @ 0x10087261(x)
         eax = var_2ch
         push (eax)
         ecx = var_10h
         fcn.101bcef0 ()          // fcn.101bcef0(0x177fd4, 0x0)
         eax = dword [eax]
         ecx = var_10h
         dword [var_10h] = eax
         fcn.101ba9a0 ()          // fcn.101ba9a0(0x10087048)
         v = eax & eax
         if (v) 
         goto loc_0x1008728d;
    loc_0x1008728d: // orphan
         esi = ebx + 0x14
         edi = ebx + 0x4c

    loc_0x100872f2: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10087306: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

