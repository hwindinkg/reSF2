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
void fcn.1015efd0 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch, int32_t arg_10h, int32_t arg_13h, int32_t arg_14h, int32_t arg_1ch, int32_t arg_1fh, int32_t arg_20h, int32_t arg_23h, int32_t arg_24h, int32_t arg_28h) {
        // CALL XREF from fcn.1015eeb0 @ 0x1015ef20(x)
        push (ebp)
        ebp = esp
        esp -= 0x2c
        eax = arg_ch  // arg3
        push (ebx)    // arg2
        edx = dword [eax + 0xbc]
        push (esi)
        esi = dword [eax + 0xb8]
        push (edi)
        edi = eax + 0xb8
        ecx = edx
        ecx -= edx
        ecx >>= 3
        dword [var_4h] = eax
        dword [var_18h] = edi
        v = ecx & ecx
        if (v <= 0) goto loc_0x1015f015 // likely
        goto loc_0x1015effe;
    loc_0x1015f015:
        // CODE XREF from fcn.1015efd0 @ 0x1015effc(x)
        eax = dword [arg_10h]
        dword [edi + 4] = esi
        ebx = dword [eax + 0x1c]
        eax = dword [eax + 0x20]
        eax -= ebx
        eax >>= 4
        v = eax & eax
        if (!v) goto loc_0x1015f0a9 // likely
        goto loc_0x1015f02a;
    loc_0x1015f0a9:
        // CODE XREFS from fcn.1015efd0 @ 0x1015f028(x), 0x1015f034(x), 0x1015f0db(x), 0x1015f119(x)
        eax = dword [edi + 4]
        eax -= dword [edi]
        edi = pop ()
        esi = pop ()
        eax >>= 3
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x1015f036;
        return eax;
    loc_0x1015f000: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f013(x)
         eax = dword [edx]
         dword [esi] = eax
         eax = dword [edx + 4]
         dword [esi + 4] = eax
         ecx--
         esi += 8
         edx = edx + 8
         v = ecx & ecx
         if (v > 0) 
         goto loc_0x1015f015;
    loc_0x1015f02a: // orphan
         eax <<<= 4
         eax += ebx
         dword [var_14h] = eax
         v = ebx - eax
         jae 0x1015f0a9           // likely

         goto loc_0x1015f036;
    loc_0x1015f036: // orphan
         eax = dword [var_4h]     // ebp
         edx = dword [eax + 0x30]
         esi = dword [eax + 0x34]
         esi -= edx
         dword [var_8h] = edx
         dword [arg_10h] = esi

    loc_0x1015f047: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f0a7(x)
         ecx = dword [ebx + 4]
         eax = dword [ebx + 8]
         eax -= ecx
         v = esi - eax
         if (v) 
         goto loc_0x1015f053;
    loc_0x1015f053: // orphan
         esi -= 4
         if (((unsigned) v) < 0) 
         goto loc_0x1015f058;
    loc_0x1015f058: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f067(x)
         eax = dword [edx]
         v = eax - dword [ecx]
         if (v) 
         goto loc_0x1015f05e;
    loc_0x1015f05e: // orphan
         edx += 4
         ecx += 4
         esi -= 4
         jae 0x1015f058           // unlikely

         goto loc_0x1015f069;
    loc_0x1015f069: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f056(x)
         v = esi - 0xfffffffc
         if (!v) 
         goto loc_0x1015f06e;
    loc_0x1015f06e: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f05c(x)
         al = byte [edx]
         v = al - byte [ecx]
         if (v) 
         goto loc_0x1015f074;
    loc_0x1015f074: // orphan
         v = esi - 0xfffffffd
         if (!v) 
         goto loc_0x1015f079;
    loc_0x1015f079: // orphan
         al = byte [edx + 1]
         v = al - byte [ecx + 1]
         if (v) 
         goto loc_0x1015f081;
    loc_0x1015f081: // orphan
         v = esi - 0xfffffffe
         if (!v) 
         goto loc_0x1015f086;
    loc_0x1015f086: // orphan
         al = byte [edx + 2]
         v = al - byte [ecx + 2]
         if (v) 
         goto loc_0x1015f08e;
    loc_0x1015f08e: // orphan
         v = esi - 0xffffffff
         if (!v) 
         goto loc_0x1015f093;
    loc_0x1015f093: // orphan
         al = byte [edx + 3]
         v = al - byte [ecx + 3]
         if (!v) 
         goto loc_0x1015f09b;
    loc_0x1015f09b: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f072(x), 0x1015f07f(x), 0x1015f08c(x)
         edx = dword [var_8h]
         esi = dword [arg_10h]

    loc_0x1015f0a1: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f051(x)
         ebx += 0x10              // 16
         v = ebx - dword [var_14h]
         if (((unsigned) v) < 0) 
         return eax;
    loc_0x1015f0ba: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f06c(x), 0x1015f077(x), 0x1015f084(x), 0x1015f091(x), 0x1015f099(x)
         eax = dword [ebx]
         dword [arg_10h] = eax
         ebx = dword [eax + 4]
         esi = dword [eax + 8]
         esi -= ebx
         eax = 0x2aaaaaab
         eax = eax * esi
         edx >>= 2
         ecx = edx
         ecx >>>= 0x1f
         dword [var_ch] = ebx
         ecx += edx
         if (!v) 
         goto loc_0x1015f0dd;
    loc_0x1015f0dd: // orphan
         eax = 0x2aaaaaab
         eax = eax * esi
         edx >>= 2
         eax = edx
         eax >>>= 0x1f
         eax += edx
         ecx = eax - 1
         v = ecx - eax
         jae 0x1015f0fd           // likely

         goto loc_0x1015f0f5;
    loc_0x1015f0f5: // orphan
         eax = ecx + ecx*2
         eax = ebx + eax*8
         
         goto loc_0x1015f0fd;
    loc_0x1015f0fd: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f0f3(x)
         push (ecx)
         push (str.Subcontainer_index_error__i) // 0x105aca0c // "Subcontainer index error %i" // (pstr 0x105aca0c) "Subcontainer index error %i"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         eax = dword [arg_10h]
         esp += 8
         eax = dword [eax + 4]

    loc_0x1015f111: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f0fb(x)
         dword [var_10h] = eax
         eax += 0x18              // 24
         v = ebx - eax
         jae 0x1015f0a9           // unlikely

         goto loc_0x1015f11b;
    loc_0x1015f11b: // orphan
         eax = dword [var_4h]
         edx = dword [eax + 0x30]
         esi = dword [eax + 0x34]
         esi -= edx
         dword [var_8h] = edx
         dword [arg_10h] = esi
         esp = esp                // ebp

    loc_0x1015f130: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f197(x)
         ecx = dword [ebx]
         eax = dword [ebx + 4]
         eax -= ecx
         v = esi - eax
         if (v) 
         goto loc_0x1015f13b;
    loc_0x1015f13b: // orphan
         esi -= 4
         if (((unsigned) v) < 0) 
         goto loc_0x1015f140;
    loc_0x1015f140: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f14f(x)
         eax = dword [edx]
         v = eax - dword [ecx]
         if (v) 
         goto loc_0x1015f146;
    loc_0x1015f146: // orphan
         edx += 4
         ecx += 4
         esi -= 4
         jae 0x1015f140           // unlikely

         goto loc_0x1015f151;
    loc_0x1015f151: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f13e(x)
         v = esi - 0xfffffffc
         if (!v) 
         goto loc_0x1015f156;
    loc_0x1015f156: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f144(x)
         al = byte [edx]
         v = al - byte [ecx]
         if (v) 
         goto loc_0x1015f15c;
    loc_0x1015f15c: // orphan
         v = esi - 0xfffffffd
         if (!v) 
         goto loc_0x1015f161;
    loc_0x1015f161: // orphan
         al = byte [edx + 1]
         v = al - byte [ecx + 1]
         if (v) 
         goto loc_0x1015f169;
    loc_0x1015f169: // orphan
         v = esi - 0xfffffffe
         if (!v) 
         goto loc_0x1015f16e;
    loc_0x1015f16e: // orphan
         al = byte [edx + 2]
         v = al - byte [ecx + 2]
         if (v) 
         goto loc_0x1015f176;
    loc_0x1015f176: // orphan
         v = esi - 0xffffffff
         if (!v) 
         goto loc_0x1015f17b;
    loc_0x1015f17b: // orphan
         al = byte [edx + 3]
         v = al - byte [ecx + 3]
         if (!v) 
         goto loc_0x1015f183;
    loc_0x1015f183: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f15a(x), 0x1015f167(x), 0x1015f174(x)
         edx = dword [var_8h]
         esi = dword [arg_10h]

    loc_0x1015f189: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f139(x)
         eax = dword [var_10h]
         ebx += 0x18              // 24
         eax += 0x18              // 24
         dword [var_ch] = ebx
         v = ebx - eax
         if (((unsigned) v) < 0) 
         return eax;
    loc_0x1015f199: // orphan
         eax = dword [edi + 4]
         eax -= dword [edi]
         edi = pop ()
         esi = pop ()
         eax >>= 3
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x1015f1aa: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f154(x), 0x1015f15f(x), 0x1015f16c(x), 0x1015f179(x), 0x1015f181(x)
         ecx = dword [arg_8h]
         fcn.10159770 ()          // fcn.10159770(0x0)
         ecx = eax
         fcn.10164ab0 ()          // fcn.10164ab0(0x0)
         esi = dword [arg_ch]
         ecx = dword [var_4h]
         push (0)
         push (esi)
         byte [arg_13h] = al
         fcn.1015ffe0 ()          // fcn.1015ffe0(0x0, 0x0, 0x1015f1b2, 0x0)
         v = al & al
         if (!v) 
         goto loc_0x1015f1d2;
    loc_0x1015f1d2: // orphan
         ecx = esi
         fcn.101036a0 ()          // fcn.101036a0(0x0)
         esi = dword [arg_20h]
         esi -= dword [arg_1ch]
         ecx = dword [0x10657c9c] // [0x10657c9c:4]=0
         dword [var_14h] = eax
         eax = esi
         cdq
         ecx /=
         v = edx & edx
         if (v) 
         goto loc_0x1015f1f1;
    loc_0x1015f1f1: // orphan
         ecx = esi
         dword [var_8h] = esi
         
         goto loc_0x1015f1f8;
    loc_0x1015f1f8: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f1ef(x)
         v = esi & esi
         if (v <= 0) 
         goto loc_0x1015f1fc;
    loc_0x1015f1fc: // orphan
         ecx -= edx
         ecx += esi
         
         goto loc_0x1015f202;
    loc_0x1015f202: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f1fa(x)
         ecx = esi
         ecx -= edx

    loc_0x1015f206: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f200(x)
         dword [var_8h] = ecx

    loc_0x1015f209: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f1f6(x)
         eax = byte [arg_13h]
         movss xmm1 dword [arg_14h]
         subss xmm1 dword [ebp + 0x18]
         edx = dword [edi]
         ecx -= esi
         movd xmm0 eax
         cvtdq2ps xmm0 xmm0
         dword [var_10h] = ecx
         ecx = dword [edi + 4]
         esi = ecx
         esi -= ecx
         xmm1 = xmm1 * xmm0
         esi >>= 3
         addss xmm1 dword [arg_24h]
         v = esi & esi
         if (v <= 0) 
         goto loc_0x1015f23c;
    loc_0x1015f23c: // orphan
         esp = esp                // ebp

    loc_0x1015f240: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f253(x)
         eax = dword [ecx]
         dword [edx] = eax
         eax = dword [ecx + 4]
         dword [edx + 4] = eax
         esi--
         ecx += 8
         edx += 8
         v = esi & esi
         if (v > 0) 
         goto loc_0x1015f255;
    loc_0x1015f255: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f23a(x)
         push (edi)
         push (ecx)
         ecx = dword [var_4h]     // int32_t arg_ch
         movss dword [esp] xmm1
         push (dword [var_8h])
         dword [edi + 4] = edx
         push (dword [var_14h])
         push (ebx)
         fcn.1015fa80 ()          // fcn.1015fa80(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0)
         eax = dword [edi]
         ecx = dword [edi + 4]
         ecx -= eax
         ecx >>= 3
         dword [arg_14h] = eax
         v = ecx & ecx
         if (!v) 
         goto loc_0x1015f27f;
    loc_0x1015f27f: // orphan
         ecx = eax + ecx*8
         dword [var_8h] = ecx
         v = eax - ecx
         jae 0x1015f2e2           // likely

         goto loc_0x1015f289;
    loc_0x1015f289: // orphan
         esi = dword [arg_ch]
         esp = esp                // ebp

    loc_0x1015f290: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2e0(x)
         v = esi - dword [eax]
         if (v) 
         goto loc_0x1015f294;
    loc_0x1015f294: // orphan
         edx = dword [edi + 4]
         eax = dword [eax + 4]
         eax -= dword [arg_1ch]
         ecx = dword [edi]
         eax += dword [var_10h]
         esi = edx
         esi -= edx
         esi >>= 3
         dword [var_14h] = eax
         v = esi & esi
         if (v <= 0) 
         goto loc_0x1015f2b0;
    loc_0x1015f2b0: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2c3(x)
         eax = dword [edx]
         dword [ecx] = eax
         eax = dword [edx + 4]
         dword [ecx + 4] = eax
         esi--
         ecx += 8
         edx = edx + 8
         v = esi & esi
         if (v > 0) 
         goto loc_0x1015f2c5;
    loc_0x1015f2c5: // orphan
         eax = dword [var_14h]

    loc_0x1015f2c8: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2ae(x)
         dword [edi + 4] = ecx
         v = eax & eax
         if (v > 0) 
         goto loc_0x1015f2cf;
    loc_0x1015f2cf: // orphan
         eax = dword [arg_14h]
         ecx = dword [var_8h]
         esi = dword [arg_ch]

    loc_0x1015f2d8: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f292(x)
         eax += 8
         dword [arg_14h] = eax
         v = eax - ecx
         if (((unsigned) v) < 0) 
         goto loc_0x1015f2e2;
    loc_0x1015f2e2: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f1cc(x), 0x1015f27d(x), 0x1015f287(x)
         esi = dword [arg_20h]
         ecx = dword [0x10657c9c] // [0x10657c9c:4]=0
         eax = esi
         cdq
         ecx /=
         v = edx & edx
         if (!v) 
         goto loc_0x1015f2f4;
    loc_0x1015f2f4: // orphan
         ecx -= edx
         ecx += esi
         dword [arg_14h] = ecx
         
         goto loc_0x1015f2fd;
    loc_0x1015f2fd: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2cd(x)
         edx = 0
         dword [var_2ch] = edx
         dword [var_28h] = eax
         v = ecx - dword [edi + 8]
         if (!v) 
         goto loc_0x1015f30a;
    loc_0x1015f30a: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x1015f30e;
    loc_0x1015f30e: // orphan
         dword [ecx] = edx
         dword [ecx + 4] = eax

    loc_0x1015f313: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f30c(x)
         dword [edi + 4] += 8
         eax = dword [edi + 4]
         eax -= dword [edi]
         edi = pop ()
         esi = pop ()
         eax >>= 3
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x1015f328: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f308(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = arg_23h
         push (eax)
         eax = var_2ch
         push (eax)
         push (ecx)
         ecx = edi
         fcn.10200f40 ()          // fcn.10200f40(0x177fd4, 0x0)
         eax = dword [edi + 4]
         eax -= dword [edi]
         edi = pop ()
         esi = pop ()
         eax >>= 3
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x1015f34d: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2f2(x)
         dword [arg_14h] = esi

    loc_0x1015f350: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f2fb(x)
         ecx = dword [edi + 4]
         esi = dword [edi]
         edx = ecx
         edx -= ecx
         edx >>= 3
         v = edx & edx
         if (v <= 0) 
         goto loc_0x1015f360;
    loc_0x1015f360: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f373(x)
         eax = dword [ecx]
         dword [esi] = eax
         eax = dword [ecx + 4]
         dword [esi + 4] = eax
         edx--
         esi += 8
         ecx = ecx + 8
         v = edx & edx
         if (v > 0) 
         goto loc_0x1015f375;
    loc_0x1015f375: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f35e(x)
         eax = dword [var_4h]
         dword [edi + 4] = esi
         ecx = dword [eax + 0x24]
         fcn.10164ab0 ()          // fcn.10164ab0(0x1)
         ecx = dword [ebx + 0x14]
         ecx -= dword [ebx + 0x10]
         byte [arg_1fh] = al
         eax = 0x92492493
         eax = eax * ecx
         edx += ecx
         edx >>= 4
         eax = edx
         eax >>>= 0x1f
         eax += edx
         dword [var_14h] = 0
         dword [var_28h] = eax
         dword [var_8h] = 0
         if (!v) 
         goto loc_0x1015f3b6;
    loc_0x1015f3b6: // orphan
         eax = dword [arg_ch]
         edi = byte [arg_13h]
         esi = byte [arg_1fh]
         eax += 0x34              // 52
         dword [var_10h] = eax
         dword [arg_10h] = 0
         edi = edi

    loc_0x1015f3d0: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f534(x)
         ecx = dword [ebx + 0x14]
         ecx -= dword [ebx + 0x10]
         eax = 0x92492493
         eax = eax * ecx
         edx += ecx
         ecx = dword [var_8h]
         edx >>= 4
         eax = edx
         eax >>>= 0x1f
         eax += edx
         v = ecx - eax
         jae 0x1015f3f8           // likely

         goto loc_0x1015f3f0;
    loc_0x1015f3f0: // orphan
         eax = dword [ebx + 0x10]
         eax += dword [arg_10h]
         
         goto loc_0x1015f3f8;
    loc_0x1015f3f8: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f3ee(x)
         push (ecx)
         push (str.Subcontainer_index_error__i) // 0x105aca0c // "Subcontainer index error %i" // (pstr 0x105aca0c) "Subcontainer index error %i"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         eax = dword [ebx + 0x10]
         esp += 8

    loc_0x1015f409: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f3f6(x)
         ecx = dword [arg_28h]
         eax += 0xc               // 12
         dword [arg_ch] = eax
         v = ecx & ecx
         if (v) 
         goto loc_0x1015f41a;
    loc_0x1015f41a: // orphan
         eax = dword [var_4h]
         eax = dword [eax + 0x14]
         ecx = eax
         dword [arg_1ch] = eax
         fcn.10159800 ()          // fcn.10159800(-1)
         ecx = eax
         fcn.1023d540 ()          // fcn.1023d540(-1)
         ecx = eax
         fcn.1016c5d0 ()          // fcn.1016c5d0(-1)
         ecx = dword [arg_8h]
         ebx = eax
         fcn.10159800 ()          // fcn.10159800(-1)
         ecx = eax
         fcn.1023d540 ()          // fcn.1023d540(-1)
         ecx = eax
         fcn.1016c5d0 ()          // fcn.1016c5d0(-1)
         movss xmm0 dword [eax]
         comiss xmm0 dword [ebx]
         ecx = dword [arg_1ch]
         seta al
         eax = eax*2 - 1
         byte [var_1ch] = al
         push (dword [var_1ch])
         push (dword [arg_ch])
         fcn.10159770 ()          // fcn.10159770(0xfffffe01)
         ecx = eax
         fcn.101649d0 ()          // fcn.101649d0(0xfffffe01, -1, 0xfffffe01)
         v = eax & eax
         if (!v) 
         goto loc_0x1015f47d;
    loc_0x1015f47d: // orphan
         ecx = eax
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         movss xmm0 dword [eax]
         ebx = dword [var_ch]
         
         goto loc_0x1015f48d;
    loc_0x1015f48d: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f47b(x)
         movss xmm0 dword [0x1064c938] // [0x1064c938:4]=0x799a130c
         ebx = dword [var_ch]
         
         goto loc_0x1015f49a;
    loc_0x1015f49a: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f414(x)
         push (ecx)
         ecx = dword [var_4h]     // int32_t arg_ch
         push (eax)
         fcn.1015f740 ()          // fcn.1015f740(0x0, 0x0, 0x0)
         fstp dword [ebp + 0x1c]
         movss xmm0 dword [arg_1ch]

    loc_0x1015f4ac: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f48b(x), 0x1015f498(x)
         eax = dword [arg_ch]     // int32_t arg1
         ecx = dword [var_10h]    // int32_t arg_8h
         push (dword [eax])
         movss dword [arg_1ch] xmm0
         push (dword [arg_20h])
         fcn.1015da00 ()          // fcn.1015da00(0xc, 0x0, 0x1015f442, 0x0)
         fstp dword [ebp - 0x24]
         eax = dword [arg_ch]     // int32_t arg1
         ecx = dword [var_10h]    // int32_t arg_8h
         push (dword [eax])
         push (dword [arg_14h])
         fcn.1015da00 ()          // fcn.1015da00(0xc, 0x0, 0x1015f442, 0x0)
         fstp dword [ebp - 0x20]
         movss xmm0 dword [var_20h]
         subss xmm0 dword [ebp - 0x24]
         push (dword [var_18h])
         movd xmm1 esi
         cvtdq2ps xmm1 xmm1
         push (ecx)
         xmm0 = xmm0 * xmm1
         ecx = dword [var_4h]     // int32_t arg_ch
         movd xmm1 edi
         addss xmm0 dword [arg_1ch]
         cvtdq2ps xmm1 xmm1
         subss xmm0 dword [ebp + 0x18]
         xmm0 = xmm0 * xmm1
         addss xmm0 dword [arg_24h]
         movss dword [esp] xmm0
         push (dword [arg_14h])
         push (dword [arg_ch])
         push (ebx)
         fcn.1015fa80 ()          // fcn.1015fa80(0xc, 0x0, -1, 0x0, 0x0, 0x0, 0x0)
         ecx = dword [var_14h]
         dword [arg_10h] += 0x1c  // [0x1c:4]=-1 // 28
         ecx += eax
         eax = dword [var_8h]
         eax++
         dword [var_14h] = ecx
         dword [var_8h] = eax
         v = eax - dword [var_28h]
         if (((unsigned) v) < 0) 
         goto loc_0x1015f53a;
    loc_0x1015f53a: // orphan
         edi = dword [var_18h]
         
         goto loc_0x1015f53f;
    loc_0x1015f53f: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f3b0(x)
         ecx = 0

    loc_0x1015f541: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f53d(x)
         ebx = dword [arg_14h]
         eax = dword [arg_20h]
         v = ebx - eax
         if (!v) 
         goto loc_0x1015f54f;
    loc_0x1015f54f: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x1015f553;
    loc_0x1015f553: // orphan
         edx = dword [edi + 4]
         ecx = dword [edi]
         esi = edx
         esi -= edx
         esi >>= 3
         v = esi & esi
         if (v <= 0) 
         goto loc_0x1015f563;
    loc_0x1015f563: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f576(x)
         eax = dword [edx]
         dword [ecx] = eax
         eax = dword [edx + 4]
         dword [ecx + 4] = eax
         esi--
         ecx += 8
         edx = edx + 8
         v = esi & esi
         if (v > 0) 
         goto loc_0x1015f578;
    loc_0x1015f578: // orphan
         eax = dword [arg_20h]

    loc_0x1015f57b: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f561(x)
         edx = 0
         ebx -= eax
         dword [edi + 4] = ecx
         dword [var_2ch] = edx
         dword [var_28h] = ebx
         v = ecx - dword [edi + 8]
         if (!v) 
         goto loc_0x1015f58d;
    loc_0x1015f58d: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x1015f591;
    loc_0x1015f591: // orphan
         dword [ecx] = edx
         dword [ecx + 4] = ebx

    loc_0x1015f596: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f58f(x)
         dword [edi + 4] += 8
         edi = pop ()
         esi = pop ()
         eax = 1
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x1015f5a8: // orphan
         // CODE XREF from fcn.1015efd0 @ 0x1015f58b(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = arg_23h
         push (eax)
         eax = var_2ch
         push (eax)
         push (ecx)
         ecx = edi
         fcn.10200f40 ()          // fcn.10200f40(0x177fd4, 0x0)
         edi = pop ()
         esi = pop ()
         eax = 1
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x1015f5ca: // orphan
         // CODE XREFS from fcn.1015efd0 @ 0x1015f549(x), 0x1015f551(x)
         edi = pop ()
         esi = pop ()
         eax = ecx
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

