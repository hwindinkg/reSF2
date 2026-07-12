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
void fcn.10161350 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch) {
        // CALL XREF from fcn.1015a410 @ 0x1015a48b(x)
        push (ebp)
        ebp = esp
        esp -= 0x30
        push (ebx)    // arg2
        ebx = arg_ch  // arg3
        eax = dword [ebx + 0x2c]
        v = byte [eax + 0x62] - 0
        if (v) goto loc_0x10161374 // likely
        goto loc_0x10161362;
    loc_0x10161374:
        // CODE XREFS from fcn.10161350 @ 0x10161360(x), 0x10161369(x)
        ecx = dword [arg_8h]
        push (esi)
        push (edi)
        fcn.10159770 () // fcn.10159770(0x0)
        ecx = dword [ebx + 0x14]
        dword [var_4h] = eax
        fcn.101596d0 () // fcn.101596d0(0x0)
        ecx = dword [ebx + 0x14]
        edi = eax
        fcn.10159150 () // fcn.10159150(0x0)
        ecx = dword [ebx + 0x14]
        esi = eax
        fcn.10159820 () // fcn.10159820(0x0)
        ecx = dword [ebx + 0x14]
        dword [var_ch] = eax
        eax = var_14h
        push (eax)
        eax = var_2ch
        push (eax)
        eax = var_30h // int32_t arg1
        push (eax)
        push (dword [ebx + 0x54])
        dword [var_30h] = 0
        push (1)      // 1
        dword [var_2ch] = 0
        dword [var_28h] = 0
        dword [var_24h] = 0
        dword [var_20h] = 0
        dword [var_1ch] = esi
        dword [var_18h] = edi
        dword [var_14h] = 0
        dword [var_10h] = 0
        fcn.10159820 () // fcn.10159820(0x177fd0)
        ecx = eax     // int32_t arg_8h
        fcn.10175530 () // fcn.10175530(0x177fd0, 0x0, 0x177fd0, 0x0, 0x0, 0x0, 0x0)
        ecx = dword [ebx + 0x14]
        ecx = dword [ecx + 0x1a0]
        fcn.10170480 () // method.DisplayItem.virtual_496 // fcn.10170480(0x177fd0)
        esi = dword [arg_8h]
        movss xmm0 dword [eax]
        ecx = dword [esi + 0x1a0]
        movss dword [var_28h] xmm0
        fcn.10170480 () // method.DisplayItem.virtual_496 // fcn.10170480(0x177fd0)
        movss xmm0 dword [eax]
        ecx = esi
        movss dword [var_24h] xmm0
        fcn.10159770 () // fcn.10159770(0x177fd0)
        ecx = eax
        fcn.10164850 () // fcn.10164850(0x177fd0)
        push (esi)
        dword [var_20h] = eax
        fcn.1015d600 () // fcn.1015d600(0x177fd0, 0x0, 0x177fd0)
        edi = dword [var_4h]
        esp += 4
        ecx = edi
        dword [var_10h] = eax
        fcn.10164c10 () // method.CCLabelBMFontExtended.3.virtual_12 // fcn.10164c10(0x177fd0)
        v = al & al
        if (!v) goto loc_0x1016147b // unlikely
        goto loc_0x10161450;
    loc_0x1016147b:
        // CODE XREF from fcn.10161350 @ 0x1016144e(x)
        push (str.enemy_no_play_animation) // 0x105b1ae4 // "enemy no play animation" // (pstr 0x105b1ae4) "enemy no play animation"
        dword [ebx + 0xc] = 0xffffffff // [0xffffffff:4]=-1 // -1
        fcn.101472f0 () // fcn.101472f0(0x0, 0x0)
        esp += 4      // (pstr 0x105b1ae4) "enemy no play animation"
        
    loc_0x1016148f:
        // CODE XREF from fcn.10161350 @ 0x10161479(x)
        ecx = dword [ebx + 0x24]
        fcn.10164c10 () // method.CCLabelBMFontExtended.3.virtual_12 // fcn.10164c10(0x0)
        v = al & al
        if (!v) goto loc_0x101614b4 // likely
        goto loc_0x1016149b;
        return eax;
        return eax;
    loc_0x1016136b:
        eax = 0
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
    loc_0x10161450: // orphan
         ecx = edi
         fcn.10164850 ()          // fcn.10164850(0x0)
         ecx = dword [var_4h]
         edi = eax
         fcn.10164860 ()          // fcn.10164860(0x0)
         ecx = dword [ebx + 0x6c]
         esi = eax
         eax = var_30h            // int32_t arg1
         push (eax)
         fcn.102437f0 ()          // fcn.102437f0(0x177fd0, 0x0)
         eax += esi
         eax += edi
         edi = dword [var_4h]
         dword [ebx + 0xc] = eax
         
         goto loc_0x1016147b;
    loc_0x1016149b: // orphan
         ecx = dword [ebx + 0x24]
         fcn.10164850 ()          // fcn.10164850(0x0)
         ecx = dword [ebx + 0x24]
         esi = eax
         fcn.10164860 ()          // fcn.10164860(0x0)
         eax += esi
         dword [ebx + 0x10] = eax
         
         goto loc_0x101614b4;
    loc_0x101614b4: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161499(x)
         push (str.bot_no_play_animation) // 0x105b1afc // "bot no play animation" // (pstr 0x105b1afc) "bot no play animation"
         dword [ebx + 0x10] = 0xffffffff // [0xffffffff:4]=-1 // -1
         fcn.101472f0 ()          // fcn.101472f0(0x0, 0x0)
         esp += 4                 // (pstr 0x105b1afc) "bot no play animation"

    loc_0x101614c8: // orphan
         // CODE XREF from fcn.10161350 @ 0x101614b2(x)
         v = byte [ebx + 0x51] - 0
         if (!v) 
         goto loc_0x101614d2;
    loc_0x101614d2: // orphan
         ecx = dword [ebx + 0x24]
         byte [ebx + 0x51] = 0
         fcn.10164c10 ()          // method.CCLabelBMFontExtended.3.virtual_12 // fcn.10164c10(0x0)
         v = al & al
         if (!v) 
         goto loc_0x101614e6;
    loc_0x101614e6: // orphan
         ecx = edi
         fcn.10164c10 ()          // method.CCLabelBMFontExtended.3.virtual_12 // fcn.10164c10(0x0)
         v = al & al
         if (!v) 
         goto loc_0x101614f5;
    loc_0x101614f5: // orphan
         ecx = dword [ebx + 0x24]
         fcn.1002c4c0 ()          // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
         ecx = edi
         esi = eax
         fcn.1002c4c0 ()          // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
         dword [var_8h] = eax
         v = esi & esi
         if (!v) 
         goto loc_0x10161511;
    loc_0x10161511: // orphan
         v = eax & eax
         if (!v) 
         goto loc_0x10161519;
    loc_0x10161519: // orphan
         ecx = esi
         fcn.10103670 ()          // fcn.10103670(0x0)
         ecx = dword [ebx]
         edi = eax
         ecx--
         if (!v) 
         goto loc_0x10161527;
    loc_0x10161527: // orphan
         ecx--
         if (!v) 
         goto loc_0x1016152a;
    loc_0x1016152a: // orphan
         ecx--
         if (v) 
         goto loc_0x10161531;
    loc_0x10161531: // orphan
         eax = edi - 1
         dword [ebx + 0x48] = eax
         
         goto loc_0x10161539;
    loc_0x10161539: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161528(x)
         ecx = dword [var_8h]     // uint32_t arg_8h
         push (1)                 // 1
         fcn.10103510 ()          // fcn.10103510(0x0, 0x0, 0x0)
         ecx = 1
         ecx -= dword [ebx + 0xc]
         eax += ecx               // int32_t arg1
         ecx = esi
         dword [ebx + 0x48] = eax
         dword [var_8h] = eax
         fcn.10103650 ()          // fcn.10103650(0xffe88031)
         ecx = dword [var_8h]
         
         goto loc_0x1016155f;
    loc_0x1016155f: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161525(x)
         ecx = dword [var_8h]
         push (1)                 // 1
         fcn.101034b0 ()          // fcn.101034b0(0x0, 0x0)
         ecx = 1
         ecx -= dword [ebx + 0xc]
         eax += ecx               // int32_t arg1
         ecx = esi
         dword [ebx + 0x48] = eax
         dword [var_8h] = eax
         fcn.10103650 ()          // fcn.10103650(0xffe88031)
         ecx = dword [var_8h]

    loc_0x10161583: // orphan
         // CODE XREF from fcn.10161350 @ 0x1016155d(x)
         v = ecx - eax
         if (v <= 0) 
         goto loc_0x10161587;
    loc_0x10161587: // orphan
         ecx = esi
         fcn.10103650 ()          // fcn.10103650(0x0)
         ecx = eax

    loc_0x10161590: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161585(x)
         v = edi - ecx
         eax = ecx
         cmovg eax edi
         dword [ebx + 0x48] = eax
         dword [ebx + 0x48]--
         
         goto loc_0x1016159f;
    loc_0x1016159f: // orphan
         // CODE XREF from fcn.10161350 @ 0x101614cc(x)
         eax = dword [ebx]
         eax--
         v = eax - 2              // 2
         if (((unsigned) v) > 0) 
         goto loc_0x101615a7;
    loc_0x101615a7: // orphan
         push (0x105b1b14)        // '\x14' // "!" // (pstr 0x105b1b14) "!"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         esp += 4                 // (pstr 0x105b1b14) "!"

    loc_0x101615b4: // orphan
         // XREFS: CODE 0x101614e0  CODE 0x101614ef  CODE 0x1016150b   // XREFS: CODE 0x10161513  CODE 0x1016152b  CODE 0x10161537   // XREFS: CODE 0x1016159d  CODE 0x101615a5  
         eax = dword [ebx + 0x48]
         dword [ebx] = 0
         v = eax - 1              // 1
         if (v <= 0) 
         return eax;
    loc_0x101615c2: // orphan
         edi = pop ()
         eax--
         dword [ebx + 0x48] = eax
         esi = pop ()
         eax = 0
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x101615d1: // orphan
         // CODE XREF from fcn.10161350 @ 0x101615c0(x)
         ecx = dword [var_4h]     // "!"
         fcn.1002c4c0 ()          // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
         edi = eax
         v = edi & edi
         if (!v) 
         goto loc_0x101615df;
    loc_0x101615df: // orphan
         fcn.10052f80 ()
         edx = dword [eax + 4]
         esi = dword [eax]
         ecx = edx
         ecx -= esi
         ecx >>= 2
         dword [var_8h] = edx
         v = ecx & ecx
         if (!v) 
         goto loc_0x101615f7;
    loc_0x101615f7: // orphan
         ecx = dword [esi]
         v = esi - edx
         jae 0x1016163a           // likely

         goto loc_0x101615fd;
    loc_0x101615fd: // orphan
         ecx = ecx

    loc_0x10161600: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161638(x)
         fcn.1016c5d0 ()          // fcn.1016c5d0(0x0)
         ecx = dword [eax]
         eax = dword [eax + 4]
         eax -= ecx
         eax >>= 2
         v = eax & eax
         if (!v) 
         goto loc_0x10161613;
    loc_0x10161613: // orphan
         eax = ecx + eax*4
         v = ecx - eax
         jae 0x1016162f           // likely

         goto loc_0x1016161a;
    loc_0x1016161a: // orphan
         ebx = ebx

    loc_0x10161620: // orphan
         // CODE XREF from fcn.10161350 @ 0x1016162d(x)
         v = edi - dword [ecx]
         if (!v) 
         goto loc_0x10161628;
    loc_0x10161628: // orphan
         ecx += 4
         v = ecx - eax
         if (((unsigned) v) < 0) 
         goto loc_0x1016162f;
    loc_0x1016162f: // orphan
         // CODE XREFS from fcn.10161350 @ 0x10161611(x), 0x10161618(x)
         ecx = dword [esi + 4]
         esi += 4
         v = esi - dword [var_8h]
         if (((unsigned) v) < 0) 
         goto loc_0x1016163a;
    loc_0x1016163a: // orphan
         // CODE XREFS from fcn.10161350 @ 0x101615dd(x), 0x101615f5(x), 0x101615fb(x)
         esi = dword [arg_8h]

    loc_0x1016163d: // orphan
         // CODE XREF from fcn.10161350 @ 0x101617ac(x)
         ecx = ebx
         fcn.101626d0 ()          // fcn.101626d0(0x0, 0x0)
         ecx = ebx
         fcn.101625c0 ()          // fcn.101625c0(0x0)
         ecx = ebx
         fcn.10160100 ()          // fcn.10160100(0x0, 0x0)
         v = al & al
         if (!v) 
         goto loc_0x1016165a;
    loc_0x1016165a: // orphan
         push (esi)
         ecx = ebx                // int32_t arg_8h
         fcn.10161950 ()          // fcn.10161950(0x0, 0x0, 0x0)
         ecx = dword [ebx + 0x6c]
         dword [ebx + 0x70] = eax
         eax = var_30h            // int32_t arg1
         push (eax)
         fcn.10243950 ()          // fcn.10243950(0x177fd0)
         fstp dword [ebp - 8]
         movss xmm0 dword [var_8h]
         comiss xmm0 dword [ebx + 0xa4]
         ecx = dword [ebx + 0x6c]
         seta al
         byte [ebx + 0x81] = al
         eax = var_30h            // int32_t arg1
         push (eax)
         movss dword [ebx + 0xdc] xmm0
         fcn.102438b0 ()          // fcn.102438b0(0x177fd0)
         ecx = ebx                // int32_t arg_8h
         fstp dword [ebp - 8]
         movss xmm0 dword [var_8h]
         comiss xmm0 dword [ebx + 0xa8]
         movss dword [ebx + 0xe0] xmm0
         seta al
         byte [ebx + 0x82] = al
         eax = var_30h            // int32_t arg1
         push (eax)
         fcn.10162640 ()          // fcn.10162640(0x177fd0, 0x0, 0x0)
         eax = var_30h            // int32_t arg1
         push (eax)
         ecx = ebx
         fcn.10162530 ()          // fcn.10162530(0x177fd0, 0x0)
         ecx = dword [ebx + 0x6c]
         eax = var_30h            // int32_t arg1
         push (eax)
         fcn.10243640 ()          // fcn.10243640(0x177fd0)
         fstp dword [ebp - 8]
         movss xmm0 dword [var_8h]
         comiss xmm0 dword [ebx + 0xac]
         ecx = dword [ebx + 0x6c]
         seta al
         byte [ebx + 0x9c] = al
         eax = var_30h            // int32_t arg1
         push (eax)
         movss dword [ebx + 0xe4] xmm0
         fcn.102436d0 ()          // fcn.102436d0(0x177fd0)
         edi = dword [arg_ch]
         fstp dword [ebp - 8]
         movss xmm0 dword [var_8h]
         comiss xmm0 dword [ebx + 0xb0]
         push (edi)
         seta al
         push (esi)
         ecx = ebx                // int32_t arg_8h
         movss dword [ebx + 0x78] xmm0
         byte [ebx + 0x74] = al
         fcn.10161ad0 ()          // fcn.10161ad0(0x177f01, 0x0, 0x0, 0x0)
         dword [var_8h] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x10161738;
    loc_0x10161738: // orphan
         ecx = ebx + 0xb8
         push (ecx)
         ecx = ebx                // uint32_t arg_8h
         byte [ebx + 0x50] = 0
         fcn.1015f980 ()          // fcn.1015f980(0x0, 0x0, 0x0)
         ecx = ebx
         fcn.1015d7e0 ()          // fcn.1015d7e0(0x0, 0x0)
         eax = ebx + 0xc4         // int32_t arg1
         push (eax)
         ecx = ebx
         fcn.10161840 ()          // fcn.10161840(0xc4, 0x0)
         edi = eax
         v = edi - 0xffffffff
         if (v <= 0) 
         return eax;
    loc_0x10161766: // orphan
         edx = dword [ebx + 0x18]
         ecx = dword [arg_8h]
         esi = dword [edx + edi*4]
         edx = dword [ebx + 0xc4]
         push (esi)
         dword [ebx + 0x48] = esi
         push (dword [edx + edi*4])
         push (dword [var_8h])
         push (dword [arg_ch])
         fcn.10159770 ()          // fcn.10159770(0x0)
         push (eax)
         ecx = ebx                // uint32_t arg_ch
         fcn.10160560 ()          // fcn.10160560(0x0, 0x0, 0x0, -1, -1, 0x0)
         eax = dword [ebx + 0xc4]
         eax = dword [eax + edi*4]
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

    loc_0x101617a1: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161622(x)
         esi = dword [arg_8h]
         push (esi)
         ecx = ebx                // int32_t arg_8h
         fcn.10161150 ()          // fcn.10161150(0x0, 0x0, 0x0)
         
         goto loc_0x101617b1;
    loc_0x101617b1: // orphan
         // CODE XREF from fcn.10161350 @ 0x10161736(x)
         v = byte [ebx + 0x51] - 0
         if (!v) 
         goto loc_0x101617b7;
    loc_0x101617b7: // orphan
         push (0)
         push (0)
         push (1)                 // 1
         eax = edi - 1
         push (eax)
         ecx = esi
         fcn.10159770 ()          // fcn.10159770(-1)
         push (eax)
         ecx = ebx                // uint32_t arg_ch
         fcn.10160560 ()          // fcn.10160560(-1, 0x0, 0x0, 0x0, 0x0, 0x0)
         byte [ebx + 0x50] = 0

    loc_0x101617d4: // orphan
         // CODE XREFS from fcn.10161350 @ 0x10161654(x), 0x10161764(x), 0x101617b5(x)
         edi = pop ()
         esi = pop ()
         eax = 0
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

