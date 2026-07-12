// callconv: eax reg (eax, ebx, ecx, edx);
void fcn.10121580 (int32_t arg1, int32_t arg2, int32_t arg3) {
        // CALL XREF from method.KeyPad.virtual_4 @ 0x1012139a(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x10318478)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x24
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        eax = ecx     // arg3
        dword [var_1ch] = eax
        ebx = 0
        edi = 0
        dword [var_30h] = ebx
        dword [var_2ch] = edi
        dword [var_28h] = ebx
        ecx = dword [eax + 0x48]
        ecx -= dword [eax + 0x44]
        eax = 0x2aaaaaab
        eax = eax * ecx
        edx >>= 1     // arg4
        eax = edx     // arg4
        eax >>>= 0x1f
        eax += edx    // arg4
        esi = 0
        dword [var_4h] = edi
        dword [var_18h] = esi
        dword [var_24h] = eax
        v = eax & eax
        if (v <= 0) goto loc_0x10121793 // likely
        goto loc_0x101215e3;
    loc_0x10121793:
        // CODE XREF from fcn.10121580 @ 0x101215dd(x)
        edi -= ebx
        edi >>= 2
        esi = 0
        v = edi - 1   // 1
        if (v) goto loc_0x101217a6 // likely
        goto loc_0x1012179f;
    loc_0x101217a6:
        // CODE XREF from fcn.10121580 @ 0x1012179d(x)
        if (((unsigned) v) <= 0) goto case.0x101217e0.2 // unlikely
        goto loc_0x101217ac;
    loc_0x10121838:
        // XREFS: CODE 0x101217a1  CODE 0x101217a6  CODE 0x101217de
        // XREFS: CODE 0x101217e0  CODE 0x101217ef  CODE 0x101217f4
        // XREFS: CODE 0x101217f9  CODE 0x10121803  CODE 0x10121808
        // XREFS: CODE 0x1012180d  CODE 0x10121817  CODE 0x1012181c
        // XREFS: CODE 0x10121821  CODE 0x1012182b
        ecx = dword [var_1ch]
        eax = dword [ecx + 8]
        edi = ecx + 8
        v = eax - esi
        if (!v) goto loc_0x10121862 // unlikely
        goto loc_0x10121845;
    loc_0x10121862:
        // CODE XREFS from fcn.10121580 @ 0x10121843(x), 0x10121858(x)
        dword [var_4h] = 0xffffffff // -1
        v = ebx & ebx
        if (!v) goto loc_0x10121876 // likely
        goto loc_0x1012186d;
    loc_0x10121876:
        // CODE XREF from fcn.10121580 @ 0x1012186b(x)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x10121849;
        goto loc_0x101217b0;
        switch (ecx) { // jump table of 7 cases at 0x10121888
            case 1: // 0x101217e7
                // CODE XREF from fcn.10121580 @ 0x101217e0(x)
                v = eax - 3   // 3
                if (v) goto loc_0x101217f1 // likely
                break;
            case 2: // 0x10121838
                goto loc_0x10121838;
            case 3: // 0x101217fb
                // CODE XREF from fcn.10121580 @ 0x101217e0(x)
                v = eax - 1   // 1
                if (v) goto loc_0x10121805 // likely
                break;
            case 4: // 0x10121838
                goto loc_0x10121838;
            case 5: // 0x1012180f
                // CODE XREF from fcn.10121580 @ 0x101217e0(x)
                v = eax - 3   // 3
                if (v) goto loc_0x10121819 // likely
                break;
            case 6: // 0x10121838
                goto loc_0x10121838;
            case 7: // 0x10121823
                // CODE XREF from fcn.10121580 @ 0x101217e0(x)
                v = eax - 1   // 1
                if (v) goto loc_0x1012182d // likely
                break;
            default: // 0x10121838
                goto loc_0x10121838;
        }
    loc_0x101215f0: // orphan
         // CODE XREFS from fcn.10121580 @ 0x101215eb(x), 0x1012178a(x)
         ecx = dword [ebx + 0x48]
         ecx -= dword [ebx + 0x44]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = esi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x1012160a;
    loc_0x1012160a: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x10121617: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121608(x)
         edx = dword [ebx + 0x44]
         ecx = dword [var_14h]
         eax = dword [ecx + edx + 4]
         eax -= dword [ecx + edx]
         eax >>= 4
         v = eax & eax
         if (!v) 
         goto loc_0x1012162f;
    loc_0x1012162f: // orphan
         ecx = dword [ebx + 0x48]
         ecx -= edx
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = esi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10121648;
    loc_0x10121648: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x10121655: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121646(x)
         eax = dword [ebx + 0x44]
         edx = dword [var_14h]
         ecx = dword [ebx + 0x48]
         esi = dword [edx + eax]
         ecx -= eax
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = dword [var_18h] - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10121678;
    loc_0x10121678: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x10121685: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121676(x)
         eax = dword [ebx + 0x44]
         edx = dword [var_14h]
         ecx = dword [edx + eax + 4]
         ecx -= dword [edx + eax]
         ecx >>= 4
         dword [var_20h] = ecx
         ecx = dword [ebx + 0x48]
         ecx -= eax
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = dword [var_18h] - eax
         if (((unsigned) v) < 0) 
         goto loc_0x101216b2;
    loc_0x101216b2: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x101216bf: // orphan
         // CODE XREF from fcn.10121580 @ 0x101216b0(x)
         ecx = dword [var_20h]
         eax = dword [ebx + 0x44]
         edx = dword [var_14h]
         ecx <<<= 4
         ecx += dword [edx + eax]
         dword [var_20h] = ecx
         v = esi - ecx
         jae 0x1012177c           // unlikely

         goto loc_0x101216d9;
    loc_0x101216d9: // orphan
         esp = esp                // ebp

    loc_0x101216e0: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121776(x)
         push (dword [esi])
         fcn.102fc760 ()          // fcn.102fc760(0x0)
         esp += 4
         v = al & 3               // 3
         if (v) 
         goto loc_0x101216ee;
    loc_0x101216ee: // orphan
         v = byte [esi + 8] - 0
         if (!v) 
         goto loc_0x101216f4;
    loc_0x101216f4: // orphan
         eax = ebx + 8
         push (eax)
         push (3)                 // 3
         ecx = ebx
         byte [esi + 8] = 0
         fcn.100ae000 ()          // fcn.100ae000(0x8, 0x0, 0x0)
         
         goto loc_0x10121707;
    loc_0x10121707: // orphan
         // CODE XREF from fcn.10121580 @ 0x101216ec(x)
         v = byte [esi + 8] - 0
         if (v) 
         goto loc_0x1012170d;
    loc_0x1012170d: // orphan
         byte [esi + 8] = 1

    loc_0x10121711: // orphan
         // CODE XREF from fcn.10121580 @ 0x1012170b(x)
         ebx = dword [ebx + 0x5c]
         ecx = ebx
         eax = dword [ebx + 4]
         v = eax & eax
         if (!v) 
         goto loc_0x1012171d;
    loc_0x1012171d: // orphan
         edx = dword [esi]

    loc_0x10121720: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121731(x)
         v = dword [eax + 0x10] - edx
         jl 0x1012172c            // likely

         goto loc_0x10121725;
    loc_0x10121725: // orphan
         ecx = eax
         eax = dword [eax + 8]
         
         goto loc_0x1012172c;
    loc_0x1012172c: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121723(x)
         eax = dword [eax + 0xc]

    loc_0x1012172f: // orphan
         // CODE XREF from fcn.10121580 @ 0x1012172a(x)
         v = eax & eax
         if (v) 
         goto loc_0x10121733;
    loc_0x10121733: // orphan
         // CODE XREF from fcn.10121580 @ 0x1012171b(x)
         v = ecx - ebx
         if (!v) 
         goto loc_0x10121737;
    loc_0x10121737: // orphan
         eax = dword [esi]
         v = eax - dword [ecx + 0x10]
         if (v >= 0) 
         goto loc_0x1012173e;
    loc_0x1012173e: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121735(x)
         ecx = ebx

    loc_0x10121740: // orphan
         // CODE XREF from fcn.10121580 @ 0x1012173c(x)
         ecx += 0x14              // 20
         v = edi - dword [var_28h]
         if (!v) 
         goto loc_0x10121748;
    loc_0x10121748: // orphan
         v = edi & edi
         if (!v) 
         goto loc_0x1012174c;
    loc_0x1012174c: // orphan
         eax = dword [ecx]
         dword [edi] = eax

    loc_0x10121750: // orphan
         // CODE XREF from fcn.10121580 @ 0x1012174a(x)
         edi += 4
         dword [var_2ch] = edi
         
         goto loc_0x10121758;
    loc_0x10121758: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121746(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = var_dh
         push (eax)
         push (ecx)
         push (edi)
         ecx = var_30h
         fcn.100d5be0 ()          // fcn.100d5be0(0x177ff3, 0x0)
         edi = dword [var_2ch]

    loc_0x1012176d: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121756(x)
         ebx = dword [var_1ch]

    loc_0x10121770: // orphan
         // CODE XREFS from fcn.10121580 @ 0x101216f2(x), 0x10121705(x)
         esi += 0x10              // 16
         v = esi - dword [var_20h]
         if (((unsigned) v) < 0) 
         goto loc_0x1012177c;
    loc_0x1012177c: // orphan
         // CODE XREF from fcn.10121580 @ 0x101216d3(x)
         esi = dword [var_18h]

    loc_0x1012177f: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121629(x)
         dword [var_14h] += 0xc   // [0xc:4]=-1 // 12
         esi++
         dword [var_18h] = esi
         v = esi - dword [var_24h]
         jl 0x101215f0            // unlikely

         goto loc_0x10121790;
    loc_0x10121790: // orphan
         ebx = dword [var_30h]

    loc_0x1012179f: // orphan
         esi = dword [ebx]
         
         goto loc_0x101217a6;
    loc_0x101217ac: // orphan
         v = edi & edi
         if (v) 
         goto loc_0x101217b0;
    loc_0x101217b0: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x101217bd: // orphan
         // CODE XREF from fcn.10121580 @ 0x101217ae(x)
         eax = dword [ebx]
         dword [var_24h] = eax
         v = edi - 1              // 1
         if (((unsigned) v) > 0) 
         goto loc_0x101217c7;
    loc_0x101217c7: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x101217d4: // orphan
         // CODE XREF from fcn.10121580 @ 0x101217c5(x)
         ecx = dword [var_24h]
         eax = dword [ebx + 4]
         ecx--
         v = ecx - 6              // 6
         if (((unsigned) v) > 0) 
         goto loc_0x101217e0;
    loc_0x101217ec: // orphan
         esi = eax - 1
         
         goto loc_0x101217f1;
    loc_0x101217f1: // orphan
         // CODE XREF from fcn.10121580 @ 0x101217ea(x)
         v = eax - 7              // 7
         if (v) 
         goto loc_0x101217f6;
    loc_0x101217f6: // orphan
         esi = eax + 1
         
         goto loc_0x101217fb;
    loc_0x10121800: // orphan
         esi = eax + 1
         
         goto loc_0x10121805;
    loc_0x10121805: // orphan
         // CODE XREF from fcn.10121580 @ 0x101217fe(x)
         v = eax - 5              // 5
         if (v) 
         goto loc_0x1012180a;
    loc_0x1012180a: // orphan
         esi = eax - 1
         
         goto loc_0x1012180f;
    loc_0x10121814: // orphan
         esi = eax + 1
         
         goto loc_0x10121819;
    loc_0x10121819: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121812(x)
         v = eax - 7              // 7
         if (v) 
         goto loc_0x1012181e;
    loc_0x1012181e: // orphan
         esi = eax - 1
         
         goto loc_0x10121823;
    loc_0x10121828: // orphan
         esi = eax + 7
         
         goto loc_0x1012182d;
    loc_0x1012182d: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121826(x)
         ecx = 6
         v = eax - 5              // 5
         if (!v) esi = ecx

    loc_0x10121845: // orphan
         v = eax & eax
         if (!v) 
         goto loc_0x10121849;
    loc_0x10121849: // orphan
         push (edi)
         push (3)                 // 3
         fcn.100ae000 ()          // fcn.100ae000(0x0, 0x0, 0x0)
         ecx = dword [var_1ch]

    loc_0x10121854: // orphan
         // CODE XREF from fcn.10121580 @ 0x10121847(x)
         dword [edi] = esi
         v = esi & esi
         if (!v) 
         goto loc_0x1012185a;
    loc_0x1012185a: // orphan
         push (edi)
         push (2)                 // 2
         fcn.100ae000 ()          // fcn.100ae000(0x0, 0x0, 0x0)

    loc_0x1012186d: // orphan
         push (ebx)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

