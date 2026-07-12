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
void fcn.100d8370 (int32_t arg1) {
        // CALL XREF from method.StageSF.virtual_328 @ 0x10231bb8(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x10311052) // 'R\x101\x10'
        eax = dword fs:[0]
        push (eax)
        esp -= 0x34
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        push (0x1038aa60) // "preloadHacks - init hash" // (pstr 0x1038aa60) "preloadHacks - init hash"
        dword [var_14h] = 0
        fcn.101472f0 () // fcn.101472f0(0x177ff0, 0x0)
        esp += 4      // (pstr 0x1038aa60) "preloadHacks - init hash"
        fcn.10241c80 ()
        v = al & al
        if (!v) goto loc_0x100d844a // unlikely
        goto loc_0x100d83b9;
    loc_0x100d844a:
        // CODE XREF from fcn.100d8370 @ 0x100d83b3(x)
        push (0x19)   // 25
        dword [var_14h] = 2
        fcn.102fc5d0 () // fcn.102fc5d0(0x0)
        esp += 4
        ebx = 0
        esi = eax
        dword [var_18h] = ebx
        dword [var_34h] = ebx
        dword [var_30h] = ebx
        dword [var_2ch] = ebx
        ecx = esi
        dword [var_4h] = 2
        edx = ecx + 1
        
    do {
        // CODE XREF from fcn.100d8370 @ 0x100d847c(x)
        al = byte [ecx]
        ecx++
        v = al & al
    } while (al & al);
    loc_0x100d847e:
        ecx -= edx
        eax = ecx + esi
        edi = eax
        edi -= esi
        dword [var_20h] = eax
        ecx = edi + 1
        eax = ecx - 1
        v = eax - 0xfffffffe
        if (((unsigned) v) > 0) goto 0x100d84c8 // unlikely
        goto loc_0x100d8495;
    loc_0x100d84c8:
        // CODE XREF from fcn.100d8370 @ 0x100d8493(x)
        push (str.basic_string) // 0x10374404 // "basic_string" // (pstr 0x10374404) "basic_string"
        fcn.102e3f60 () // fcn.102e3f60(0x0)
        esp += 4      // (pstr 0x10374404) "basic_string"
        
    loc_0x100d84d5:
        // CODE XREF from fcn.100d8370 @ 0x100d84c6(x)
        v = dword [var_20h] - esi
        if (!v) goto loc_0x100d84e8 // likely
        goto loc_0x100d84da;
        goto loc_0x100d84a7;
    loc_0x100d83b9:
        fcn.10240330 ()
        edi = 0
        dword [var_34h] = edi
        dword [var_30h] = edi
        dword [var_2ch] = edi
        ecx = dword [eax + 4]
        eax = dword [eax]
        esi = ecx
        esi -= eax
        dword [var_20h] = eax
        ebx = esi + 1
        dword [var_4h] = edi
        eax = ebx - 1
        dword [var_1ch] = ecx
        v = eax - 0xfffffffe
        if (((unsigned) v) > 0) goto 0x100d840e // unlikely
        goto loc_0x100d83e6;
        return eax;
    loc_0x100d83e6:
        push (ebx)
        fcn.102f7820 ()
        edi = eax
        esp += 4
        v = edi & edi
        if (v) goto loc_0x100d8400 // unlikely
        goto loc_0x100d83f5;
    loc_0x100d8400:
        // CODE XREF from fcn.100d8370 @ 0x100d83f3(x)
        eax = ebx + edi
        dword [var_34h] = edi
        dword [var_30h] = edi
        dword [var_2ch] = eax
        goto loc_0x100d841b
        
    loc_0x100d841b:
        // CODE XREF from fcn.100d8370 @ 0x100d840c(x)
        eax = dword [var_20h]
        v = dword [var_1ch] - eax
        if (v) goto loc_0x100d8427 // unlikely
        goto loc_0x100d8423;
        return eax;
    loc_0x100d840e: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d83e4(x)
         push (str.basic_string)  // 0x10374404 // "basic_string" // (pstr 0x10374404) "basic_string"
         fcn.102e3f60 ()          // fcn.102e3f60(0x0)
         esp += 4                 // (pstr 0x10374404) "basic_string"

    loc_0x100d8423: // orphan
         eax = edi
         
         goto loc_0x100d8427;
    loc_0x100d8427: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8421(x)
         push (esi)
         push (eax)
         push (edi)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += esi

    loc_0x100d8434: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8425(x)
         dword [var_30h] = eax
         byte [eax] = 0
         eax = dword [var_34h]
         ebx = 1
         dword [var_18h] = eax
         
         goto loc_0x100d844a;
    loc_0x100d8495: // orphan
         push (ecx)
         fcn.102f7820 ()
         ebx = eax
         esp += 4
         dword [var_18h] = ebx
         v = ebx & ebx
         if (v) 
         goto loc_0x100d84a7;
    loc_0x100d84a7: // orphan
         eax = edi + 1
         push (eax)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x1)
         ebx = eax
         esp += 4
         dword [var_18h] = ebx

    loc_0x100d84b8: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d84a5(x)
         eax = edi + 1
         eax += ebx
         dword [var_34h] = ebx
         dword [var_30h] = ebx
         dword [var_2ch] = eax
         
         goto loc_0x100d84c8;
    loc_0x100d84da: // orphan
         push (edi)
         push (esi)
         push (ebx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         ebx = eax + edi

    loc_0x100d84e8: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d84d8(x)
         dword [var_30h] = ebx
         byte [ebx] = 0
         edi = dword [var_34h]
         ebx = 6

    loc_0x100d84f6: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8445(x)
         esi = 0
         dword [var_14h] = ebx
         ecx = var_34h
         dword [var_40h] = esi
         dword [var_3ch] = esi
         dword [var_38h] = esi
         eax = dword [ecx + 4]
         ecx = dword [ecx]
         dword [var_28h] = eax
         eax -= ecx
         dword [var_24h] = ecx
         ecx = eax + 1
         dword [var_20h] = eax
         eax = ecx - 1
         dword [var_4h] = 4
         dword [var_1ch] = ecx
         v = eax - 0xfffffffe
         if (((unsigned) v) > 0) 
         goto loc_0x100d852c;
    loc_0x100d852c: // orphan
         push (ecx)
         fcn.102f7820 ()
         esi = eax
         esp += 4
         v = esi & esi
         if (v) 
         goto loc_0x100d853b;
    loc_0x100d853b: // orphan
         push (dword [var_1ch])
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         esi = eax

    loc_0x100d8548: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8539(x)
         eax = dword [var_1ch]
         eax += esi
         dword [var_40h] = esi
         dword [var_3ch] = esi
         dword [var_38h] = eax
         
         goto loc_0x100d8558;
    loc_0x100d8558: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d852a(x)
         push (str.basic_string)  // 0x10374404 // "basic_string" // (pstr 0x10374404) "basic_string"
         fcn.102e3f60 ()          // fcn.102e3f60(0x0)
         esp += 4                 // (pstr 0x10374404) "basic_string"

    loc_0x100d8565: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8556(x)
         eax = dword [var_24h]
         v = dword [var_28h] - eax
         if (!v) 
         goto loc_0x100d856d;
    loc_0x100d856d: // orphan
         push (dword [var_20h])
         push (eax)
         push (esi)
         sub.MSVCR110.dll_memmove ()
         esi = eax
         esp += 0xc
         esi += dword [var_20h]

    loc_0x100d857f: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d856b(x)
         dword [var_3ch] = esi
         byte [esi] = 0
         dword [var_4h] = 6
         v = bl & 4               // 4
         if (!v) 
         goto loc_0x100d8591;
    loc_0x100d8591: // orphan
         eax = dword [var_18h]
         ebx &= 0xfffffffb        // 4294967291
         dword [var_14h] = ebx
         v = eax & eax
         if (!v) 
         goto loc_0x100d859e;
    loc_0x100d859e: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d85a7: // orphan
         // CODE XREFS from fcn.100d8370 @ 0x100d858f(x), 0x100d859c(x)
         v = bl & 2               // 2
         if (!v) 
         goto loc_0x100d85ac;
    loc_0x100d85ac: // orphan
         ebx &= 0xfffffffd        // 4294967293

    loc_0x100d85af: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d85aa(x)
         byte [var_4h] = 7
         v = bl & 1               // 1
         if (!v) 
         goto loc_0x100d85b8;
    loc_0x100d85b8: // orphan
         v = edi & edi
         if (!v) 
         goto loc_0x100d85bc;
    loc_0x100d85bc: // orphan
         push (edi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d85c5: // orphan
         // CODE XREFS from fcn.100d8370 @ 0x100d85b6(x), 0x100d85ba(x)
         push (2)                 // 2
         fcn.102fc5d0 ()          // fcn.102fc5d0(0x0)
         esi = eax
         edx = esi
         esp += 4
         eax = edx + 1

    loc_0x100d85d6: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d85db(x)
         cl = byte [edx]
         edx++
         v = cl & cl
         if (v) 
         goto loc_0x100d85dd;
    loc_0x100d85dd: // orphan
         edx -= eax
         eax = var_dh
         push (eax)
         eax = edx + esi
         push (eax)
         push (esi)
         ecx = var_40h
         fcn.10002270 ()          // fcn.10002270(0x0, 0x0)
         edi = dword [var_40h]
         push (edi)
         fcn.100f9410 ()          // fcn.100f9410(0x0, 0x0)
         fcn.10241b10 ()
         v = byte [eax] - 0
         bl = v == 0
         fcn.100da010 ()
         byte [eax] = bl
         fcn.100da010 ()
         eax = byte [eax]
         push (eax)
         fcn.100f9400 ()          // fcn.100f9400(0x1)
         esp += 8
         fcn.100e62d0 ()
         esi = eax
         fcn.100d63e0 ()          // fcn.100d63e0(0x1)
         byte [esi] = al
         fcn.100e62d0 ()
         v = byte [eax] - 0
         if (v) 
         goto loc_0x100d8634;
    loc_0x100d8634: // orphan
         fcn.100dac00 ()          // fcn.100dac00(0x0)
         fcn.100da010 ()
         v = byte [eax] - 0
         if (!v) 
         goto loc_0x100d8643;
    loc_0x100d8643: // orphan
         fcn.100d9ba0 ()
         eax = al
         push (eax)
         fcn.100f5ab0 ()          // fcn.100f5ab0(0x0, 0x0, 0x0)
         esp += 4

    loc_0x100d8654: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8641(x)
         fcn.100d9bb0 ()          // fcn.100d9bb0(0x0)

    loc_0x100d8659: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8632(x)
         dword [var_4h] = 0xffffffff // -1
         v = edi & edi
         if (!v) 
         goto loc_0x100d8664;
    loc_0x100d8664: // orphan
         push (edi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d866d: // orphan
         // CODE XREF from fcn.100d8370 @ 0x100d8662(x)
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

