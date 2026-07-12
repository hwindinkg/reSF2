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
void fcn.10161ad0 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_bh) {
        // CALL XREF from fcn.10161350 @ 0x1016172c(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031d820)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x60
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        edi = ecx     // arg3
        dword [var_14h] = edi
        esi = dword [edi + 0xbc]
        ecx = dword [edi + 0xb8]
        ebx = edi + 0xb8
        edx = esi
        edx -= esi
        edx >>= 3
        dword [var_34h] = ebx
        v = edx & edx
        if (v <= 0) goto loc_0x10161b34 // likely
        goto loc_0x10161b1d;
    loc_0x10161b34:
        // CODE XREF from fcn.10161ad0 @ 0x10161b1b(x)
        dword [ebx + 4] = ecx
        ecx = dword [arg_8h]
        fcn.10159770 () // fcn.10159770(0x0)
        esi = eax
        ecx = esi
        dword [var_20h] = esi
        fcn.10164aa0 () // fcn.10164aa0(0x0)
        ecx = dword [edi + 0x24]
        dword [var_28h] = eax
        fcn.10164aa0 () // fcn.10164aa0(0x0)
        ecx = dword [var_28h]
        dword [var_24h] = eax
        v = ecx & ecx
        if (!v) goto loc_0x10161ba0 // likely
        goto loc_0x10161b60;
    loc_0x10161ba0:
        // CODE XREFS from fcn.10161ad0 @ 0x10161b5e(x), 0x10161b62(x)
        xmm0 ^= xmm0
        
    loc_0x10161ba3:
        // CODE XREF from fcn.10161ad0 @ 0x10161b9e(x)
        movss dword [var_18h] xmm0
        goto loc_0x10161b64;
        return eax;
    loc_0x10161b20: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161b32(x)
         eax = dword [esi + ecx]
         dword [ecx] = eax
         eax = dword [esi + ecx + 4]
         dword [ecx + 4] = eax
         edx--
         ecx += 8
         v = edx & edx
         if (v > 0) 
         goto loc_0x10161b34;
    loc_0x10161b60: // orphan
         v = eax & eax
         if (!v) 
         goto loc_0x10161b64;
    loc_0x10161b64: // orphan
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         ecx = dword [var_24h]
         esi = eax
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         movss xmm0 dword [esi]
         subss xmm0 dword [eax]
         esi = dword [var_20h]
         comiss xmm0 dword [0x103744cc]
         if (((unsigned) v) < 0) 
         goto loc_0x10161b87;
    loc_0x10161b87: // orphan
         movss xmm1 dword [0x1037439c] // [0x1037439c:4]=0x3f800000
         movss dword [var_18h] xmm1
         
         goto loc_0x10161b96;
    loc_0x10161b96: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161b85(x)
         movss xmm0 dword [0x103744e4] // [0x103744e4:4]=0xbf800000
         
         goto loc_0x10161ba0;
    loc_0x10161ba8: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161b94(x)
         ecx = esi
         fcn.10164ab0 ()          // fcn.10164ab0(0x0)
         eax = al
         movd xmm0 eax
         cvtdq2ps xmm0 xmm0
         xmm0 = xmm0 * dword [var_18h]
         comiss xmm0 dword [0x103744cc]
         if (((unsigned) v) <= 0) 
         goto loc_0x10161bc7;
    loc_0x10161bc7: // orphan
         ecx = dword [edi + 0x14]
         push (0)
         fcn.1015a370 ()          // fcn.1015a370(0x0, 0x0)
         dword [edi + 0x68] = 0xfffffffe // [0xfffffffe:4]=-1 // 4294967294
         byte [edi + 0x51] = 1
         dword [edi + 0x48] = 0x88ca6c00 // [0x88ca6c00:4]=-1
         dword [edi] = 3

    loc_0x10161be9: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161c64(x), 0x10162500(x)
         eax = 0
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161bff: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161bc5(x)
         v = byte [edi + 0x74] - 0
         if (!v) 
         goto loc_0x10161c05;
    loc_0x10161c05: // orphan
         push (dword [arg_8h])
         ecx = edi                // int32_t arg_8h
         fcn.1015fc80 ()          // fcn.1015fc80(0x0, 0x0, 0x0)
         v = al & al
         if (!v) 
         return eax;
    loc_0x10161c13: // orphan
         push (dword [arg_8h])
         ecx = edi                // int32_t arg_8h
         fcn.1015dad0 ()          // fcn.1015dad0(0x0, 0x0, 0x0)
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()             // ebp
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161c31: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161c03(x), 0x10161c11(x)
         ecx = esi
         fcn.10164850 ()          // fcn.10164850(0x0)
         v = dword [edi + 0x4c] - eax
         if (v >= 0) 
         goto loc_0x10161c41;
    loc_0x10161c41: // orphan
         push (esi)
         fcn.10160520 ()          // fcn.10160520(0x0, 0x0)
         esp += 4
         v = al & al
         if (v) 
         goto loc_0x10161c52;
    loc_0x10161c52: // orphan
         push (dword [edi + 0xc])
         ecx = dword [edi + 0x54]
         push (str.Uninterrupt)   // 0x105ac8a0 // "Uninterrupt" // (pstr 0x105ac8a0) "Uninterrupt"
         fcn.10103d50 ()          // fcn.10103d50(0x0, 0x0, -1)
         v = al & al
         if (!v) 
         goto loc_0x10161c66;
    loc_0x10161c66: // orphan
         push (esi)
         fcn.1015ff10 ()          // fcn.1015ff10(0x0, 0x0)
         esp += 4
         v = al & al
         if (v) 
         goto loc_0x10161c77;
    loc_0x10161c77: // orphan
         eax = dword [edi + 0x70]
         eax -= 2
         if (!v) 
         goto loc_0x10161c7f;
    loc_0x10161c7f: // orphan
         eax--
         if (!v) 
         goto loc_0x10161c82;
    loc_0x10161c82: // orphan
         eax--
         if (!v) 
         return eax;
    loc_0x10161c85: // orphan
         ecx = edi
         fcn.10162750 ()          // fcn.10162750(0x0)
         dword [edi] = 1
         eax = 0
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()             // ebp
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161ca8: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161c83(x)
         dword [edi + 0x68] = 0xa
         eax = 0
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161cc5: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161c80(x)
         push (0)
         push (dword [arg_8h])
         
         goto loc_0x10161ccc;
    loc_0x10161ccc: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161c7d(x)
         ebx = dword [arg_8h]     // int32_t arg2
         push (ebx)
         ecx = edi                // int32_t arg3
         fcn.1015eeb0 ()          // fcn.1015eeb0(0x0, 0x0, 0x0, 0x0)
         v = eax & eax
         if (!v) 
         return eax;
    loc_0x10161cdb: // orphan
         dword [edi + 0x68] = 1
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161cf6: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161cd9(x)
         push (0)
         push (ebx)

    loc_0x10161cf9: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161cca(x)
         ecx = edi                // int32_t arg_8h
         fcn.1015e410 ()          // fcn.1015e410(0x0, 0x0, 0x0, 0x0, 0x0)
         v = eax & eax
         if (!v) 
         return eax;
    loc_0x10161d08: // orphan
         dword [edi + 0x68] = 2
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161d23: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161c71(x)
         v = byte [edi + 0x81] - 0
         esi = dword [arg_8h]
         if (!v) 
         goto loc_0x10161d2f;
    loc_0x10161d2f: // orphan
         push (esi)
         ecx = edi                // int32_t arg3
         fcn.1015eeb0 ()          // fcn.1015eeb0(0x0, 0x0, 0x0, 0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10161d3b;
    loc_0x10161d3b: // orphan
         dword [edi + 0x68] = 1

    loc_0x10161d42: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161d39(x)
         v = byte [edi + 0x80] - 0
         if (v) 
         goto loc_0x10161d4f;
    loc_0x10161d4f: // orphan
         v = eax & eax
         if (v) 
         goto loc_0x10161d57;
    loc_0x10161d57: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161d2d(x)
         v = byte [edi + 0x82] - 0
         if (!v) 
         goto loc_0x10161d60;
    loc_0x10161d60: // orphan
         push (esi)
         ecx = edi                // int32_t arg_8h
         fcn.1015dc70 ()          // fcn.1015dc70(0x0, 0x0, 0x0, 0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10161d6c;
    loc_0x10161d6c: // orphan
         dword [edi + 0x68] = 0

    loc_0x10161d73: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161d6a(x)
         v = byte [edi + 0x80] - 0
         if (v) 
         goto loc_0x10161d80;
    loc_0x10161d80: // orphan
         v = eax & eax
         if (v) 
         goto loc_0x10161d88;
    loc_0x10161d88: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161d5e(x)
         v = byte [edi + 0x9c] - 0
         if (!v) 
         goto loc_0x10161d95;
    loc_0x10161d95: // orphan
         fcn.10052d40 ()
         ecx = esi
         dword [var_24h] = eax
         fcn.10159770 ()          // fcn.10159770(0x0)
         ecx = eax
         fcn.1002c4c0 ()          // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
         esi = 0
         dword [var_28h] = eax
         dword [var_18h] = esi
         v = eax & eax
         if (!v) 
         goto loc_0x10161db7;
    loc_0x10161db7: // orphan
         ecx = dword [arg_8h]
         fcn.10159770 ()          // fcn.10159770(0x0)
         ecx = eax
         fcn.10164c10 ()          // method.CCLabelBMFontExtended.3.virtual_12 // fcn.10164c10(0x0)
         v = al & al
         if (!v) 
         goto loc_0x10161dca;
    loc_0x10161dca: // orphan
         ecx = dword [var_28h]
         push (1)                 // 1
         fcn.10103510 ()          // fcn.10103510(0x0, 0x0, 0x0)
         esi = 1
         esi -= dword [edi + 0xc]
         esi += eax
         dword [var_18h] = esi

    loc_0x10161de1: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161db5(x), 0x10161dc8(x)
         ecx = ebx
         fcn.1015d7b0 ()          // fcn.1015d7b0(0x0)
         edx = dword [var_24h]
         eax = dword [edx + 4]
         edx = dword [edx]
         dword [var_1ch] = eax
         eax -= edx
         eax >>= 2
         dword [var_20h] = edx
         v = eax & eax
         if (!v) 
         goto loc_0x10161e03;
    loc_0x10161e03: // orphan
         ecx = dword [edx]
         v = edx - dword [var_1ch]
         jae 0x10161e8e           // likely

         goto loc_0x10161e0e;
    loc_0x10161e0e: // orphan
         edi = edi

    loc_0x10161e10: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161e89(x)
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         edx = dword [eax + 4]
         ecx = dword [eax]
         eax = edx
         eax -= ecx
         eax >>= 2
         dword [var_28h] = edx
         dword [arg_8h] = ecx
         v = eax & eax
         if (!v) 
         goto loc_0x10161e2b;
    loc_0x10161e2b: // orphan
         edi = dword [ecx]
         v = ecx - edx
         jae 0x10161e77           // likely

         goto loc_0x10161e31;
    loc_0x10161e31: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161e75(x)
         ecx = edi
         fcn.10103650 ()          // fcn.10103650(0x0)
         v = dword [var_18h] - eax
         if (v <= 0) 
         goto loc_0x10161e3d;
    loc_0x10161e3d: // orphan
         ecx = edi
         fcn.10103650 ()          // fcn.10103650(0x0)
         esi = eax

    loc_0x10161e46: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161e3b(x)
         ecx = edi
         fcn.10103670 ()          // fcn.10103670(0x0)
         v = eax - esi
         cmovg esi eax
         eax = var_38h            // int32_t arg1
         push (eax)
         ecx = ebx
         dword [var_38h] = edi
         dword [var_34h] = esi
         fcn.10161100 ()          // fcn.10161100(0x177fc8, 0x0)
         eax = dword [arg_8h]
         esi = dword [var_18h]
         edi = dword [eax + 4]
         eax += 4
         dword [arg_8h] = eax
         v = eax - dword [var_28h]
         if (((unsigned) v) < 0) 
         goto loc_0x10161e77;
    loc_0x10161e77: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161e29(x), 0x10161e2f(x)
         eax = dword [var_20h]
         esi = dword [var_18h]
         ecx = dword [eax + 4]
         eax += 4
         dword [var_20h] = eax
         v = eax - dword [var_1ch]
         if (((unsigned) v) < 0) 
         goto loc_0x10161e8b;
    loc_0x10161e8b: // orphan
         edi = dword [var_14h]

    loc_0x10161e8e: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161dfd(x), 0x10161e08(x)
         eax = dword [ebx + 4]
         eax -= dword [ebx]
         eax >>= 3
         v = eax & eax
         if (!v) 
         goto loc_0x10161e9a;
    loc_0x10161e9a: // orphan
         dword [edi + 0x68] = 5

    loc_0x10161ea1: // orphan
         // XREFS: CODE 0x10161d49  CODE 0x10161d51  CODE 0x10161d7a   // XREFS: CODE 0x10161d82  CODE 0x10161e98  CODE 0x101622b0   // XREFS: CODE 0x101622e5  CODE 0x101624d1  
         eax = dword [ebx + 4]
         eax -= dword [ebx]
         eax >>= 3

    loc_0x10161ea9: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161d02(x), 0x101622ed(x)
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161ebd: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161d8f(x)
         ecx = edi
         fcn.10162750 ()          // fcn.10162750(0x0)
         dword [edi] = 2
         eax = 0
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()             // ebp
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x10161ee0: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161c3b(x), 0x10161c4c(x)
         ecx = dword [edi + 0x88]
         ecx -= dword [edi + 0x84]
         al = 0
         byte [var_dh] = al
         eax = 0x2aaaaaab
         eax = eax * ecx          // int32_t arg1
         ecx = dword [edi + 0x6c]
         edx >>= 1
         esi = edx
         esi >>>= 0x1f
         esi += edx
         dword [var_28h] = esi
         fcn.100a2c30 ()          // method.DisplayZone.virtual_496 // fcn.100a2c30(0x0)
         edx = eax
         dword [var_24h] = edx
         v = esi & esi
         if (!v) 
         goto loc_0x10161f19;
    loc_0x10161f19: // orphan
         ecx = 0
         esi = 0
         dword [var_20h] = ecx
         dword [var_1ch] = esi

    loc_0x10161f23: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016200c(x)
         eax = dword [edi + 0x84]
         v = byte [esi + eax + 8] - 0
         if (!v) 
         goto loc_0x10161f34;
    loc_0x10161f34: // orphan
         eax = dword [edx]
         dword [var_44h] = 0
         dword [var_40h] = 0
         dword [var_3ch] = 0
         edx = var_44h
         push (edx)
         push (dword [ecx + eax])
         dword [var_4h] = 0
         fcn.100593f0 ()          // fcn.100593f0(0x2, 0x0, 0x0)
         eax = dword [var_40h]
         edi = dword [var_44h]
         dword [var_18h] = eax
         eax -= edi
         eax >>= 2
         esp += 8
         v = eax & eax
         if (!v) 
         goto loc_0x10161f73;
    loc_0x10161f73: // orphan
         eax = dword [var_18h]
         esi = dword [edi]
         v = edi - eax
         jae 0x10161fdd           // likely

         goto loc_0x10161f7c;
    loc_0x10161f7c: // orphan
         esp = esp                // ebp

    loc_0x10161f80: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161fd8(x)
         v = esi & esi
         if (!v) 
         goto loc_0x10161f84;
    loc_0x10161f84: // orphan
         ecx = dword [var_14h]    // int32_t arg_8h
         push (esi)
         fcn.10160270 ()          // fcn.10160270(0x0, 0x0, 0x0)
         v = al & al
         if (!v) 
         goto loc_0x10161f91;
    loc_0x10161f91: // orphan
         ecx = esi
         fcn.10103670 ()          // fcn.10103670(0x0)
         ecx = dword [ebx + 4]
         dword [var_30h] = esi
         dword [var_2ch] = eax
         v = ecx - dword [ebx + 8]
         if (!v) 
         goto loc_0x10161fa6;
    loc_0x10161fa6: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x10161faa;
    loc_0x10161faa: // orphan
         dword [ecx] = esi
         dword [ecx + 4] = eax

    loc_0x10161faf: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161fa8(x)
         dword [ebx + 4] += 8
         
         goto loc_0x10161fb5;
    loc_0x10161fb5: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161fa4(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = var_dh
         push (eax)
         eax = var_30h
         push (eax)
         push (ecx)
         ecx = ebx
         fcn.10200f40 ()          // fcn.10200f40(0x177fd0, 0x0)

    loc_0x10161fc9: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161fb3(x)
         byte [var_dh] = 1

    loc_0x10161fcd: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161f8f(x)
         eax = dword [var_18h]

    loc_0x10161fd0: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161f82(x)
         esi = dword [edi + 4]
         edi += 4
         v = edi - eax
         if (((unsigned) v) < 0) 
         goto loc_0x10161fda;
    loc_0x10161fda: // orphan
         edi = dword [var_44h]

    loc_0x10161fdd: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161f7a(x)
         esi = dword [var_1ch]

    loc_0x10161fe0: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161f71(x)
         dword [var_4h] = 0xffffffff // -1
         v = edi & edi
         if (!v) 
         goto loc_0x10161feb;
    loc_0x10161feb: // orphan
         push (edi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10161ff4: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161fe9(x)
         edx = dword [var_24h]
         ecx = dword [var_20h]
         edi = dword [var_14h]

    loc_0x10161ffd: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10161f2e(x)
         esi += 0xc               // 12
         ecx += 0x4c              // 76
         dword [var_28h]--
         dword [var_1ch] = esi
         dword [var_20h] = ecx
         if (v) 
         goto loc_0x10162012;
    loc_0x10162012: // orphan
         al = byte [var_dh]
         v = al & al
         if (!v) 
         goto loc_0x10162019;
    loc_0x10162019: // orphan
         dword [edi + 0x68] = 6

    loc_0x10162020: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10161f13(x), 0x10162017(x)
         byte [edi + 0xb4] = 0
         ecx = dword [edi + 0x94]
         ecx -= dword [edi + 0x90]
         eax = 0x2aaaaaab
         eax = eax * ecx
         ecx = dword [edi + 0x6c]
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         dword [var_1ch] = eax
         fcn.10243740 ()          // fcn.10243740(0x0)
         v = dword [var_1ch] - 0
         edx = eax
         dword [var_28h] = edx
         dword [var_24h] = 0
         if (((unsigned) v) <= 0) 
         goto loc_0x10162064;
    loc_0x10162064: // orphan
         ebx = 0
         ecx = 0
         dword [var_20h] = ecx
         
    loc_0x10162070: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x1016206b(x), 0x10162139(x)
         v = byte [edi + 0xb4] - 0
         if (v) 
         goto loc_0x1016207d;
    loc_0x1016207d: // orphan
         eax = dword [edi + 0x90]
         v = byte [ecx + eax + 8] - 0
         if (!v) 
         goto loc_0x1016208e;
    loc_0x1016208e: // orphan
         eax = dword [edx]
         dword [var_44h] = 0
         dword [var_40h] = 0
         dword [var_3ch] = 0
         ecx = var_44h
         push (ecx)
         push (dword [ebx + eax])
         dword [var_4h] = 1
         fcn.100593f0 ()          // fcn.100593f0(0x0, 0x0, 0x177fbc)
         ecx = dword [arg_8h]
         esp += 8
         fcn.10158fc0 ()          // fcn.10158fc0(0x0)
         esi = dword [var_44h]
         dword [var_18h] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x101620cd;
    loc_0x101620cd: // orphan
         edx = dword [var_40h]
         ecx = edx
         ecx -= esi
         ecx >>= 2
         dword [var_2ch] = edx
         v = ecx & ecx
         if (!v) 
         goto loc_0x101620de;
    loc_0x101620de: // orphan
         eax = dword [esi]
         v = esi - edx
         jae 0x1016210e           // likely

         goto loc_0x101620e4;
    loc_0x101620e4: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162100(x)
         v = eax & eax
         if (!v) 
         goto loc_0x101620e8;
    loc_0x101620e8: // orphan
         ecx = dword [var_18h]    // int32_t arg_8h
         push (eax)
         fcn.10160270 ()          // fcn.10160270(0x0, 0x0, 0x0)
         v = al & al
         if (v) 
         goto loc_0x101620f5;
    loc_0x101620f5: // orphan
         edx = dword [var_2ch]

    loc_0x101620f8: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101620e6(x)
         eax = dword [esi + 4]
         esi += 4
         v = esi - edx
         if (((unsigned) v) < 0) 
         goto loc_0x10162102;
    loc_0x10162102: // orphan
         
         goto loc_0x10162104;
    loc_0x10162104: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101620f3(x)
         byte [edi + 0xb4] = 1

    loc_0x1016210b: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162102(x)
         esi = dword [var_44h]

    loc_0x1016210e: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x101620cb(x), 0x101620dc(x), 0x101620e2(x)
         dword [var_4h] = 0xffffffff // -1
         v = esi & esi
         if (!v) 
         goto loc_0x10162119;
    loc_0x10162119: // orphan
         push (esi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10162122: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162117(x)
         edx = dword [var_28h]

    loc_0x10162125: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162088(x)
         eax = dword [var_24h]
         dword [var_20h] += 0xc   // [0xc:4]=-1 // 12
         ecx = dword [var_20h]
         eax++
         ebx += 0x4c              // 76
         dword [var_24h] = eax
         v = eax - dword [var_1ch]
         if (((unsigned) v) < 0) 
         goto loc_0x1016213f;
    loc_0x1016213f: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162077(x)
         ebx = edi + 0xb8

    loc_0x10162145: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016205e(x)
         ecx = dword [edi + 0x14]
         fcn.101596d0 ()          // fcn.101596d0(0x0)
         edi = eax
         eax = dword [var_14h]
         ecx = dword [eax + 0x14]
         fcn.10159150 ()          // fcn.10159150(0x0)
         esi = eax
         eax = dword [var_14h]
         ecx = dword [eax + 0x14]
         fcn.10159820 ()          // fcn.10159820(0x0)
         dword [var_48h] = eax
         eax = var_50h
         push (eax)
         eax = var_68h
         push (eax)
         dword [var_58h] = esi
         esi = dword [var_14h]
         eax = var_6ch
         ecx = dword [esi + 0x14]
         push (eax)
         push (dword [esi + 0x54])
         dword [var_6ch] = 0
         push (1)                 // 1
         dword [var_68h] = 0
         dword [var_64h] = 0
         dword [var_60h] = 0
         dword [var_5ch] = 0
         dword [var_54h] = edi
         dword [var_50h] = 0
         dword [var_4ch] = 0
         fcn.10159820 ()          // fcn.10159820(0x177f94)
         ecx = eax
         fcn.10175530 ()          // fcn.10175530(0x177f94, 0x0, 0x177f94, 0x0, 0x177f98, 0x0, 0x0)
         ecx = dword [esi + 0x14]
         ecx = dword [ecx + 0x1a0]
         fcn.10170480 ()          // method.DisplayItem.virtual_496 // fcn.10170480(0x177f94)
         edi = dword [arg_8h]
         movss xmm0 dword [eax]
         ecx = dword [edi + 0x1a0]
         movss dword [var_64h] xmm0
         fcn.10170480 ()          // method.DisplayItem.virtual_496 // fcn.10170480(0x177f94)
         movss xmm0 dword [eax]
         ecx = edi
         movss dword [var_60h] xmm0
         fcn.10159770 ()          // fcn.10159770(0x177f94)
         ecx = eax
         fcn.10164850 ()          // fcn.10164850(0x177f94)
         ecx = dword [arg_8h]
         dword [var_5ch] = eax
         edi = 0
         fcn.10159b50 ()          // method.cocos2d::CCParticleSystemQuad.virtual_628 // fcn.10159b50(0x177f94)
         ecx = dword [eax + 4]
         ecx -= dword [eax]
         ecx >>= 2
         dword [var_2ch] = ecx
         v = ecx & ecx
         if (v <= 0) 
         goto loc_0x1016221c;
    loc_0x1016221c: // orphan
         esi = 0
         ebx = ecx

    loc_0x10162220: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162245(x)
         ecx = dword [arg_8h]
         fcn.10159b50 ()          // method.cocos2d::CCParticleSystemQuad.virtual_628 // fcn.10159b50(0x0)
         eax = dword [eax]
         ecx = dword [eax + esi*4]
         v = ecx & ecx
         if (!v) 
         goto loc_0x10162231;
    loc_0x10162231: // orphan
         fcn.10159770 ()          // fcn.10159770(0x0)
         ecx = eax
         fcn.10164980 ()          // fcn.10164980(0x0)
         v = eax - edi
         cmovg edi eax

    loc_0x10162242: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016222f(x)
         esi++
         v = esi - ebx
         jl 0x10162220            // unlikely

         goto loc_0x10162247;
    loc_0x10162247: // orphan
         ebx = dword [var_34h]
         esi = dword [var_14h]

    loc_0x1016224d: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016221a(x)
         ecx = dword [esi + 0x6c] // int32_t arg3
         eax = var_6ch            // int32_t arg1
         push (eax)
         push (dword [esi + 0x58])
         dword [var_4ch] = edi
         fcn.10243750 ()          // fcn.10243750(0x177f94, 0x0, -1, 0x0, 0x0)
         fstp dword [ebp - 0x34]
         movss xmm1 dword [var_34h]
         movss xmm2 dword [0x1037439c] // [0x1037439c:4]=0x3f800000
         comiss xmm2 xmm1
         if (((unsigned) v) <= 0) 
         goto loc_0x10162274;
    loc_0x10162274: // orphan
         xmm1 = xmm2

    loc_0x10162277: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162272(x)
         xmm0 = xmm2
         divss xmm0 xmm1
         ecx = 0x1066067c
         subss xmm2 xmm0
         movss dword [var_2ch] xmm2
         fcn.10182be0 ()          // fcn.10182be0(0x0, 0x0)
         fstp dword [ebp - 0x34]
         movss xmm0 dword [var_34h]
         comiss xmm0 dword [var_2ch]
         if (((unsigned) v) <= 0) 
         goto loc_0x1016229f;
    loc_0x1016229f: // orphan
         byte [esi + 0x50] = 1

    loc_0x101622a3: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016229d(x)
         v = byte [esi + 0x50] - 0
         if (v) 
         goto loc_0x101622a9;
    loc_0x101622a9: // orphan
         v = byte [esi + 0xb4] - 0
         if (!v) 
         goto loc_0x101622b6;
    loc_0x101622b6: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101622a7(x)
         eax = 0
         v = byte [esi + 0x82] - al
         if (!v) 
         goto loc_0x101622c0;
    loc_0x101622c0: // orphan
         push (dword [arg_8h])
         ecx = esi                // int32_t arg_8h
         fcn.1015dc70 ()          // fcn.1015dc70(0x0, 0x0, 0x0, 0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x101622ce;
    loc_0x101622ce: // orphan
         dword [esi + 0x68] = 0

    loc_0x101622d5: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101622cc(x)
         v = byte [esi + 0x80] - 0
         if (!v) 
         goto loc_0x101622de;
    loc_0x101622de: // orphan
         v = byte [esi + 0xb4] - 0
         if (!v) 
         goto loc_0x101622eb;
    loc_0x101622eb: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101622dc(x)
         v = eax & eax
         if (v) 
         goto loc_0x101622f3;
    loc_0x101622f3: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101622be(x)
         v = byte [esi + 0xb4] - 0
         if (!v) 
         goto loc_0x10162300;
    loc_0x10162300: // orphan
         fcn.10052d60 ()
         ecx = dword [ebx + 4]
         edx = dword [ebx]
         edi = ecx
         edi -= ecx
         edi >>= 3
         dword [arg_8h] = eax
         v = edi & edi
         if (v <= 0) 
         goto loc_0x10162318;
    loc_0x10162318: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016232b(x)
         eax = dword [ecx]
         dword [edx] = eax
         eax = dword [ecx + 4]
         dword [edx + 4] = eax
         edi--
         edx += 8
         ecx = ecx + 8
         v = edi & edi
         if (v > 0) 
         goto loc_0x1016232d;
    loc_0x1016232d: // orphan
         eax = dword [arg_8h]

    loc_0x10162330: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162316(x)
         dword [ebx + 4] = edx
         ecx = dword [eax + 4]
         edx = dword [eax]
         eax = ecx
         eax -= edx
         eax >>= 2
         dword [var_24h] = ecx
         dword [var_28h] = edx
         v = eax & eax
         if (!v) 
         goto loc_0x1016234d;
    loc_0x1016234d: // orphan
         ecx = dword [edx]
         v = edx - dword [var_24h]
         jae 0x101623c8           // likely

         goto loc_0x10162354;
    loc_0x10162354: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101623c3(x)
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         ecx = dword [eax + 4]
         esi = dword [eax]
         eax = ecx
         eax -= esi
         eax >>= 2
         dword [var_34h] = ecx
         v = eax & eax
         if (!v) 
         goto loc_0x1016236c;
    loc_0x1016236c: // orphan
         edi = dword [esi]
         v = esi - ecx
         jae 0x101623b5           // likely

         goto loc_0x10162372;
    loc_0x10162372: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101623b3(x)
         ecx = edi
         dword [var_30h] = edi
         fcn.10103670 ()          // fcn.10103670(0x0)
         ecx = dword [ebx + 4]
         dword [var_2ch] = eax
         v = ecx - dword [ebx + 8]
         if (!v) 
         goto loc_0x10162387;
    loc_0x10162387: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x1016238b;
    loc_0x1016238b: // orphan
         dword [ecx] = edi
         dword [ecx + 4] = eax

    loc_0x10162390: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162389(x)
         dword [ebx + 4] += 8
         
         goto loc_0x10162396;
    loc_0x10162396: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162385(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = arg_bh
         push (eax)
         eax = var_30h
         push (eax)
         push (ecx)
         ecx = ebx
         fcn.10200f40 ()          // fcn.10200f40(0x177fd0, 0x0)

    loc_0x101623aa: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162394(x)
         edi = dword [esi + 4]
         esi += 4
         v = esi - dword [var_34h]
         if (((unsigned) v) < 0) 
         goto loc_0x101623b5;
    loc_0x101623b5: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x1016236a(x), 0x10162370(x)
         eax = dword [var_28h]
         eax += 4
         dword [var_28h] = eax
         ecx = dword [eax]
         v = eax - dword [var_24h]
         if (((unsigned) v) < 0) 
         goto loc_0x101623c5;
    loc_0x101623c5: // orphan
         esi = dword [var_14h]

    loc_0x101623c8: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x10162347(x), 0x10162352(x)
         eax = dword [ebx + 4]
         eax -= dword [ebx]
         eax >>= 3
         v = eax & eax
         if (!v) 
         goto loc_0x101623d8;
    loc_0x101623d8: // orphan
         dword [esi + 0x68] = 9
         
         goto loc_0x101623e4;
    loc_0x101623e4: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101622fa(x)
         v = byte [esi + 0x9c] - 0
         if (!v) 
         goto loc_0x101623f1;
    loc_0x101623f1: // orphan
         fcn.10052d40 ()
         ecx = dword [ebx + 4]
         edx = dword [ebx]
         edi = ecx
         edi -= ecx
         edi >>= 3
         dword [arg_8h] = eax
         v = edi & edi
         if (v <= 0) 
         goto loc_0x10162409;
    loc_0x10162409: // orphan
         esp = esp                // ebp

    loc_0x10162410: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162423(x)
         eax = dword [ecx]
         dword [edx] = eax
         eax = dword [ecx + 4]
         dword [edx + 4] = eax
         edi--
         edx += 8
         ecx = ecx + 8
         v = edi & edi
         if (v > 0) 
         goto loc_0x10162425;
    loc_0x10162425: // orphan
         eax = dword [arg_8h]

    loc_0x10162428: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162407(x)
         dword [ebx + 4] = edx
         ecx = dword [eax + 4]
         edx = dword [eax]
         eax = ecx
         eax -= edx
         eax >>= 2
         dword [var_24h] = ecx
         dword [var_28h] = edx
         v = eax & eax
         if (!v) 
         goto loc_0x10162441;
    loc_0x10162441: // orphan
         ecx = dword [edx]
         v = edx - dword [var_24h]
         jae 0x101624bc           // likely

         goto loc_0x10162448;
    loc_0x10162448: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101624b7(x)
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         ecx = dword [eax + 4]
         esi = dword [eax]
         eax = ecx
         eax -= esi
         eax >>= 2
         dword [var_34h] = ecx
         v = eax & eax
         if (!v) 
         goto loc_0x10162460;
    loc_0x10162460: // orphan
         edi = dword [esi]
         v = esi - ecx
         jae 0x101624a9           // likely

         goto loc_0x10162466;
    loc_0x10162466: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x101624a7(x)
         ecx = edi
         dword [var_30h] = edi
         fcn.10103670 ()          // fcn.10103670(0x0)
         ecx = dword [ebx + 4]
         dword [var_2ch] = eax
         v = ecx - dword [ebx + 8]
         if (!v) 
         goto loc_0x1016247b;
    loc_0x1016247b: // orphan
         v = ecx & ecx
         if (!v) 
         goto loc_0x1016247f;
    loc_0x1016247f: // orphan
         dword [ecx] = edi
         dword [ecx + 4] = eax

    loc_0x10162484: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x1016247d(x)
         dword [ebx + 4] += 8
         
         goto loc_0x1016248a;
    loc_0x1016248a: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162479(x)
         push (1)                 // 1
         push (1)                 // 1
         eax = arg_bh
         push (eax)
         eax = var_30h
         push (eax)
         push (ecx)
         ecx = ebx
         fcn.10200f40 ()          // fcn.10200f40(0x177fd0, 0x0)

    loc_0x1016249e: // orphan
         // CODE XREF from fcn.10161ad0 @ 0x10162488(x)
         edi = dword [esi + 4]
         esi += 4
         v = esi - dword [var_34h]
         if (((unsigned) v) < 0) 
         goto loc_0x101624a9;
    loc_0x101624a9: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x1016245e(x), 0x10162464(x)
         eax = dword [var_28h]
         eax += 4
         dword [var_28h] = eax
         ecx = dword [eax]
         v = eax - dword [var_24h]
         if (((unsigned) v) < 0) 
         goto loc_0x101624b9;
    loc_0x101624b9: // orphan
         esi = dword [var_14h]

    loc_0x101624bc: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x1016243f(x), 0x10162446(x)
         eax = dword [ebx + 4]
         eax -= dword [ebx]
         eax >>= 3
         v = eax & eax
         if (!v) 
         goto loc_0x101624c8;
    loc_0x101624c8: // orphan
         dword [esi + 0x68] = 5

    loc_0x101624cf: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x101623df(x), 0x101623eb(x)
         v = eax & eax
         if (v) 
         goto loc_0x101624d7;
    loc_0x101624d7: // orphan
         // CODE XREFS from fcn.10161ad0 @ 0x101623d2(x), 0x101624c6(x)
         ecx = dword [esi + 0x14]
         push (0)
         fcn.1015a370 ()          // fcn.1015a370(0x0, 0x0)
         v = byte [esi + 0xb4] - 0
         dword [esi + 0x68] = 0xfffffffe // [0xfffffffe:4]=-1 // 4294967294
         byte [esi + 0x51] = 1
         dword [esi + 0x48] = 0x88ca6c00 // [0x88ca6c00:4]=-1
         dword [esi] = 3
         if (!v) 
         return eax;
    loc_0x10162506: // orphan
         ecx = esi
         fcn.101612a0 ()          // fcn.101612a0(0x0)
         eax = 0
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()             // ebp
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

