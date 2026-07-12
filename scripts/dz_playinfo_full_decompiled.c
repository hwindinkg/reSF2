// callconv: eax reg (eax, ebx, ecx, edx);
void fcn.10164fa0 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch, int32_t arg_10h, uint32_t arg_14h, uint32_t arg_18h) {
        // CALL XREF from fcn.1015a220 @ 0x1015a2c0(x)
        push (ebp)
        ebp = esp
        esp -= 0xc
        push (ebx)    // arg2
        push (esi)
        esi = dword [arg_8h]
        push (edi)
        ebx = arg_ch  // arg3
        v = esi & esi
        if (!v) goto loc_0x101650f1 // likely
        goto loc_0x10164fb6;
    loc_0x101650f1:
        // CODE XREF from fcn.10164fa0 @ 0x10164fb0(x)
        al = 0
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x10164fbd;
        return eax;
    loc_0x10164fbd:
        v = eax - esi
        if (!v) goto loc_0x10164fe7 // likely
        goto loc_0x10164fc1;
    loc_0x10164fe7:
        // CODE XREFS from fcn.10164fa0 @ 0x10164fbb(x), 0x10164fbf(x), 0x10164fcc(x)
        v = byte [ebx + 0x7f] - 0
        if (!v) goto loc_0x10165015 // unlikely
        goto loc_0x10164fed;
    loc_0x10165015:
        // CODE XREF from fcn.10164fa0 @ 0x10164feb(x)
        v = byte [arg_14h] - 0
        edi = dword [esi + 0x74]
        if (!v) goto loc_0x10165056 // likely
        goto loc_0x1016501e;
    loc_0x10165056:
        // CODE XREF from fcn.10164fa0 @ 0x1016501c(x)
        v = dword [arg_18h] - 0xffffffff
        cmovg edi dword [arg_18h]
        
    loc_0x1016505e:
        // CODE XREFS from fcn.10164fa0 @ 0x10165039(x), 0x10165049(x), 0x10165054(x)
        eax = dword [ebx + 0xe8]
        ecx = ebx + 0xe8
        dword [eax + 4] () // 4 // 0x4(-1, 0x0, 0xe8, 0x0)
        eax = dword [arg_ch]
        dword [ebx + 0x124] = 0x77359400 // [0x77359400:4]=-1
        v = al & al
        jns 0x10165084 // likely
        goto loc_0x1016507e;
        goto loc_0x10165024;
        goto loc_0x10164ffe;
        goto loc_0x10164fce;
        return eax;
    loc_0x10164fce:
        push (dword [esi + 0x68])
        push (dword [esi + 0x7c])
        push (dword [edi + 0x68])
        push (dword [edi + 0x7c])
        push (str.Animation_error_need:__s__Priority__i___Animation_played:__s__Priority__i_) // 0x105b2148 // "Animation error need: '%s' (Priority %i)// Animation played: '%s' (Priority %i)" // (pstr 0x105b2148) "Animation error need: '%s' (Priority %i)// Animation played: '%s"
        fcn.101471b0 () // fcn.101471b0(0x0, 0x0)
        esp += 0x14
        break;
    loc_0x10164fc1: // orphan
         ecx = esi
         edi = eax
         fcn.10103870 ()          // method.Content.1.virtual_348 // fcn.10103870(0x0)
         v = edi - eax
         if (!v) 
    loc_0x10164fed: // orphan
         ecx = ebx
         fcn.10164510 ()          // fcn.10164510(0x0, 0x0)
         ecx = dword [ebx + 0x2c]
         eax = dword [ebx + 0x28]
         v = ecx - ecx
         if (!v) 
         goto loc_0x10164ffe;
    loc_0x10164ffe: // orphan
         esi = ecx
         esi -= ecx
         push (esi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += esi
         esi = dword [arg_8h]

    loc_0x10165012: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x10164ffc(x)
         dword [ebx + 0x2c] = eax

    loc_0x1016501e: // orphan
         v = byte [ebx + 0x50] - 0
         if (!v) 
         goto loc_0x10165024;
    loc_0x10165024: // orphan
         eax = dword [ebx + 0x6c]
         v = eax - 2              // 2
         if (((unsigned) v) > 0) 
         goto loc_0x1016502c;
    loc_0x1016502c: // orphan
         ecx = dword [ebx + 0x60]
         edi = dword [arg_18h]
         eax = 0
         ecx += eax
         edi++
         edi += ecx
         
         goto loc_0x1016503b;
    loc_0x1016503b: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x1016502a(x)
         ecx = dword [ebx + 0x60]
         edi = dword [arg_18h]
         eax += 0xfffffffe
         ecx += eax
         edi++
         edi += ecx
         
         goto loc_0x1016504b;
    loc_0x1016504b: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x10165022(x)
         edi = dword [arg_18h]
         ecx = dword [ebx + 0x6c]
         edi++
         edi += ecx
         
         goto loc_0x10165056;
    loc_0x1016507e: // orphan
         byte [ebx + 0x54] = 0xff // [0xff:1]=255 // 255
         
         goto loc_0x10165084;
    loc_0x10165084: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x1016507c(x)
         byte [ebx + 0x54] = 1
         if (v > 0) 
         goto loc_0x1016508a;
    loc_0x1016508a: // orphan
         push (str.set_sign_value____1_or_1) // 0x105b2270 // "set sign value != -1 or 1" // (pstr 0x105b2270) "set sign value != -1 or 1"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         esp += 4                 // (pstr 0x105b2270) "set sign value != -1 or 1"

    loc_0x10165097: // orphan
         // CODE XREFS from fcn.10164fa0 @ 0x10165082(x), 0x10165088(x)
         dword [ebx + 0x20] = esi
         dword [ebx + 0x60] = edi
         eax = byte [esi + 0x78]  // int32_t arg1
         ecx = esi
         dword [ebx + 0x64] = eax
         fcn.10103490 ()          // fcn.10103490(0xff)
         byte [ebx + 0x7d] = al
         eax = dword [ebx + 0x64]
         eax--
         v = dword [ebx + 0x60] - eax
         if (v <= 0) 
         goto loc_0x101650b7;
    loc_0x101650b7: // orphan
         dword [ebx + 0x60] = eax

    loc_0x101650ba: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x101650b5(x)
         ecx = dword [ebx + 0x20]
         fcn.10103010 ()          // fcn.10103010(0x0)
         ecx = dword [eax + 8]
         ecx -= dword [eax + 4]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         if (v) 
         goto loc_0x101650da;
    loc_0x101650da: // orphan
         eax = dword [ebx + 0x20]
         push (dword [eax + 0x7c])
         push (str.ModelAnimation::playInfo___empty_animation___s_) // 0x105b2198 // "ModelAnimation::playInfo - empty animation "%s"" // (pstr 0x105b2198) "ModelAnimation::playInfo - empty animation \"%s\""
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         esp += 8
         byte [ebx + 0x50] = 0

    loc_0x101650fc: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x101650d8(x)
         ecx = dword [ebx + 0x20] // int32_t arg3
         push (edi)
         edi = ebx + 0xe8
         push (edi)
         fcn.10104980 ()          // fcn.10104980(0x0, 0x0, 0x0, 0x0, 0x0)
         ecx = ebx
         fcn.10164f20 ()          // fcn.10164f20(0x0, 0x0)
         ecx = ebx
         fcn.10165c10 ()          // fcn.10165c10(0x0)
         push (edi)
         ecx = ebx                // int32_t arg_8h
         fcn.10164c20 ()          // fcn.10164c20(0x0, 0x0, 0x0, 0x0)
         ecx = ebx
         fcn.101661d0 ()          // fcn.101661d0(0x0)
         al = byte [arg_10h]
         byte [ebx + 0x7e] = al
         v = al & al
         if (!v) 
         goto loc_0x10165133;
    loc_0x10165133: // orphan
         ecx = ebx
         fcn.10165d80 ()          // fcn.10165d80(0x0, 0x0)
         
         goto loc_0x1016513f;
    loc_0x1016513f: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x10165131(x)
         push (2)                 // 2
         ecx = edi
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = dword [eax + 8]
         ecx -= dword [eax + 4]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         edi = edx
         edi >>>= 0x1f
         push (2)                 // 2
         ecx = ebx + 0xe8
         edi += edx
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = ebx + 0xe8
         esi = dword [eax + 4]
         push (0)
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = edi + edi*2
         ecx <<<= 2
         push (ecx)
         push (esi)
         push (dword [eax + 4])
         fcn.102f7830 ()
         esp += 0xc
         ecx = ebx + 0xe8
         push (2)                 // 2
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = dword [eax + 8]
         ecx -= dword [eax + 4]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         edi = edx
         edi >>>= 0x1f
         push (2)                 // 2
         ecx = ebx + 0xe8
         edi += edx
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         ecx = ebx + 0xe8
         esi = dword [eax + 4]
         push (1)                 // 1
         fcn.10102c70 ()          // fcn.10102c70(0x0, 0x0)
         edx = edi + edi*2
         edx <<<= 2
         push (edx)
         push (esi)
         push (dword [eax + 4])
         fcn.102f7830 ()
         esi = dword [arg_8h]
         esp += 0xc

    loc_0x101651e4: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x1016513a(x)
         ecx = esi
         fcn.1001b520 ()          // method.Content.1.virtual_356 // fcn.1001b520(0x0)
         esp -= 0xc
         ecx = esp
         push (eax)
         dword [ecx] = 0
         dword [ecx + 4] = 0
         dword [ecx + 8] = 0
         ecx = ebx + 0x118        // int32_t arg_8h // pe_nt_image_headers32
         fcn.10165710 ()          // fcn.10165710(0x0, 0x0, 0x118, 0x0)
         esp -= 0xc
         ecx = var_ch
         dword [var_8h] = 0
         dword [var_4h] = 0
         word [ebx + 0x50] = 1
         dword [ebx + 0x68] = 0
         dword [ebx + 0x6c] = 0
         dword [ebx + 0x78] = 0x77359400 // [0x77359400:4]=-1
         dword [ebx + 0x74] = 0
         dword [ebx + 0x124] = 0x77359400 // [0x77359400:4]=-1
         dword [ebx + 0x70] = 0xfffffffd // [0xfffffffd:4]=-1 // 4294967293
         dword [esp] = 0
         fcn.1028e430 ()          // fcn.1028e430(0x0, 0x0, 0x177ff4, 0x0)
         push (eax)
         ecx = ebx + 0xa4
         fcn.1028e490 ()          // fcn.1028e490(0x0, 0x0)
         ecx = dword [ebx + 0x20]
         fcn.10103790 ()          // fcn.10103790(0x0)
         v = al & al
         if (v) 
         goto loc_0x1016527d;
    loc_0x1016527d: // orphan
         ecx = dword [ebx + 0x20]
         eax = var_ch
         push (eax)
         fcn.10103890 ()          // fcn.10103890(0x177ff4, 0x0)
         push (eax)
         ecx = ebx + 0xb0
         fcn.1028e490 ()          // fcn.1028e490(0x177ff4, 0x0)
         eax = byte [ebx + 0x54]
         movd xmm0 eax
         cvtdq2ps xmm0 xmm0
         xmm0 = xmm0 * dword [ebx + 0xb0]
         movss dword [ebx + 0xb0] xmm0

    loc_0x101652b0: // orphan
         // CODE XREF from fcn.10164fa0 @ 0x1016527b(x)
         ecx = dword [ebx + 0x20]
         eax = var_ch             // int32_t arg1
         push (eax)
         fcn.10102ff0 ()          // fcn.10102ff0(0x177ff4, 0x0)
         push (eax)
         ecx = ebx + 0xbc
         fcn.1028e490 ()          // fcn.1028e490(0x177ff4, 0x0)
         eax = byte [ebx + 0x54]  // int32_t arg1
         movd xmm0 eax
         cvtdq2ps xmm0 xmm0
         ecx = ebx
         xmm0 = xmm0 * dword [ebx + 0xbc]
         movss dword [ebx + 0xbc] xmm0
         fcn.10165c60 ()          // fcn.10165c60(0x1, 0x0)
         push (dword [ebx + 0x20])
         ecx = ebx
         push (0)
         fcn.100ae000 ()          // fcn.100ae000(0x1, 0x0, 0x0)
         edi = pop ()
         esi = pop ()
         dword [ebx + 0x40] = 0
         al = 1
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

