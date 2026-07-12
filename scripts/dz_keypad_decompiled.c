// callconv: eax reg (eax, ebx, ecx, edx);
void method.KeyPad.virtual_4 (int32_t arg1, int32_t arg2) {
        push (ebp)
        ebp = esp
        esp -= 0x10
        push (ebx)    // arg2
        push (edi)
        ebx = ecx     // arg3
        fcn.10121580 () // fcn.10121580(0x0, 0x0, 0x0)
        edx = dword [ebx + 0x3c]
        edx -= dword [ebx + 0x38]
        eax = 0x2aaaaaab
        eax = eax * edx
        edx >>= 1
        eax = edx
        eax >>>= 0x1f
        eax += edx
        edi = 0
        dword [var_8h] = edi
        dword [var_10h] = eax
        v = eax & eax
        if (v <= 0) goto loc_0x10121574 // likely
        goto loc_0x101213c5;
    loc_0x10121574:
        // CODE XREF from method.KeyPad.virtual_4 @ 0x101213bf(x)
        edi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
    loc_0x101213d0: // orphan
         // CODE XREFS from method.KeyPad.virtual_4 @ 0x101213cb(x), 0x1012156d(x)
         ecx = dword [ebx + 0x3c]
         ecx -= dword [ebx + 0x38]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = edi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x101213ea;
    loc_0x101213ea: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x101213f7: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101213e8(x)
         edx = dword [ebx + 0x38]
         eax = dword [esi + edx + 4]
         eax -= dword [esi + edx]
         eax >>= 4
         v = eax & eax
         if (!v) 
         goto loc_0x1012140c;
    loc_0x1012140c: // orphan
         ecx = dword [ebx + 0x3c]
         ecx -= edx
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = edi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10121425;
    loc_0x10121425: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x10121432: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121423(x)
         eax = dword [ebx + 0x38]
         ecx = dword [ebx + 0x3c]
         esi = dword [esi + eax]
         ecx -= eax
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = edi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10121451;
    loc_0x10121451: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x1012145e: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x1012144f(x)
         eax = dword [ebx + 0x38]
         ecx = dword [var_4h]     // "vector"
         edi = dword [ecx + eax + 4]
         edi -= dword [ecx + eax]
         ecx = dword [ebx + 0x3c]
         ecx -= eax
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = dword [var_8h] - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10121485;
    loc_0x10121485: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x10121492: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121483(x)
         eax = dword [ebx + 0x38]
         ecx = dword [var_4h]     // "vector"
         edi &= 0xfffffff0        // 4294967280
         edi += dword [ecx + eax]
         dword [var_ch] = edi
         v = esi - edi
         jae 0x1012155a           // unlikely

         goto loc_0x101214a9;
    loc_0x101214a9: // orphan
         esp = esp                // ebp

    loc_0x101214b0: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121554(x)
         push (dword [esi])
         fcn.102fc760 ()          // fcn.102fc760(0x0)
         esp += 4
         v = al & 3               // 3
         if (v) 
         goto loc_0x101214be;
    loc_0x101214be: // orphan
         v = byte [esi + 8] - 0
         if (!v) 
         goto loc_0x101214c8;
    loc_0x101214c8: // orphan
         byte [esi + 8] = 0
         edi = dword [ebx + 0x50]
         ecx = edi
         eax = dword [edi + 4]
         v = eax & eax
         if (!v) 
         goto loc_0x101214d8;
    loc_0x101214d8: // orphan
         edx = dword [esi]
         ebx = ebx

    loc_0x101214e0: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214f1(x)
         v = dword [eax + 0x10] - edx
         jl 0x101214ec            // likely

         goto loc_0x101214e5;
    loc_0x101214e5: // orphan
         ecx = eax
         eax = dword [eax + 8]
         
         goto loc_0x101214ec;
    loc_0x101214ec: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214e3(x)
         eax = dword [eax + 0xc]

    loc_0x101214ef: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214ea(x)
         v = eax & eax
         if (v) 
         goto loc_0x101214f3;
    loc_0x101214f3: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214d6(x)
         v = ecx - edi
         if (!v) 
         goto loc_0x101214f7;
    loc_0x101214f7: // orphan
         eax = dword [esi]
         v = eax - dword [ecx + 0x10]
         if (v >= 0) 
         goto loc_0x101214fe;
    loc_0x101214fe: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214f5(x)
         ecx = edi

    loc_0x10121500: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214fc(x)
         push (dword [ecx + 0x14])
         push (1)                 // 1
         
         goto loc_0x10121507;
    loc_0x10121507: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214bc(x)
         v = byte [esi + 8] - 0
         if (v) 
         goto loc_0x1012150d;
    loc_0x1012150d: // orphan
         byte [esi + 8] = 1
         edi = dword [ebx + 0x50]
         ecx = edi
         eax = dword [edi + 4]
         v = eax & eax
         if (!v) 
         goto loc_0x1012151d;
    loc_0x1012151d: // orphan
         edx = dword [esi]

    loc_0x10121520: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121531(x)
         v = dword [eax + 0x10] - edx
         jl 0x1012152c            // likely

         goto loc_0x10121525;
    loc_0x10121525: // orphan
         ecx = eax
         eax = dword [eax + 8]
         
         goto loc_0x1012152c;
    loc_0x1012152c: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121523(x)
         eax = dword [eax + 0xc]

    loc_0x1012152f: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x1012152a(x)
         v = eax & eax
         if (v) 
         goto loc_0x10121533;
    loc_0x10121533: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x1012151b(x)
         v = ecx - edi
         if (!v) 
         goto loc_0x10121537;
    loc_0x10121537: // orphan
         eax = dword [esi]
         v = eax - dword [ecx + 0x10]
         if (v >= 0) 
         goto loc_0x1012153e;
    loc_0x1012153e: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121535(x)
         ecx = edi

    loc_0x10121540: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x1012153c(x)
         push (dword [ecx + 0x14])
         push (0)

    loc_0x10121545: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121505(x)
         ecx = ebx
         fcn.100ae000 ()          // fcn.100ae000(0x0, 0x0, 0x0)
         edi = dword [var_ch]

    loc_0x1012154f: // orphan
         // CODE XREFS from method.KeyPad.virtual_4 @ 0x101214c2(x), 0x1012150b(x)
         esi += 0x10              // 16
         v = esi - edi
         if (((unsigned) v) < 0) 
         goto loc_0x1012155a;
    loc_0x1012155a: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x101214a3(x)
         esi = dword [var_4h]
         edi = dword [var_8h]

    loc_0x10121560: // orphan
         // CODE XREF from method.KeyPad.virtual_4 @ 0x10121406(x)
         edi++
         esi += 0xc               // 12
         dword [var_8h] = edi
         dword [var_4h] = esi
         v = edi - dword [var_10h]
         jl 0x101213d0            // unlikely

         goto loc_0x10121573;
    loc_0x10121573: // orphan
         esi = pop ()

}

