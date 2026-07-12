// DZ decoder decompilation via radare2 pdc
// Source: libs3e_android.so

========================================================
// Function at 0x0x37a5c
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.0003751c (int32_t arg1, int32_t arg2, int32_t arg3) {
        // CALL XREFS from fcn.000389f8 @ 0x38d28(x), 0x38ff8(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r6 = ~0
        r3 = [r0 + 8] // arg1
        r2 = r1 + r2  // arg3
        r5 = [r0 + 0x2c] // arg1
        r8 = [r0 + 0x34] // arg1
        bic r6, r5, r6, lsl r3
        lr = [r0 + 0x1c] // arg1 // elf_phdr
        r4 = [r0 + 0x10] // arg1
        add r3, r6, r8, lsl 4
        (a, b) = compare (lr, 0x1000000)
        ip = [r0 + 0x20] // arg1 // elf_shdr
        r3 = r3 << 1
        r3 = (word) [r4 + r3]
        bhs 0x37570   // unlikely
        goto loc_0x00037558;
    loc_0x00037570:
        // CODE XREF from fcn.0003751c @ 0x37554(x)
        r7 = lr >> 0xb
        r3 *= r7
        (a, b) = compare (ip, r3)
        bhs 0x37650   // likely
        goto loc_0x00037580;
    loc_0x00037650:
        // CODE XREF from fcn.0003751c @ 0x3757c(x)
        r0 = r8 + 0xc0
        rsb r7, r3, lr
        rsb ip, r3, ip
        (a, b) = compare (r7, 0x1000000)
        r3 = r0 << 1
        r0 = (word) [r4 + r3]
        bhs 0x37684   // unlikely
        goto loc_0x0003766c;
    loc_0x00037684:
        // CODE XREF from fcn.0003751c @ 0x37668(x)
        lr = r7 >> 0xb
        r5 = r0 * lr
        (a, b) = compare (ip, r5)
        blo 0x37758   // unlikely
        goto loc_0x00037694;
    loc_0x00037758:
        // CODE XREF from fcn.0003751c @ 0x37690(x)
        r7 = r4 + 0x660
        r0 = 2
        r7 += 4
        sl = 0
        
    loc_0x00037768:
        // CODE XREF from fcn.0003751c @ 0x37a78(x)
        (a, b) = compare (r5, 0x1000000)
        r3 = (word) [r7] // "ecompFinal"
        bhs 0x3778c   // unlikely
        goto loc_0x00037774;
        goto loc_0x000376ac;
        goto loc_0x00037674;
        goto loc_0x00037594;
        goto loc_0x00037560;
        return r0;
    loc_0x00037560:
        r7 = (byte) [r1] // arg2
        lr = lr << 8
        r1 += 1       // arg2
        ip = r7 | ip
        break;
    loc_0x00037580: // orphan
         lr = [r0 + 0x30]         // arg1
         r4 += 0xe60
         r4 += 0xc
         orrs lr, r5, lr
         je 0x375d0               // unlikely

         goto loc_0x00037594;
    loc_0x00037594: // orphan
         lr = [r0 + 0x24]         // arg1
         r7 = ~0
         r6 = [r0 + 4]            // arg1
         (a, b) = compare (lr, 0)
         bic r5, r5, r7, lsl r6
         r7 = [r0 + 0x14]         // arg1
         ldreq lr, [r0, 0x28]     // arg1
         r6 = [r0]                // arg1
         lr -= 1
         r5 = r5 << r6
         rsb r6, r6, 8
         lr = (byte) [r7 + lr]
         add r5, r5, lr, asr r6
         add r5, r5, r5, lsl 1
         add r4, r4, r5, lsl 9

    loc_0x000375d0: // orphan
         // CODE XREF from fcn.0003751c @ 0x37590(x)
         (a, b) = compare (r8, 6)
         bhi 0x376bc              // unlikely

         goto loc_0x000375d8;
    loc_0x000375d8: // orphan
         lr = 1

    loc_0x000375dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x37628(x)
         r5 = lr << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + 1
         r0 = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x37608              // unlikely

         goto loc_0x000375f0;
    loc_0x000375f0: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000375fc;
    loc_0x000375fc: // orphan
         r6 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r6 | ip

    loc_0x00037608: // orphan
         // CODE XREF from fcn.0003751c @ 0x375ec(x)
         r6 = r3 >> 0xb
         r0 *= r6
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         rsbhs ip, r0, ip
         movlo lr, r5
         movlo r3, r0
         (a, b) = compare (lr, 0xff)
         bls 0x375dc              // likely

         goto loc_0x0003762c;
    loc_0x0003762c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37754(x)
         r0 = 1

    loc_0x00037630: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37804(x), 0x37880(x), 0x378e0(x)
         (a, b) = compare (r3, 0x1000000)
         movlo r3, 1
         movhs r3, 0
         (a, b) = compare (r1, r2) // arg3
         movlo r3, 0
         (a, b) = compare (r3, 0)
         movne r0, 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x0003766c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037674;
    loc_0x00037674: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037694: // orphan
         rsb r7, r5, r7
         r3 = r4 + r3
         (a, b) = compare (r7, 0x1000000)
         rsb r0, r5, ip
         ip = (word) [r3 + 0x18]  // section..text
         bhs 0x37918              // unlikely

         goto loc_0x000376ac;
    loc_0x000376ac: // orphan
         (a, b) = compare (r1, r2) // arg3
         blo 0x37908              // unlikely

         return r0;
    loc_0x000376b4: // orphan
         // XREFS: CODE 0x0003755c  CODE 0x000375f8  CODE 0x00037670   // XREFS: CODE 0x00037714  CODE 0x00037778  CODE 0x000377cc   // XREFS: CODE 0x00037844  CODE 0x00037904  CODE 0x00037944   // XREFS: CODE 0x000379a4  CODE 0x00037a04  CODE 0x00037a40   // XREFS: CODE 0x00037a9c  
         r0 = 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc) // 0x178000 // r13

    loc_0x000376bc: // orphan
         // CODE XREF from fcn.0003751c @ 0x375d4(x)
         lr = [r0 + 0x24]         // arg1
         r6 = [r0 + 0x38]         // arg1 // fcn.000bbf24
         r5 = [r0 + 0x14]         // arg1
         (a, b) = compare (lr, r6)
         rsb r6, r6, lr
         lr = 1
         ldrlo r0, [r0, 0x28]     // arg1
         movhs r0, 0
         r0 = r5 + r0             // arg1
         r5 = 0x100
         r6 = (byte) [r0 + r6]    // arg1

    loc_0x000376e8: // orphan
         // CODE XREF from fcn.0003751c @ 0x37750(x)
         r6 = r6 << 1
         r0 = r5 + lr
         r7 = r5 & r6
         (a, b) = compare (r3, 0x1000000)
         r0 += r7
         r8 = lr << 1
         r0 = r0 << 1
         r0 = (word) [r4 + r0]    // "ecompFinal"
         bhs 0x37724              // unlikely

         goto loc_0x0003770c;
    loc_0x0003770c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037718;
    loc_0x00037718: // orphan
         sb = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = sb | ip

    loc_0x00037724: // orphan
         // CODE XREF from fcn.0003751c @ 0x37708(x)
         sb = r3 >> 0xb
         r0 *= sb
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         lsllo lr, lr, 1
         addhs lr, r8, 1
         biclo r5, r5, r7
         movlo r3, r0
         rsbhs ip, r0, ip
         andhs r5, r5, r7
         (a, b) = compare (lr, 0xff)
         bls 0x376e8              // likely

         goto loc_0x00037754;
    loc_0x00037754: // orphan
         
         goto loc_0x00037758;
    loc_0x00037774: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x0003777c;
    loc_0x0003777c: // orphan
         lr = (byte) [r1]         // arg2
         r5 = r5 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x0003778c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37770(x)
         lr = r5 >> 0xb
         r3 *= lr
         (a, b) = compare (ip, r3)
         bhs 0x3798c              // likely

         goto loc_0x0003779c;
    loc_0x0003779c: // orphan
         add r7, r7, r6, lsl 4
         sb = 0
         r7 += 4
         r8 = 8

    loc_0x000377ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x379e8(x)
         r5 = 1

    loc_0x000377b0: // orphan
         // CODE XREF from fcn.0003751c @ 0x377fc(x)
         r6 = r5 << 1
         (a, b) = compare (r3, 0x1000000)
         r5 = r6 + 1
         lr = (word) [r7 + r6]    // "ecompFinal"
         bhs 0x377dc              // unlikely

         goto loc_0x000377c4;
    loc_0x000377c4: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000377d0;
    loc_0x000377d0: // orphan
         fp = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = fp | ip

    loc_0x000377dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x377c0(x)
         fp = r3 >> 0xb
         lr *= fp
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r5, r6
         movlo r3, lr
         (a, b) = compare (r5, r8)
         blo 0x377b0              // unlikely

         goto loc_0x00037800;
    loc_0x00037800: // orphan
         (a, b) = compare (sl, 3)
         bhi 0x37630              // unlikely

         goto loc_0x00037808;
    loc_0x00037808: // orphan
         rsb r8, r8, sb
         r6 = 1
         r5 = r8 + r5
         (a, b) = compare (r5, 3)
         lslls r5, r5, 7
         addls r8, r5, 0x360
         movhi r8, 0x4e0

    loc_0x00037824: // orphan
         // CODE XREF from fcn.0003751c @ 0x37874(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + r8
         r6 = r5 + 1
         lr = (word) [r4 + lr]    // "ecompFinal"
         bhs 0x37854              // unlikely

         goto loc_0x0003783c;
    loc_0x0003783c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037848;
    loc_0x00037848: // orphan
         r7 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r7 | ip

    loc_0x00037854: // orphan
         // CODE XREF from fcn.0003751c @ 0x37838(x)
         r7 = r3 >> 0xb
         lr *= r7
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         (a, b) = compare (r6, 0x3f) // '?'
         bls 0x37824              // likely

         goto loc_0x00037878;
    loc_0x00037878: // orphan
         lr = r6 - 0x40
         (a, b) = compare (lr, 3)
         bls 0x37630              // unlikely

         goto loc_0x00037884;
    loc_0x00037884: // orphan
         (a, b) = compare (lr, 0xd)
         r5 = lr >> 1
         r7 = r5 - 1
         bhi 0x37a88              // unlikely

         goto loc_0x00037894;
    loc_0x00037894: // orphan
         r5 = lr & 1
         rsb lr, r6, 0x2ec
         r5 |= 2
         lr += 3
         add lr, lr, r5, lsl r7
         add r4, r4, lr, lsl 1

    loc_0x000378ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ad8(x)
         r6 = 1
         
         goto loc_0x000378b4;
    loc_0x000378b4: // orphan
         // CODE XREF from fcn.0003751c @ 0x37900(x)
         r8 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r8 | ip

    loc_0x000378c0: // orphan
         // CODE XREF from fcn.0003751c @ 0x378f4(x)
         r8 = r3 >> 0xb
         lr *= r8
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         r7 -= 1
         je 0x37630               // unlikely

         goto loc_0x000378e4;
    loc_0x000378e4: // orphan
         // CODE XREF from fcn.0003751c @ 0x378b0(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         r6 = r5 + 1
         lr = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x378c0              // unlikely

         goto loc_0x000378f8;
    loc_0x000378f8: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         blo 0x378b4              // unlikely

         goto loc_0x00037904;
    loc_0x00037904: // orphan
         
         goto loc_0x00037908;
    loc_0x00037908: // orphan
         // CODE XREF from fcn.0003751c @ 0x376b0(x)
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         r0 = lr | r0

    loc_0x00037918: // orphan
         // CODE XREF from fcn.0003751c @ 0x376a8(x)
         lr = r7 >> 0xb
         lr = ip * lr
         (a, b) = compare (r0, lr)
         bhs 0x379ec              // likely

         goto loc_0x00037928;
    loc_0x00037928: // orphan
         r3 = r8 + 0xf
         (a, b) = compare (lr, 0x1000000)
         add r3, r6, r3, lsl 4
         r3 = r3 << 1
         r3 = (word) [r4 + r3]
         bhs 0x37958              // unlikely

         goto loc_0x00037940;
    loc_0x00037940: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037948;
    loc_0x00037948: // orphan
         ip = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         r0 = ip | r0

    loc_0x00037958: // orphan
         // CODE XREF from fcn.0003751c @ 0x3793c(x)
         ip = lr >> 0xb
         ip = r3 * ip
         (a, b) = compare (r0, ip)
         bhs 0x37a7c              // likely

         goto loc_0x00037968;
    loc_0x00037968: // orphan
         (a, b) = compare (ip, 0x1000000)
         movlo ip, 1
         movhs ip, 0
         (a, b) = compare (r1, r2) // arg3
         movlo ip, 0
         (a, b) = compare (ip, 0)
         movne r0, 0
         moveq r0, 3

         goto loc_0x0003798c;
    loc_0x0003798c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37798(x)
         rsb lr, r3, r5
         rsb ip, r3, ip
         (a, b) = compare (lr, 0x1000000)
         r3 = (word) [r7 + 2]
         bhs 0x379b8              // unlikely

         goto loc_0x000379a0;
    loc_0x000379a0: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x000379a8;
    loc_0x000379a8: // orphan
         r5 = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         ip = r5 | ip

    loc_0x000379b8: // orphan
         // CODE XREF from fcn.0003751c @ 0x3799c(x)
         r5 = lr >> 0xb
         r3 *= r5
         (a, b) = compare (ip, r3)
         addlo r6, r7, r6, lsl 4
         rsbhs ip, r3, ip
         movlo sb, 8
         addlo r7, r6, 0x104
         movlo r8, sb
         addhs r7, r7, 0x204
         rsbhs r3, r3, lr
         movhs sb, 0x10
         movhs r8, 0x100
         
         goto loc_0x000379ec;
    loc_0x000379ec: // orphan
         // CODE XREF from fcn.0003751c @ 0x37924(x)
         rsb r7, lr, r7
         rsb ip, lr, r0
         (a, b) = compare (r7, 0x1000000)
         r0 = (word) [r3 + 0x30]
         bhs 0x37a18              // unlikely

         goto loc_0x00037a00;
    loc_0x00037a00: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a08;
    loc_0x00037a08: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037a18: // orphan
         // CODE XREF from fcn.0003751c @ 0x379fc(x)
         lr = r7 >> 0xb
         r5 = r0 * lr
         (a, b) = compare (ip, r5)
         blo 0x37a68              // unlikely

         goto loc_0x00037a28;
    loc_0x00037a28: // orphan
         rsb r7, r5, r7
         rsb ip, r5, ip
         (a, b) = compare (r7, 0x1000000)
         r3 = (word) [r3 + 0x48]
         bhs 0x37a54              // unlikely

         goto loc_0x00037a3c;
    loc_0x00037a3c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a44;
    loc_0x00037a44: // orphan
         r0 = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = r0 | ip

    loc_0x00037a54: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a38(x)
         lr = r7 >> 0xb
         r5 = r3 * lr
         (a, b) = compare (ip, r5)
         rsbhs ip, r5, ip
         rsbhs r5, r5, r7

    loc_0x00037a68: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37a24(x), 0x37a84(x)
         r7 = r4 + 0xa60
         r0 = 3
         r7 += 8
         sl = 0xc
         
         goto loc_0x00037a7c;
    loc_0x00037a7c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37964(x)
         rsb r5, ip, lr
         rsb ip, ip, r0
         
         goto loc_0x00037a88;
    loc_0x00037a88: // orphan
         // CODE XREF from fcn.0003751c @ 0x37890(x)
         r5 -= 5

    loc_0x00037a8c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ac8(x)
         (a, b) = compare (r3, 0x1000000)
         bhs 0x37aac              // unlikely

         goto loc_0x00037a94;
    loc_0x00037a94: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037aa0;
    loc_0x00037aa0: // orphan
         lr = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037aac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a90(x)
         r3 = r3 >> 1
         r5 -= 1
         rsb lr, r3, ip
         lr = lr >> 0x1f
         lr -= 1
         lr &= r3
         rsb ip, lr, ip
         bne 0x37a8c              // likely

         goto loc_0x00037acc;
    loc_0x00037acc: // orphan
         r4 += 0x640
         r7 = 4
         r4 += r7
         
}


========================================================
// Function at 0x0x37adc
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.00037adc (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_1h, int32_t arg_4h) {
        // CALL XREFS from fcn.000389f8 @ 0x38d60(x), 0x38eec(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r3 = r0       // arg1
        sp -= 0x64
        sb = [r3 + 0x18] // section..text
        fp = [r3 + 0x24]
        [var_48h] = r0 // arg1
        r0 = [0x00037afc] // [0x389cc:4]=0x7d4e8
        [var_5ch] = r1 // arg2
        r0 = pc + r0
        [var_2ch] = r2 // arg3
        [var_44h] = r0
        
    loc_0x00037b08:
        // CODE XREF from fcn.00037adc @ 0x37e78(x)
        r3 = [var_48h]
        r3 = [r3 + 0x30]
        (a, b) = compare (r3, 0)
        [var_20h] = r3
        r3 = [var_48h]
        je 0x38998    // unlikely
        goto loc_0x00037b20;
        return r0;
    loc_0x00037b20:
        r3 = [r3 + 0x2c]
        [var_8h] = r3
        r3 = [var_5ch]
        [var_14h] = r3
        
    loc_0x00037b30:
        // CODE XREF from fcn.00037adc @ 0x389c8(x)
        r1 = [var_48h]
        lr = 1
        r3 = [r1 + 0x10]
        r2 = [r1 + 8]
        r0 = r3
        [var_ch] = r3
        r3 = 0
        [var_1ch] = r3
        r3 = [r1 + 4]
        r2 = lr << r2
        r2 -= 1
        [var_28h] = r2
        r3 = lr << r3
        r2 = [r1 + 0x44]
        r3 -= 1
        [var_50h] = r3
        r3 = [r1]
        ip = r0 + 0x640
        lr = [r1 + 0x34]
        r0 += 0xe60
        ip += 4
        r0 += 0xc
        [var_40h] = r3
        r3 = [r1 + 0x14]
        [var_4h] = lr
        [var_58h] = ip
        [var_18h] = r3
        r3 = [r1 + 0x28]
        lr = [r1 + 0x38] // fcn.000bbf24
        ip = [r1 + 0x3c] // fcn.000bbf24
        [var_30h] = r0
        [var_4ch] = r2
        r0 = [r1 + 0x40] // fcn.000bbf24
        [var_24h] = r3
        r2 = [r1 + 0x1c] // elf_phdr
        r3 = [r1 + 0x20] // elf_shdr
        [var_10h] = lr
        [var_34h] = ip
        [var_3ch] = r0
        return r0;
    loc_0x00037c10: // orphan
         r2 = [var_8h]
         lr = [var_20h]
         r4 = [var_30h]
         orrs r2, r2, lr
         rsb r2, ip, 0x800
         add ip, ip, r2, lsr 5
         r2 = [var_ch]
         [r2 + r0] = (half) ip
         je 0x37c78               // unlikely

         goto loc_0x00037c34;
    loc_0x00037c34: // orphan
         r0 = [var_50h]
         (a, b) = compare (fp, 0)
         r2 = [var_8h]
         ip = [var_18h]
         r2 &= r0
         r0 = [var_40h]
         r2 = r2 << r0
         ldreq r0, [sp, 0x24]
         subne r0, fp, 1
         subeq r0, r0, 1
         ip = (byte) [ip + r0]
         r0 = [var_40h]
         rsb r0, r0, 8
         add r2, r2, ip, asr r0
         r0 = [var_30h]
         add r2, r2, r2, lsl 1
         add r4, r0, r2, lsl 9

    loc_0x00037c78: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c30(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 6)
         bhi 0x384e8              // likely

         goto loc_0x00037c84;
    loc_0x00037c84: // orphan
         r2 = r1
         ip = 1

    loc_0x00037c8c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37ce8(x)
         (a, b) = compare (r2, 0x1000000)
         r5 = ip << 1
         lsllo r2, r2, 8
         ip = r5 + 1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r1 = (word) [r4 + r5]    // "ecompFinal"
         r6 = r4 + r5
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb r7, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt" // "rfaceShow"
         add r1, r1, r7, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         rsbls r3, r0, r3
         movhi ip, r5
         movhi r2, r0
         (a, b) = compare (ip, 0xff)
         [r6] = (half) lr
         bls 0x37c8c              // likely

         goto loc_0x00037cec;
    loc_0x00037cec: // orphan
         // CODE XREF from fcn.00037adc @ 0x3859c(x)
         r1 = [var_18h]
         r0 = [var_4h]
         [r1 + fp] = (byte) ip
         fp += 1
         r1 = [var_8h]
         r1 += 1
         [var_8h] = r1
         r1 = [var_44h]
         r1 = (byte) [r1 + r0]
         [var_4h] = r1

    loc_0x00037d14: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383ec(x), 0x384e4(x), 0x38630(x)
         r1 = [var_2ch]
         r0 = [var_14h]
         (a, b) = compare (r1, sb)
         cmphi r0, fp
         bhi 0x37bcc              // unlikely

         goto loc_0x00037d28;
    loc_0x00037d28: // orphan
         // CODE XREF from fcn.00037adc @ 0x38934(x)
         (a, b) = compare (r2, 0x1000000)
         r0 = [var_48h]
         ip = [var_8h]
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         [r0 + 0x1c] = r2
         orrlo r3, r1, r3, lsl 8
         [r0 + 0x20] = r3
         r3 = [var_1ch]
         r1 = [r0 + 0xc]
         [r0 + 0x2c] = ip
         [r0 + 0x48] = r3
         (a, b) = compare (r1, ip)
         r3 = [var_10h]           // fcn.000bbf24
         ip = [var_1ch]
         [r0 + 0x18] = sb
         [r0 + 0x38] = r3
         r3 = [var_34h]           // fcn.000bbf24
         [r0 + 0x24] = fp
         [r0 + 0x3c] = r3
         r3 = [var_3ch]           // fcn.000bbf24
         [r0 + 0x40] = r3
         r3 = [var_4ch]
         [r0 + 0x44] = r3
         r3 = [var_4h]
         [r0 + 0x34] = r3
         movls r3, r0
         strls r1, [r3, 0x30]
         r3 = ip - 1
         (a, b) = compare (r3, 0x110)
         movhi sl, ip
         bhi 0x37e50              // likely

         goto loc_0x00037dac;
    loc_0x00037dac: // orphan
         r0 = [var_48h]
         r3 = [var_5ch]
         rsb lr, fp, r3
         r3 = [r0 + 0x30]
         (a, b) = compare (lr, ip)
         r2 = [r0 + 0x14]
         r0 = [r0 + 0x28]
         movhs lr, ip
         (a, b) = compare (r3, 0)
         bne 0x37de8              // unlikely

         goto loc_0x00037dd4;
    loc_0x00037dd4: // orphan
         r3 = [var_8h]
         rsb r3, r3, r1
         (a, b) = compare (r3, lr)
         ldrls r3, [sp, 0x48]
         strls r1, [r3, 0x30]

    loc_0x00037de8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37dd0(x)
         r1 = [var_1ch]
         (a, b) = compare (lr, 0)
         r3 = [var_8h]
         rsb sl, lr, r1
         r1 = [var_48h]
         r3 = lr + r3
         [r1 + 0x48] = sl
         [r1 + 0x2c] = r3
         je 0x37e48               // likely

         goto loc_0x00037e0c;
    loc_0x00037e0c: // orphan
         ip = [var_10h]           // fcn.000bbf24
         lr += fp                 // r13
         r1 = r2 + fp             // r13

    loc_0x00037e18: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e3c(x)
         (a, b) = compare (ip, fp)
         rsb r3, ip, fp           // r13
         r3 = r2 + r3             // r13
         fp += 1
         movhi r4, r0
         movls r4, 0
         r3 = (byte) [r3 + r4]
         (a, b) = compare (fp, lr)
         [r1] + 1 = (byte) r3
         bne 0x37e18              // likely

         goto loc_0x00037e40;
    loc_0x00037e40: // orphan
         r3 = [var_48h]
         sl = [r3 + 0x48]

    loc_0x00037e48: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e08(x)
         r3 = [var_48h]
         [r3 + 0x24] = fp

    loc_0x00037e50: // orphan
         // CODE XREF from fcn.00037adc @ 0x37da8(x)
         r3 = [var_5ch]
         (a, b) = compare (r3, fp)
         bls 0x37e7c              // likely

         goto loc_0x00037e5c;
    loc_0x00037e5c: // orphan
         r3 = [var_48h]
         sb = [r3 + 0x18]
         r3 = [var_2ch]
         (a, b) = compare (r3, sb)
         bls 0x37e7c              // likely

         goto loc_0x00037e70;
    loc_0x00037e70: // orphan
         r3 = 0x111
         (a, b) = compare (sl, r3)
         bls 0x37b08              // likely

         return r0;
    loc_0x00037e7c: // orphan
         // CODE XREFS from fcn.00037adc @ 0x37e58(x), 0x37e6c(x)
         r3 = 0x112
         (a, b) = compare (sl, r3)
         ldrhi r2, [sp, 0x48]
         strhi r3, [r2, 0x48]
         r0 = 0
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00037e98: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c0c(x)
         lr = [var_4h]
         rsb r2, r1, r2
         r4 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lr += 0xc0
         rsb r3, r1, r3
         lsllo r2, r2, 8
         sub ip, ip, ip, lsr 5
         lr = lr << 1
         [r4 + r0] = (half) ip
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x383f0              // likely

         goto loc_0x00037ee0;
    loc_0x00037ee0: // orphan
         r4 = [var_ch]
         rsb ip, r0, 0x800
         r5 = [var_4h]
         r2 = r4 + 0x660
         add r0, r0, ip, lsr 5
         r2 += 4
         r5 += 0xc
         [r4 + lr] = (half) r0
         [var_4h] = r5

    loc_0x00037f04: // orphan
         // CODE XREF from fcn.00037adc @ 0x386b4(x)
         (a, b) = compare (r1, 0x1000000)
         ip = (word) [r2]         // "oSha1Final"
         lsllo r1, r1, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x385a0              // likely

         goto loc_0x00037f2c;
    loc_0x00037f2c: // orphan
         add r6, r2, r6, lsl 4
         rsb r1, ip, 0x800
         r6 += 4
         sl = ~7
         r8 = 8
         add ip, ip, r1, lsr 5
         [r2] = (half) ip

    loc_0x00037f48: // orphan
         // CODE XREFS from fcn.00037adc @ 0x385f4(x), 0x386d4(x)
         r5 = 1

    loc_0x00037f4c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fa8(x)
         (a, b) = compare (r0, 0x1000000)
         lr = r5 << 1
         lsllo r0, r0, 8
         r5 = lr + 1
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r2 = (word) [r6 + lr]
         r4 = r6 + lr // DATA XREF from sym.s3eGLGetInt @ 0x8adf0(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb
         rsb r7, r2, 0x800
         r1 = r2 * r1
         sub ip, r2, r2, lsr 5
         add r2, r2, r7, lsr 5
         uxth ip, ip
         (a, b) = compare (r1, r3)
         rsb r0, r1, r0
         uxthhi ip, r2
         rsbls r3, r1, r3
         movhi r5, lr
         movhi r0, r1
         (a, b) = compare (r5, r8)
         [r4] = (half) ip
         blo 0x37f4c              // unlikely

         goto loc_0x00037fac;
    loc_0x00037fac: // orphan
         r2 = [var_4h]
         lr = sb
         (a, b) = compare (r2, 0xb)
         r2 = r5 + sl
         [var_1ch] = r2
         r2 = r0
         bls 0x382bc              // unlikely

         goto loc_0x00037fc8;
    loc_0x00037fc8: // orphan
         r1 = [var_1ch]
         ip = [var_ch]            // "@" // DATA XREF from sym.s3eGLGetInt @ 0x8ad8c(r)
         (a, b) = compare (r1, 3)
         lslls r1, r1, 7
         addls r1, r1, 0x360      // (pstr 0x00000000) ">"
         movhi r1, 0x4e0
         (a, b) = compare (r0, 0x1000000)
         r1 = ip + r1
         lsllo r2, r0, 8
         ip = (word) [r1 + 2]
         r0 = r2 >> 0xb
         ldrblo lr, [sb]
         addlo sb, sb, 1
         r0 = ip * r0
         orrlo r3, lr, r3, lsl 8
         (a, b) = compare (r0, r3)
         rsbhi lr, ip, 0x800
         rsbls r3, r0, r3
         rsbls r0, r0, r2
         movhi r2, 4
         movls r2, 6
         addhi ip, ip, lr, lsr 5
         subls ip, ip, ip, lsr 5
         (a, b) = compare (r0, 0x1000000)
         lsllo r0, r0, 8
         lr = r1 + r2
         uxth ip, ip
         [r1 + 2] = (half) ip
         ldrblo ip, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, ip, r3, lsl 8
         ip = r0 >> 0xb
         ip = r4 * ip
         (a, b) = compare (ip, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, ip, r3
         addls r2, r2, 1          // (pstr 0x00000000) ">"
         addhi r0, r4, r0, lsr 5
         rsbls ip, ip, r0
         uxthls r0, r4
         r2 = r2 << 1
         uxthhi r0, r0
         (a, b) = compare (ip, 0x1000000)
         [lr] = (half) r0
         lsllo ip, ip, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, r0, r3, lsl 8
         r0 = ip >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi ip, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, r2, 1
         addhi ip, r4, ip, lsr 5
         rsbls r0, r0, ip
         movhi lr, r2
         uxthls ip, r4
         uxthhi ip, ip
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + r2] = (half) ip
         lsllo r0, r0, 8
         ldrblo r2, [sb]          // (pstr 0x00000000) ">"
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r2, r3
         addls ip, lr, 1          // (pstr 0x00005400) "rGetLocaltimeOffset"
         addhi r0, r4, r0, lsr 5
         rsbls r2, r2, r0
         movhi ip, lr
         uxthls r0, r4
         uxthhi r0, r0
         (a, b) = compare (r2, 0x1000000)
         ip = ip << 1
         lsllo r2, r2, 8
         [r1 + lr] = (half) r0
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + ip]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi r2, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, ip, 1
         addhi r2, r4, r2, lsr 5
         rsbls r0, r0, r2
         movhi lr, ip
         uxthls r2, r4
         uxthhi r2, r2
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + ip] = (half) r2
         lsllo r0, r0, 8
         ldrblo r2, [sb]
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi ip, r4, 0x800
         rsbls r3, r2, r3
         rsbls r2, r2, r0
         addls r0, lr, 1
         addhi r4, r4, ip, lsr 5
         movhi r0, lr
         ip = r0 - 0x40
         subls r4, r4, r4, lsr 5
         (a, b) = compare (ip, 3)
         uxth r4, r4
         [r1 + lr] = (half) r4
         bls 0x38270              // unlikely

         goto loc_0x000381c0;
    loc_0x000381c0: // orphan
         (a, b) = compare (ip, 0xd)
         r1 = ip >> 1
         ip &= 1
         r6 = r1 - 1
         ip |= 2
         bhi 0x38774              // unlikely

         goto loc_0x000381d8;
    loc_0x000381d8: // orphan
         rsb r0, r0, 0x2ec
         ip = ip << r6
         r0 += 3
         r7 = [var_ch]            // "@"
         r8 = 1
         r0 += ip
         sl = r8
         [var_4ch] = fp
         r1 = r0 << r8
         [var_38h] = r1

    loc_0x00038200: // orphan
         // CODE XREF from fcn.00037adc @ 0x38268(x)
         r1 = [var_38h]
         (a, b) = compare (r2, 0x1000000)
         r4 = r8 << 1
         lsllo r2, r2, 8
         r5 = r4 + r1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r8 = r4 + 1
         r1 = (word) [r7 + r5]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb fp, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt"
         add r1, r1, fp, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         orrls ip, ip, sl
         movhi r8, r4
         movhi r2, r0
         rsbls r3, r0, r3
         r6 -= 1
         [r7 + r5] = (half) lr
         sl = sl << 1
         bne 0x38200              // likely

         goto loc_0x0003826c;
    loc_0x0003826c: // orphan
         fp = [var_4ch]           // (pstr 0x00000000) ">" r13

    loc_0x00038270: // orphan
         // CODE XREFS from fcn.00037adc @ 0x381bc(x), 0x38914(x)
         r1 = [var_20h]           // (cstr 0x00000000) ">"
         (a, b) = compare (r1, 0)
         r1 = ip + 1
         bne 0x386d8              // likely

         goto loc_0x00038280;
    loc_0x00038280: // orphan
         r0 = [var_8h]
         (a, b) = compare (r0, ip)
         bls 0x386e4              // unlikely

         goto loc_0x0003828c;
    loc_0x0003828c: // orphan
         // CODE XREF from fcn.00037adc @ 0x386e0(x)
         r0 = [var_4h]
         (a, b) = compare (r0, 0x12)
         r0 = [var_3ch]           // fcn.000bbf24
         [var_4ch] = r0
         movhi r0, 0xa            // (pstr 0x00000000) ">"
         movls r0, 7
         [var_4h] = r0
         r0 = [var_34h]           // fcn.000bbf24
         [var_3ch] = r0
         r0 = [var_10h]           // fcn.000bbf24
         [var_10h] = r1
         [var_34h] = r0

    loc_0x000382bc: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fc4(x)
         r1 = [var_1ch]
         r4 = [var_10h]
         r5 = r1 + 2
         r1 = [var_14h]
         lr = [var_24h]
         rsb r0, fp, r1
         rsb r1, r4, fp           // (pstr 0x00000000) ">" r13
         (a, b) = compare (r5, r0)
         ip = lr
         movlo r0, r5
         (a, b) = compare (fp, r4)
         rsb r4, r0, r5
         [var_1ch] = r4
         movhs ip, 0
         r1 += ip                 // (pstr 0x00000000) ">" r13
         ip = [var_8h]
         ip += r0
         [var_8h] = ip
         ip = r1 + r0
         (a, b) = compare (lr, ip)
         blo 0x385f8              // unlikely

         goto loc_0x00038310;
    loc_0x00038310: // orphan
         r6 = [var_18h]
         rsb r1, fp, r1
         r8 = arg_4h
         sl = fp + r1
         ip = r6 + fp
         r5 = r8 + r1
         lr = ip + 1
         r7 = ip + r0
         (a, b) = compare (sl, r8)
         cmplt fp, r5
         rsb r4, lr, r7
         r6 += sl
         [var_54h] = lr
         sl = r6 | ip
         lr = r4 + 1
         movge r5, 1
         movlt r5, 0
         (a, b) = compare (lr, 9)
         movls r5, 0
         andhi r5, r5, 1
         (a, b) = compare (sl, 3)
         andeq r5, r5, 1
         [var_38h] = lr
         movne r5, 0
         (a, b) = compare (r5, 0)
         r0 += fp                 // (pstr 0x00000000) ">" r13
         je 0x38938               // likely

         goto loc_0x0003837c;
    loc_0x0003837c: // orphan
         r4 -= 3
         r6 -= 4
         r8 = ip
         r5 = 0
         r4 = r4 >> 2
         lr = r4 + 1
         r4 = lr << 2

    loc_0x00038398: // orphan
         // CODE XREF from fcn.00037adc @ 0x383a8(x)
         r5 += 1
         sl = [r6 + 4]!
         [r8] + 4 = sl
         blo 0x38398              // unlikely

         goto loc_0x000383ac;
    loc_0x000383ac: // orphan
         r5 = [var_38h]
         (a, b) = compare (r5, r4)
         r4 = ip + r4
         je 0x383e8               // likely

         goto loc_0x000383bc;
    loc_0x000383bc: // orphan
         r6 = (byte) [r4 + r1]
         r5 = r4 + 1
         (a, b) = compare (r7, r5)
         strb r6, [ip, lr, lsl 2]
         je 0x383e8               // unlikely

         goto loc_0x000383d0;
    loc_0x000383d0: // orphan
         lr = (byte) [r5 + r1]
         ip = r4 + 2
         (a, b) = compare (r7, ip)
         [r4 + 1] = (byte) lr
         ldrbne r1, [ip, r1]
         strbne r1, [r4, 2]

    loc_0x000383e8: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383b8(x), 0x383cc(x), 0x3895c(x)
         fp = r0
         
         goto loc_0x000383f0;
    loc_0x000383f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37edc(x)
         ip = [var_8h]
         sub r0, r0, r0, lsr 5
         r4 = [var_20h]
         rsb r2, r1, r2
         rsb r3, r1, r3
         orrs ip, ip, r4
         ip = [var_ch]            // "@"
         [ip + lr] = (half) r0
         je 0x386e4               // unlikely

         goto loc_0x00038414;
    loc_0x00038414: // orphan
         (a, b) = compare (r2, 0x1000000)
         r4 = lr + 0x18
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [ip + r4]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38634              // likely

         goto loc_0x00038440;
    loc_0x00038440: // orphan
         r2 = [var_4h]
         rsb ip, r0, 0x800
         (a, b) = compare (r1, 0x1000000)
         r2 += 0xf
         add r0, r0, ip, lsr 5
         ip = [var_ch]
         lsllo r1, r1, 8
         add r2, r6, r2, lsl 4
         [ip + r4] = (half) r0
         r0 = r2 << 1
         ldrblo r2, [sb]
         addlo sb, sb, 1
         ip = (word) [ip + r0]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb
         r2 = ip * r2
         (a, b) = compare (r2, r3)
         bls 0x386f0              // likely

         goto loc_0x00038488;
    loc_0x00038488: // orphan
         rsb lr, ip, 0x800
         r5 = [var_10h]
         r6 = [var_18h]
         add ip, ip, lr, lsr 5
         lr = [var_ch]            // "@"
         r4 = [var_24h]
         (a, b) = compare (fp, r5)
         rsb r1, r5, fp           // r13
         r1 = r6 + r1
         movhs r4, 0
         [lr + r0] = (half) ip
         r0 = [var_4h]
         r1 = (byte) [r1 + r4]
         r4 = r6
         (a, b) = compare (r0, 7)
         r0 = [var_8h]
         [r6 + fp] = (byte) r1
         r0 += 1
         fp += 1
         [var_8h] = r0
         movlo r0, 9
         movhs r0, 0xb
         [var_4h] = r0
         
         goto loc_0x000384e8;
    loc_0x000384e8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c80(x)
         r2 = [var_48h]
         r6 = 0x100
         r0 = [var_24h]
         [var_38h] = fp
         lr = [r2 + 0x14]
         r2 = [var_10h]
         rsb ip, r2, fp           // r13
         (a, b) = compare (fp, r2)
         r2 = r1
         r1 = lr + ip
         movhs r0, 0
         ip = 1
         r7 = (byte) [r1 + r0]

    loc_0x0003851c: // orphan
         // CODE XREF from fcn.00037adc @ 0x38594(x)
         r7 = r7 << 1
         r1 = ip + r6
         r5 = r6 & r7
         (a, b) = compare (r2, 0x1000000)
         r1 += r5
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         fp = ip << 1
         r1 = r1 << 1
         addlo sb, sb, 1
         r8 = r4 + r1
         orrlo r3, r0, r3, lsl 8
         r1 = (word) [r4 + r1]
         r0 = r2 >> 0xb
         rsb sl, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5
         add r1, r1, sl, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         lslhi ip, ip, 1
         addls ip, fp, 1
         uxthhi lr, r1
         bichi r6, r6, r5
         movhi r2, r0
         rsbls r3, r0, r3
         andls r6, r6, r5
         (a, b) = compare (ip, 0xff)
         [r8] = (half) lr
         bls 0x3851c              // unlikely

         goto loc_0x00038598;
    loc_0x00038598: // orphan
         fp = [var_38h]           // r13
         
         goto loc_0x000385a0;
    loc_0x000385a0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37f28(x)
         rsb r1, r0, r1
         rsb r3, r0, r3
         (a, b) = compare (r1, 0x1000000)
         sub ip, ip, ip, lsr 5
         lsllo r1, r1, 8
         [r2] = (half) ip
         ldrblo r0, [sb]
         addlo sb, sb, 1
         ip = (word) [r2 + 2]
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x386b8              // likely

         goto loc_0x000385d8;
    loc_0x000385d8: // orphan
         rsb r1, ip, 0x800
         add r6, r2, r6, lsl 4
         r6 += 0x104
         sl = 0
         add ip, ip, r1, lsr 5
         r8 = 8
         [r2 + 2] = (half) ip
         
         goto loc_0x000385f8;
    loc_0x000385f8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3830c(x)
         r5 = lr
         lr = [var_18h]
         ip = lr + fp
         fp += r0                 // r13
         r0 = lr
         lr += fp
         r4 = r0

    loc_0x00038614: // orphan
         // CODE XREF from fcn.00037adc @ 0x3862c(x)
         r0 = (byte) [r4 + r1]
         r1 += 1
         (a, b) = compare (r5, r1)
         [ip] + 1 = (byte) r0
         moveq r1, 0
         (a, b) = compare (ip, lr)
         bne 0x38614              // likely

         goto loc_0x00038630;
    loc_0x00038630: // orphan
         
         goto loc_0x00038634;
    loc_0x00038634: // orphan
         // CODE XREF from fcn.00037adc @ 0x3843c(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         ip = lr + 0x30
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + r4] = (half) r0
         r0 = (word) [r1 + ip]
         ldrblo r1, [sb]
         addlo sb, sb, 1 // DATA XREFS from fcn.0007cae4 @ 0x7cc20(r), 0x7cc3c(r), 0x7cc48(r), 0x7cc4c(r), 0x7cc64(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38708              // likely

         goto loc_0x00038674;
    loc_0x00038674: // orphan
         rsb r2, r0, 0x800
         lr = [var_10h]
         r4 = [var_34h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_34h] = lr
         [var_10h] = r4
         [r0 + ip] = (half) r2

    loc_0x00038694: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38704(x), 0x38770(x), 0x38994(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 7)
         r2 = [var_ch]            // "@"
         r2 += 0xa60
         movlo r0, 8
         movhs r0, 0xb
         r2 += 8
         [var_4h] = r0
         
         goto loc_0x000386b8;
    loc_0x000386b8: // orphan
         // CODE XREF from fcn.00037adc @ 0x385d4(x)
         rsb r3, r0, r3
         sub ip, ip, ip, lsr 5
         rsb r0, r0, r1
         r6 = r2 + 0x204
         sl = ~0xef
         [r2 + 2] = (half) ip
         r8 = 0x100
         
         goto loc_0x000386d8;
    loc_0x000386d8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3827c(x)
         r0 = [var_20h]
         (a, b) = compare (r0, ip)
         bhi 0x3828c              // likely

         return r0;
    loc_0x000386e4: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38288(x), 0x38410(x)
         r0 = 1
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x000386f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x38484(x)
         lr = [var_ch]            // "@"
         sub ip, ip, ip, lsr 5
         rsb r1, r2, r1
         rsb r3, r2, r3
         [lr + r0] = (half) ip
         
         goto loc_0x00038708;
    loc_0x00038708: // orphan
         // CODE XREF from fcn.00037adc @ 0x38670(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]
         (a, b) = compare (r2, 0x1000000)
         lr += 0x48
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + ip] = (half) r0
         r0 = (word) [r1 + lr]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38960              // likely

         goto loc_0x00038748;
    loc_0x00038748: // orphan
         r4 = [var_3ch]           // fcn.000bbf24
         rsb r2, r0, 0x800
         ip = [var_10h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_10h] = r4
         r4 = [var_34h]
         [r0 + lr] = (half) r2
         [var_34h] = ip
         [var_3ch] = r4
         
         goto loc_0x00038774;
    loc_0x00038774: // orphan
         // CODE XREF from fcn.00037adc @ 0x381d4(x)
         r1 -= 5

    loc_0x00038778: // orphan
         // CODE XREF from fcn.00037adc @ 0x387ac(x)
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r2 = r2 >> 1
         orrlo r3, r0, r3, lsl 8
         r1 -= 1
         rsb r3, r2, r3
         r0 = r3 >> 0x1f
         add ip, r0, ip, lsl 1
         r0 &= r2
         ip += 1
         r3 = r0 + r3
         bne 0x38778              // likely

         goto loc_0x000387b0;
    loc_0x000387b0: // orphan
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ip = ip << 4
         r0 = r1 + 0x640
         r1 = sb
         r0 += 6
         ldrblo r1, [sb]
         lr = (word) [r0]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb           // 0x6c20 // "hatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r4, lr, 0x800
         subls lr, lr, lr, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r2
         addhi lr, lr, r4, lsr 5
         uxthls lr, lr
         orrls ip, ip, 1
         movls r2, 6
         uxthhi lr, lr
         [r0] = (half) lr
         r0 = [var_58h]
         movhi r2, 4
         (a, b) = compare (r1, 0x1000000)
         r4 = r0 + r2
         lsllo r1, r1, 8
         lr = (word) [r0 + r2]
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb           // 0x6c20 // "hatEv"
         r0 = lr * r0             // 0x6c20 // "hatEv"
         (a, b) = compare (r0, r3)
         rsbhi r1, lr, 0x800
         subls lr, lr, lr, lsr 5
         addls r2, r2, 1
         rsbls r3, r0, r3
         addhi lr, lr, r1, lsr 5
         rsbls r0, r0, r1
         r1 = [var_58h]
         r2 = r2 << 1
         orrls ip, ip, 2
         uxthhi lr, lr
         uxthls lr, lr
         (a, b) = compare (r0, 0x1000000)
         [r4] = (half) lr
         lsllo r0, r0, 8
         lr = (word) [r1 + r2]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r0, lr, 0x800
         addls r4, r2, 1
         subls lr, lr, lr, lsr 5
         movhi r4, r2
         addhi r0, lr, r0, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r0
         uxthls r0, lr
         lr = r4 << 1
         r4 = [var_58h]
         uxthhi r0, r0
         orrls ip, ip, 4
         (a, b) = compare (r1, 0x1000000)
         lsllo r1, r1, 8
         [r4 + r2] = (half) r0
         ldrblo r2, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r2 = r0 * r2
         (a, b) = compare (r2, r3)
         rsbhi r1, r0, 0x800
         rsbls r3, r2, r3
         orrls ip, ip, 8
         rsbls r2, r2, r1
         addhi r0, r0, r1, lsr 5
         r1 = [var_58h]
         subls r0, r0, r0, lsr 5
         if (ip != 1)
         uxth r0, r0
         [r1 + lr] = (half) r0
         bne 0x38270              // likely

         goto loc_0x00038918;
    loc_0x00038918: // orphan
         r1 = [var_1ch]
         r0 = [var_4h]
         r1 += 0x110
         r0 -= 0xc
         r1 += 2
         [var_4h] = r0
         [var_1ch] = r1
         
         goto loc_0x00038938;
    loc_0x00038938: // orphan
         // CODE XREF from fcn.00037adc @ 0x38378(x)
         r1 = ip + r1
         lr = [var_54h]
         
         goto loc_0x00038944;
    loc_0x00038944: // orphan
         // CODE XREF from fcn.00037adc @ 0x38958(x)
         lr += 1

    loc_0x00038948: // orphan
         // CODE XREF from fcn.00037adc @ 0x38940(x)
         r4 = (byte) [r1] + 1
         (a, b) = compare (lr, r7)
         [ip] = (byte) r4
         ip = lr
         bne 0x38944              // unlikely

         goto loc_0x0003895c;
    loc_0x0003895c: // orphan
         
         goto loc_0x00038960;
    loc_0x00038960: // orphan
         // CODE XREF from fcn.00037adc @ 0x38744(x)
         rsb r3, r1, r3
         rsb r1, r1, r2
         r2 = [var_4ch]           // fcn.000bbf24
         sub r0, r0, r0, lsr 5
         ip = [var_10h]           // fcn.000bbf24
         [var_10h] = r2
         r2 = [var_3ch]
         [var_4ch] = r2
         r2 = [var_34h]           // fcn.000bbf24
         [var_34h] = ip
         [var_3ch] = r2
         r2 = [var_ch]            // "@"
         [r2 + lr] = (half) r0
         
         goto loc_0x00038998;
    loc_0x00038998: // orphan
         // CODE XREF from fcn.00037adc @ 0x37b1c(x)
         r1 = [r3 + 0x2c]
         r2 = [var_5ch]
         r3 = [r3 + 0xc]
         rsb r2, fp, r2
         rsb r3, r1, r3
         (a, b) = compare (r3, r2)
         strlo r1, [sp, 8]
         addlo r3, fp, r3
         strhs r1, [sp, 8]
         ldrhs r3, [sp, 0x5c]
         strlo r3, [sp, 0x14]
         strhs r3, [sp, 0x14]
         
}


========================================================
// Function at 0x0x37e28
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.00037adc (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_1h, int32_t arg_4h) {
        // CALL XREFS from fcn.000389f8 @ 0x38d60(x), 0x38eec(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r3 = r0       // arg1
        sp -= 0x64
        sb = [r3 + 0x18] // section..text
        fp = [r3 + 0x24]
        [var_48h] = r0 // arg1
        r0 = [0x00037afc] // [0x389cc:4]=0x7d4e8
        [var_5ch] = r1 // arg2
        r0 = pc + r0
        [var_2ch] = r2 // arg3
        [var_44h] = r0
        
    loc_0x00037b08:
        // CODE XREF from fcn.00037adc @ 0x37e78(x)
        r3 = [var_48h]
        r3 = [r3 + 0x30]
        (a, b) = compare (r3, 0)
        [var_20h] = r3
        r3 = [var_48h]
        je 0x38998    // unlikely
        goto loc_0x00037b20;
        return r0;
    loc_0x00037b20:
        r3 = [r3 + 0x2c]
        [var_8h] = r3
        r3 = [var_5ch]
        [var_14h] = r3
        
    loc_0x00037b30:
        // CODE XREF from fcn.00037adc @ 0x389c8(x)
        r1 = [var_48h]
        lr = 1
        r3 = [r1 + 0x10]
        r2 = [r1 + 8]
        r0 = r3
        [var_ch] = r3
        r3 = 0
        [var_1ch] = r3
        r3 = [r1 + 4]
        r2 = lr << r2
        r2 -= 1
        [var_28h] = r2
        r3 = lr << r3
        r2 = [r1 + 0x44]
        r3 -= 1
        [var_50h] = r3
        r3 = [r1]
        ip = r0 + 0x640
        lr = [r1 + 0x34]
        r0 += 0xe60
        ip += 4
        r0 += 0xc
        [var_40h] = r3
        r3 = [r1 + 0x14]
        [var_4h] = lr
        [var_58h] = ip
        [var_18h] = r3
        r3 = [r1 + 0x28]
        lr = [r1 + 0x38] // fcn.000bbf24
        ip = [r1 + 0x3c] // fcn.000bbf24
        [var_30h] = r0
        [var_4ch] = r2
        r0 = [r1 + 0x40] // fcn.000bbf24
        [var_24h] = r3
        r2 = [r1 + 0x1c] // elf_phdr
        r3 = [r1 + 0x20] // elf_shdr
        [var_10h] = lr
        [var_34h] = ip
        [var_3ch] = r0
        return r0;
    loc_0x00037c10: // orphan
         r2 = [var_8h]
         lr = [var_20h]
         r4 = [var_30h]
         orrs r2, r2, lr
         rsb r2, ip, 0x800
         add ip, ip, r2, lsr 5
         r2 = [var_ch]
         [r2 + r0] = (half) ip
         je 0x37c78               // unlikely

         goto loc_0x00037c34;
    loc_0x00037c34: // orphan
         r0 = [var_50h]
         (a, b) = compare (fp, 0)
         r2 = [var_8h]
         ip = [var_18h]
         r2 &= r0
         r0 = [var_40h]
         r2 = r2 << r0
         ldreq r0, [sp, 0x24]
         subne r0, fp, 1
         subeq r0, r0, 1
         ip = (byte) [ip + r0]
         r0 = [var_40h]
         rsb r0, r0, 8
         add r2, r2, ip, asr r0
         r0 = [var_30h]
         add r2, r2, r2, lsl 1
         add r4, r0, r2, lsl 9

    loc_0x00037c78: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c30(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 6)
         bhi 0x384e8              // likely

         goto loc_0x00037c84;
    loc_0x00037c84: // orphan
         r2 = r1
         ip = 1

    loc_0x00037c8c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37ce8(x)
         (a, b) = compare (r2, 0x1000000)
         r5 = ip << 1
         lsllo r2, r2, 8
         ip = r5 + 1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r1 = (word) [r4 + r5]    // "ecompFinal"
         r6 = r4 + r5
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb r7, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt" // "rfaceShow"
         add r1, r1, r7, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         rsbls r3, r0, r3
         movhi ip, r5
         movhi r2, r0
         (a, b) = compare (ip, 0xff)
         [r6] = (half) lr
         bls 0x37c8c              // likely

         goto loc_0x00037cec;
    loc_0x00037cec: // orphan
         // CODE XREF from fcn.00037adc @ 0x3859c(x)
         r1 = [var_18h]
         r0 = [var_4h]
         [r1 + fp] = (byte) ip
         fp += 1
         r1 = [var_8h]
         r1 += 1
         [var_8h] = r1
         r1 = [var_44h]
         r1 = (byte) [r1 + r0]
         [var_4h] = r1

    loc_0x00037d14: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383ec(x), 0x384e4(x), 0x38630(x)
         r1 = [var_2ch]
         r0 = [var_14h]
         (a, b) = compare (r1, sb)
         cmphi r0, fp
         bhi 0x37bcc              // unlikely

         goto loc_0x00037d28;
    loc_0x00037d28: // orphan
         // CODE XREF from fcn.00037adc @ 0x38934(x)
         (a, b) = compare (r2, 0x1000000)
         r0 = [var_48h]
         ip = [var_8h]
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         [r0 + 0x1c] = r2
         orrlo r3, r1, r3, lsl 8
         [r0 + 0x20] = r3
         r3 = [var_1ch]
         r1 = [r0 + 0xc]
         [r0 + 0x2c] = ip
         [r0 + 0x48] = r3
         (a, b) = compare (r1, ip)
         r3 = [var_10h]           // fcn.000bbf24
         ip = [var_1ch]
         [r0 + 0x18] = sb
         [r0 + 0x38] = r3
         r3 = [var_34h]           // fcn.000bbf24
         [r0 + 0x24] = fp
         [r0 + 0x3c] = r3
         r3 = [var_3ch]           // fcn.000bbf24
         [r0 + 0x40] = r3
         r3 = [var_4ch]
         [r0 + 0x44] = r3
         r3 = [var_4h]
         [r0 + 0x34] = r3
         movls r3, r0
         strls r1, [r3, 0x30]
         r3 = ip - 1
         (a, b) = compare (r3, 0x110)
         movhi sl, ip
         bhi 0x37e50              // likely

         goto loc_0x00037dac;
    loc_0x00037dac: // orphan
         r0 = [var_48h]
         r3 = [var_5ch]
         rsb lr, fp, r3
         r3 = [r0 + 0x30]
         (a, b) = compare (lr, ip)
         r2 = [r0 + 0x14]
         r0 = [r0 + 0x28]
         movhs lr, ip
         (a, b) = compare (r3, 0)
         bne 0x37de8              // unlikely

         goto loc_0x00037dd4;
    loc_0x00037dd4: // orphan
         r3 = [var_8h]
         rsb r3, r3, r1
         (a, b) = compare (r3, lr)
         ldrls r3, [sp, 0x48]
         strls r1, [r3, 0x30]

    loc_0x00037de8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37dd0(x)
         r1 = [var_1ch]
         (a, b) = compare (lr, 0)
         r3 = [var_8h]
         rsb sl, lr, r1
         r1 = [var_48h]
         r3 = lr + r3
         [r1 + 0x48] = sl
         [r1 + 0x2c] = r3
         je 0x37e48               // likely

         goto loc_0x00037e0c;
    loc_0x00037e0c: // orphan
         ip = [var_10h]           // fcn.000bbf24
         lr += fp                 // r13
         r1 = r2 + fp             // r13

    loc_0x00037e18: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e3c(x)
         (a, b) = compare (ip, fp)
         rsb r3, ip, fp           // r13
         r3 = r2 + r3             // r13
         fp += 1
         movhi r4, r0
         movls r4, 0
         r3 = (byte) [r3 + r4]
         (a, b) = compare (fp, lr)
         [r1] + 1 = (byte) r3
         bne 0x37e18              // likely

         goto loc_0x00037e40;
    loc_0x00037e40: // orphan
         r3 = [var_48h]
         sl = [r3 + 0x48]

    loc_0x00037e48: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e08(x)
         r3 = [var_48h]
         [r3 + 0x24] = fp

    loc_0x00037e50: // orphan
         // CODE XREF from fcn.00037adc @ 0x37da8(x)
         r3 = [var_5ch]
         (a, b) = compare (r3, fp)
         bls 0x37e7c              // likely

         goto loc_0x00037e5c;
    loc_0x00037e5c: // orphan
         r3 = [var_48h]
         sb = [r3 + 0x18]
         r3 = [var_2ch]
         (a, b) = compare (r3, sb)
         bls 0x37e7c              // likely

         goto loc_0x00037e70;
    loc_0x00037e70: // orphan
         r3 = 0x111
         (a, b) = compare (sl, r3)
         bls 0x37b08              // likely

         return r0;
    loc_0x00037e7c: // orphan
         // CODE XREFS from fcn.00037adc @ 0x37e58(x), 0x37e6c(x)
         r3 = 0x112
         (a, b) = compare (sl, r3)
         ldrhi r2, [sp, 0x48]
         strhi r3, [r2, 0x48]
         r0 = 0
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00037e98: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c0c(x)
         lr = [var_4h]
         rsb r2, r1, r2
         r4 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lr += 0xc0
         rsb r3, r1, r3
         lsllo r2, r2, 8
         sub ip, ip, ip, lsr 5
         lr = lr << 1
         [r4 + r0] = (half) ip
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x383f0              // likely

         goto loc_0x00037ee0;
    loc_0x00037ee0: // orphan
         r4 = [var_ch]
         rsb ip, r0, 0x800
         r5 = [var_4h]
         r2 = r4 + 0x660
         add r0, r0, ip, lsr 5
         r2 += 4
         r5 += 0xc
         [r4 + lr] = (half) r0
         [var_4h] = r5

    loc_0x00037f04: // orphan
         // CODE XREF from fcn.00037adc @ 0x386b4(x)
         (a, b) = compare (r1, 0x1000000)
         ip = (word) [r2]         // "oSha1Final"
         lsllo r1, r1, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x385a0              // likely

         goto loc_0x00037f2c;
    loc_0x00037f2c: // orphan
         add r6, r2, r6, lsl 4
         rsb r1, ip, 0x800
         r6 += 4
         sl = ~7
         r8 = 8
         add ip, ip, r1, lsr 5
         [r2] = (half) ip

    loc_0x00037f48: // orphan
         // CODE XREFS from fcn.00037adc @ 0x385f4(x), 0x386d4(x)
         r5 = 1

    loc_0x00037f4c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fa8(x)
         (a, b) = compare (r0, 0x1000000)
         lr = r5 << 1
         lsllo r0, r0, 8
         r5 = lr + 1
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r2 = (word) [r6 + lr]
         r4 = r6 + lr // DATA XREF from sym.s3eGLGetInt @ 0x8adf0(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb
         rsb r7, r2, 0x800
         r1 = r2 * r1
         sub ip, r2, r2, lsr 5
         add r2, r2, r7, lsr 5
         uxth ip, ip
         (a, b) = compare (r1, r3)
         rsb r0, r1, r0
         uxthhi ip, r2
         rsbls r3, r1, r3
         movhi r5, lr
         movhi r0, r1
         (a, b) = compare (r5, r8)
         [r4] = (half) ip
         blo 0x37f4c              // unlikely

         goto loc_0x00037fac;
    loc_0x00037fac: // orphan
         r2 = [var_4h]
         lr = sb
         (a, b) = compare (r2, 0xb)
         r2 = r5 + sl
         [var_1ch] = r2
         r2 = r0
         bls 0x382bc              // unlikely

         goto loc_0x00037fc8;
    loc_0x00037fc8: // orphan
         r1 = [var_1ch]
         ip = [var_ch]            // "@" // DATA XREF from sym.s3eGLGetInt @ 0x8ad8c(r)
         (a, b) = compare (r1, 3)
         lslls r1, r1, 7
         addls r1, r1, 0x360      // (pstr 0x00000000) ">"
         movhi r1, 0x4e0
         (a, b) = compare (r0, 0x1000000)
         r1 = ip + r1
         lsllo r2, r0, 8
         ip = (word) [r1 + 2]
         r0 = r2 >> 0xb
         ldrblo lr, [sb]
         addlo sb, sb, 1
         r0 = ip * r0
         orrlo r3, lr, r3, lsl 8
         (a, b) = compare (r0, r3)
         rsbhi lr, ip, 0x800
         rsbls r3, r0, r3
         rsbls r0, r0, r2
         movhi r2, 4
         movls r2, 6
         addhi ip, ip, lr, lsr 5
         subls ip, ip, ip, lsr 5
         (a, b) = compare (r0, 0x1000000)
         lsllo r0, r0, 8
         lr = r1 + r2
         uxth ip, ip
         [r1 + 2] = (half) ip
         ldrblo ip, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, ip, r3, lsl 8
         ip = r0 >> 0xb
         ip = r4 * ip
         (a, b) = compare (ip, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, ip, r3
         addls r2, r2, 1          // (pstr 0x00000000) ">"
         addhi r0, r4, r0, lsr 5
         rsbls ip, ip, r0
         uxthls r0, r4
         r2 = r2 << 1
         uxthhi r0, r0
         (a, b) = compare (ip, 0x1000000)
         [lr] = (half) r0
         lsllo ip, ip, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, r0, r3, lsl 8
         r0 = ip >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi ip, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, r2, 1
         addhi ip, r4, ip, lsr 5
         rsbls r0, r0, ip
         movhi lr, r2
         uxthls ip, r4
         uxthhi ip, ip
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + r2] = (half) ip
         lsllo r0, r0, 8
         ldrblo r2, [sb]          // (pstr 0x00000000) ">"
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r2, r3
         addls ip, lr, 1          // (pstr 0x00005400) "rGetLocaltimeOffset"
         addhi r0, r4, r0, lsr 5
         rsbls r2, r2, r0
         movhi ip, lr
         uxthls r0, r4
         uxthhi r0, r0
         (a, b) = compare (r2, 0x1000000)
         ip = ip << 1
         lsllo r2, r2, 8
         [r1 + lr] = (half) r0
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + ip]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi r2, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, ip, 1
         addhi r2, r4, r2, lsr 5
         rsbls r0, r0, r2
         movhi lr, ip
         uxthls r2, r4
         uxthhi r2, r2
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + ip] = (half) r2
         lsllo r0, r0, 8
         ldrblo r2, [sb]
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi ip, r4, 0x800
         rsbls r3, r2, r3
         rsbls r2, r2, r0
         addls r0, lr, 1
         addhi r4, r4, ip, lsr 5
         movhi r0, lr
         ip = r0 - 0x40
         subls r4, r4, r4, lsr 5
         (a, b) = compare (ip, 3)
         uxth r4, r4
         [r1 + lr] = (half) r4
         bls 0x38270              // unlikely

         goto loc_0x000381c0;
    loc_0x000381c0: // orphan
         (a, b) = compare (ip, 0xd)
         r1 = ip >> 1
         ip &= 1
         r6 = r1 - 1
         ip |= 2
         bhi 0x38774              // unlikely

         goto loc_0x000381d8;
    loc_0x000381d8: // orphan
         rsb r0, r0, 0x2ec
         ip = ip << r6
         r0 += 3
         r7 = [var_ch]            // "@"
         r8 = 1
         r0 += ip
         sl = r8
         [var_4ch] = fp
         r1 = r0 << r8
         [var_38h] = r1

    loc_0x00038200: // orphan
         // CODE XREF from fcn.00037adc @ 0x38268(x)
         r1 = [var_38h]
         (a, b) = compare (r2, 0x1000000)
         r4 = r8 << 1
         lsllo r2, r2, 8
         r5 = r4 + r1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r8 = r4 + 1
         r1 = (word) [r7 + r5]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb fp, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt"
         add r1, r1, fp, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         orrls ip, ip, sl
         movhi r8, r4
         movhi r2, r0
         rsbls r3, r0, r3
         r6 -= 1
         [r7 + r5] = (half) lr
         sl = sl << 1
         bne 0x38200              // likely

         goto loc_0x0003826c;
    loc_0x0003826c: // orphan
         fp = [var_4ch]           // (pstr 0x00000000) ">" r13

    loc_0x00038270: // orphan
         // CODE XREFS from fcn.00037adc @ 0x381bc(x), 0x38914(x)
         r1 = [var_20h]           // (cstr 0x00000000) ">"
         (a, b) = compare (r1, 0)
         r1 = ip + 1
         bne 0x386d8              // likely

         goto loc_0x00038280;
    loc_0x00038280: // orphan
         r0 = [var_8h]
         (a, b) = compare (r0, ip)
         bls 0x386e4              // unlikely

         goto loc_0x0003828c;
    loc_0x0003828c: // orphan
         // CODE XREF from fcn.00037adc @ 0x386e0(x)
         r0 = [var_4h]
         (a, b) = compare (r0, 0x12)
         r0 = [var_3ch]           // fcn.000bbf24
         [var_4ch] = r0
         movhi r0, 0xa            // (pstr 0x00000000) ">"
         movls r0, 7
         [var_4h] = r0
         r0 = [var_34h]           // fcn.000bbf24
         [var_3ch] = r0
         r0 = [var_10h]           // fcn.000bbf24
         [var_10h] = r1
         [var_34h] = r0

    loc_0x000382bc: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fc4(x)
         r1 = [var_1ch]
         r4 = [var_10h]
         r5 = r1 + 2
         r1 = [var_14h]
         lr = [var_24h]
         rsb r0, fp, r1
         rsb r1, r4, fp           // (pstr 0x00000000) ">" r13
         (a, b) = compare (r5, r0)
         ip = lr
         movlo r0, r5
         (a, b) = compare (fp, r4)
         rsb r4, r0, r5
         [var_1ch] = r4
         movhs ip, 0
         r1 += ip                 // (pstr 0x00000000) ">" r13
         ip = [var_8h]
         ip += r0
         [var_8h] = ip
         ip = r1 + r0
         (a, b) = compare (lr, ip)
         blo 0x385f8              // unlikely

         goto loc_0x00038310;
    loc_0x00038310: // orphan
         r6 = [var_18h]
         rsb r1, fp, r1
         r8 = arg_4h
         sl = fp + r1
         ip = r6 + fp
         r5 = r8 + r1
         lr = ip + 1
         r7 = ip + r0
         (a, b) = compare (sl, r8)
         cmplt fp, r5
         rsb r4, lr, r7
         r6 += sl
         [var_54h] = lr
         sl = r6 | ip
         lr = r4 + 1
         movge r5, 1
         movlt r5, 0
         (a, b) = compare (lr, 9)
         movls r5, 0
         andhi r5, r5, 1
         (a, b) = compare (sl, 3)
         andeq r5, r5, 1
         [var_38h] = lr
         movne r5, 0
         (a, b) = compare (r5, 0)
         r0 += fp                 // (pstr 0x00000000) ">" r13
         je 0x38938               // likely

         goto loc_0x0003837c;
    loc_0x0003837c: // orphan
         r4 -= 3
         r6 -= 4
         r8 = ip
         r5 = 0
         r4 = r4 >> 2
         lr = r4 + 1
         r4 = lr << 2

    loc_0x00038398: // orphan
         // CODE XREF from fcn.00037adc @ 0x383a8(x)
         r5 += 1
         sl = [r6 + 4]!
         [r8] + 4 = sl
         blo 0x38398              // unlikely

         goto loc_0x000383ac;
    loc_0x000383ac: // orphan
         r5 = [var_38h]
         (a, b) = compare (r5, r4)
         r4 = ip + r4
         je 0x383e8               // likely

         goto loc_0x000383bc;
    loc_0x000383bc: // orphan
         r6 = (byte) [r4 + r1]
         r5 = r4 + 1
         (a, b) = compare (r7, r5)
         strb r6, [ip, lr, lsl 2]
         je 0x383e8               // unlikely

         goto loc_0x000383d0;
    loc_0x000383d0: // orphan
         lr = (byte) [r5 + r1]
         ip = r4 + 2
         (a, b) = compare (r7, ip)
         [r4 + 1] = (byte) lr
         ldrbne r1, [ip, r1]
         strbne r1, [r4, 2]

    loc_0x000383e8: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383b8(x), 0x383cc(x), 0x3895c(x)
         fp = r0
         
         goto loc_0x000383f0;
    loc_0x000383f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37edc(x)
         ip = [var_8h]
         sub r0, r0, r0, lsr 5
         r4 = [var_20h]
         rsb r2, r1, r2
         rsb r3, r1, r3
         orrs ip, ip, r4
         ip = [var_ch]            // "@"
         [ip + lr] = (half) r0
         je 0x386e4               // unlikely

         goto loc_0x00038414;
    loc_0x00038414: // orphan
         (a, b) = compare (r2, 0x1000000)
         r4 = lr + 0x18
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [ip + r4]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38634              // likely

         goto loc_0x00038440;
    loc_0x00038440: // orphan
         r2 = [var_4h]
         rsb ip, r0, 0x800
         (a, b) = compare (r1, 0x1000000)
         r2 += 0xf
         add r0, r0, ip, lsr 5
         ip = [var_ch]
         lsllo r1, r1, 8
         add r2, r6, r2, lsl 4
         [ip + r4] = (half) r0
         r0 = r2 << 1
         ldrblo r2, [sb]
         addlo sb, sb, 1
         ip = (word) [ip + r0]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb
         r2 = ip * r2
         (a, b) = compare (r2, r3)
         bls 0x386f0              // likely

         goto loc_0x00038488;
    loc_0x00038488: // orphan
         rsb lr, ip, 0x800
         r5 = [var_10h]
         r6 = [var_18h]
         add ip, ip, lr, lsr 5
         lr = [var_ch]            // "@"
         r4 = [var_24h]
         (a, b) = compare (fp, r5)
         rsb r1, r5, fp           // r13
         r1 = r6 + r1
         movhs r4, 0
         [lr + r0] = (half) ip
         r0 = [var_4h]
         r1 = (byte) [r1 + r4]
         r4 = r6
         (a, b) = compare (r0, 7)
         r0 = [var_8h]
         [r6 + fp] = (byte) r1
         r0 += 1
         fp += 1
         [var_8h] = r0
         movlo r0, 9
         movhs r0, 0xb
         [var_4h] = r0
         
         goto loc_0x000384e8;
    loc_0x000384e8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c80(x)
         r2 = [var_48h]
         r6 = 0x100
         r0 = [var_24h]
         [var_38h] = fp
         lr = [r2 + 0x14]
         r2 = [var_10h]
         rsb ip, r2, fp           // r13
         (a, b) = compare (fp, r2)
         r2 = r1
         r1 = lr + ip
         movhs r0, 0
         ip = 1
         r7 = (byte) [r1 + r0]

    loc_0x0003851c: // orphan
         // CODE XREF from fcn.00037adc @ 0x38594(x)
         r7 = r7 << 1
         r1 = ip + r6
         r5 = r6 & r7
         (a, b) = compare (r2, 0x1000000)
         r1 += r5
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         fp = ip << 1
         r1 = r1 << 1
         addlo sb, sb, 1
         r8 = r4 + r1
         orrlo r3, r0, r3, lsl 8
         r1 = (word) [r4 + r1]
         r0 = r2 >> 0xb
         rsb sl, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5
         add r1, r1, sl, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         lslhi ip, ip, 1
         addls ip, fp, 1
         uxthhi lr, r1
         bichi r6, r6, r5
         movhi r2, r0
         rsbls r3, r0, r3
         andls r6, r6, r5
         (a, b) = compare (ip, 0xff)
         [r8] = (half) lr
         bls 0x3851c              // unlikely

         goto loc_0x00038598;
    loc_0x00038598: // orphan
         fp = [var_38h]           // r13
         
         goto loc_0x000385a0;
    loc_0x000385a0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37f28(x)
         rsb r1, r0, r1
         rsb r3, r0, r3
         (a, b) = compare (r1, 0x1000000)
         sub ip, ip, ip, lsr 5
         lsllo r1, r1, 8
         [r2] = (half) ip
         ldrblo r0, [sb]
         addlo sb, sb, 1
         ip = (word) [r2 + 2]
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x386b8              // likely

         goto loc_0x000385d8;
    loc_0x000385d8: // orphan
         rsb r1, ip, 0x800
         add r6, r2, r6, lsl 4
         r6 += 0x104
         sl = 0
         add ip, ip, r1, lsr 5
         r8 = 8
         [r2 + 2] = (half) ip
         
         goto loc_0x000385f8;
    loc_0x000385f8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3830c(x)
         r5 = lr
         lr = [var_18h]
         ip = lr + fp
         fp += r0                 // r13
         r0 = lr
         lr += fp
         r4 = r0

    loc_0x00038614: // orphan
         // CODE XREF from fcn.00037adc @ 0x3862c(x)
         r0 = (byte) [r4 + r1]
         r1 += 1
         (a, b) = compare (r5, r1)
         [ip] + 1 = (byte) r0
         moveq r1, 0
         (a, b) = compare (ip, lr)
         bne 0x38614              // likely

         goto loc_0x00038630;
    loc_0x00038630: // orphan
         
         goto loc_0x00038634;
    loc_0x00038634: // orphan
         // CODE XREF from fcn.00037adc @ 0x3843c(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         ip = lr + 0x30
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + r4] = (half) r0
         r0 = (word) [r1 + ip]
         ldrblo r1, [sb]
         addlo sb, sb, 1 // DATA XREFS from fcn.0007cae4 @ 0x7cc20(r), 0x7cc3c(r), 0x7cc48(r), 0x7cc4c(r), 0x7cc64(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38708              // likely

         goto loc_0x00038674;
    loc_0x00038674: // orphan
         rsb r2, r0, 0x800
         lr = [var_10h]
         r4 = [var_34h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_34h] = lr
         [var_10h] = r4
         [r0 + ip] = (half) r2

    loc_0x00038694: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38704(x), 0x38770(x), 0x38994(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 7)
         r2 = [var_ch]            // "@"
         r2 += 0xa60
         movlo r0, 8
         movhs r0, 0xb
         r2 += 8
         [var_4h] = r0
         
         goto loc_0x000386b8;
    loc_0x000386b8: // orphan
         // CODE XREF from fcn.00037adc @ 0x385d4(x)
         rsb r3, r0, r3
         sub ip, ip, ip, lsr 5
         rsb r0, r0, r1
         r6 = r2 + 0x204
         sl = ~0xef
         [r2 + 2] = (half) ip
         r8 = 0x100
         
         goto loc_0x000386d8;
    loc_0x000386d8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3827c(x)
         r0 = [var_20h]
         (a, b) = compare (r0, ip)
         bhi 0x3828c              // likely

         return r0;
    loc_0x000386e4: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38288(x), 0x38410(x)
         r0 = 1
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x000386f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x38484(x)
         lr = [var_ch]            // "@"
         sub ip, ip, ip, lsr 5
         rsb r1, r2, r1
         rsb r3, r2, r3
         [lr + r0] = (half) ip
         
         goto loc_0x00038708;
    loc_0x00038708: // orphan
         // CODE XREF from fcn.00037adc @ 0x38670(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]
         (a, b) = compare (r2, 0x1000000)
         lr += 0x48
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + ip] = (half) r0
         r0 = (word) [r1 + lr]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38960              // likely

         goto loc_0x00038748;
    loc_0x00038748: // orphan
         r4 = [var_3ch]           // fcn.000bbf24
         rsb r2, r0, 0x800
         ip = [var_10h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_10h] = r4
         r4 = [var_34h]
         [r0 + lr] = (half) r2
         [var_34h] = ip
         [var_3ch] = r4
         
         goto loc_0x00038774;
    loc_0x00038774: // orphan
         // CODE XREF from fcn.00037adc @ 0x381d4(x)
         r1 -= 5

    loc_0x00038778: // orphan
         // CODE XREF from fcn.00037adc @ 0x387ac(x)
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r2 = r2 >> 1
         orrlo r3, r0, r3, lsl 8
         r1 -= 1
         rsb r3, r2, r3
         r0 = r3 >> 0x1f
         add ip, r0, ip, lsl 1
         r0 &= r2
         ip += 1
         r3 = r0 + r3
         bne 0x38778              // likely

         goto loc_0x000387b0;
    loc_0x000387b0: // orphan
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ip = ip << 4
         r0 = r1 + 0x640
         r1 = sb
         r0 += 6
         ldrblo r1, [sb]
         lr = (word) [r0]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb           // 0x6c20 // "hatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r4, lr, 0x800
         subls lr, lr, lr, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r2
         addhi lr, lr, r4, lsr 5
         uxthls lr, lr
         orrls ip, ip, 1
         movls r2, 6
         uxthhi lr, lr
         [r0] = (half) lr
         r0 = [var_58h]
         movhi r2, 4
         (a, b) = compare (r1, 0x1000000)
         r4 = r0 + r2
         lsllo r1, r1, 8
         lr = (word) [r0 + r2]
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb           // 0x6c20 // "hatEv"
         r0 = lr * r0             // 0x6c20 // "hatEv"
         (a, b) = compare (r0, r3)
         rsbhi r1, lr, 0x800
         subls lr, lr, lr, lsr 5
         addls r2, r2, 1
         rsbls r3, r0, r3
         addhi lr, lr, r1, lsr 5
         rsbls r0, r0, r1
         r1 = [var_58h]
         r2 = r2 << 1
         orrls ip, ip, 2
         uxthhi lr, lr
         uxthls lr, lr
         (a, b) = compare (r0, 0x1000000)
         [r4] = (half) lr
         lsllo r0, r0, 8
         lr = (word) [r1 + r2]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r0, lr, 0x800
         addls r4, r2, 1
         subls lr, lr, lr, lsr 5
         movhi r4, r2
         addhi r0, lr, r0, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r0
         uxthls r0, lr
         lr = r4 << 1
         r4 = [var_58h]
         uxthhi r0, r0
         orrls ip, ip, 4
         (a, b) = compare (r1, 0x1000000)
         lsllo r1, r1, 8
         [r4 + r2] = (half) r0
         ldrblo r2, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r2 = r0 * r2
         (a, b) = compare (r2, r3)
         rsbhi r1, r0, 0x800
         rsbls r3, r2, r3
         orrls ip, ip, 8
         rsbls r2, r2, r1
         addhi r0, r0, r1, lsr 5
         r1 = [var_58h]
         subls r0, r0, r0, lsr 5
         if (ip != 1)
         uxth r0, r0
         [r1 + lr] = (half) r0
         bne 0x38270              // likely

         goto loc_0x00038918;
    loc_0x00038918: // orphan
         r1 = [var_1ch]
         r0 = [var_4h]
         r1 += 0x110
         r0 -= 0xc
         r1 += 2
         [var_4h] = r0
         [var_1ch] = r1
         
         goto loc_0x00038938;
    loc_0x00038938: // orphan
         // CODE XREF from fcn.00037adc @ 0x38378(x)
         r1 = ip + r1
         lr = [var_54h]
         
         goto loc_0x00038944;
    loc_0x00038944: // orphan
         // CODE XREF from fcn.00037adc @ 0x38958(x)
         lr += 1

    loc_0x00038948: // orphan
         // CODE XREF from fcn.00037adc @ 0x38940(x)
         r4 = (byte) [r1] + 1
         (a, b) = compare (lr, r7)
         [ip] = (byte) r4
         ip = lr
         bne 0x38944              // unlikely

         goto loc_0x0003895c;
    loc_0x0003895c: // orphan
         
         goto loc_0x00038960;
    loc_0x00038960: // orphan
         // CODE XREF from fcn.00037adc @ 0x38744(x)
         rsb r3, r1, r3
         rsb r1, r1, r2
         r2 = [var_4ch]           // fcn.000bbf24
         sub r0, r0, r0, lsr 5
         ip = [var_10h]           // fcn.000bbf24
         [var_10h] = r2
         r2 = [var_3ch]
         [var_4ch] = r2
         r2 = [var_34h]           // fcn.000bbf24
         [var_34h] = ip
         [var_3ch] = r2
         r2 = [var_ch]            // "@"
         [r2 + lr] = (half) r0
         
         goto loc_0x00038998;
    loc_0x00038998: // orphan
         // CODE XREF from fcn.00037adc @ 0x37b1c(x)
         r1 = [r3 + 0x2c]
         r2 = [var_5ch]
         r3 = [r3 + 0xc]
         rsb r2, fp, r2
         rsb r3, r1, r3
         (a, b) = compare (r3, r2)
         strlo r1, [sp, 8]
         addlo r3, fp, r3
         strhs r1, [sp, 8]
         ldrhs r3, [sp, 0x5c]
         strlo r3, [sp, 0x14]
         strhs r3, [sp, 0x14]
         
}


========================================================
// Function at 0x0x3751c
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.0003751c (int32_t arg1, int32_t arg2, int32_t arg3) {
        // CALL XREFS from fcn.000389f8 @ 0x38d28(x), 0x38ff8(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r6 = ~0
        r3 = [r0 + 8] // arg1
        r2 = r1 + r2  // arg3
        r5 = [r0 + 0x2c] // arg1
        r8 = [r0 + 0x34] // arg1
        bic r6, r5, r6, lsl r3
        lr = [r0 + 0x1c] // arg1 // elf_phdr
        r4 = [r0 + 0x10] // arg1
        add r3, r6, r8, lsl 4
        (a, b) = compare (lr, 0x1000000)
        ip = [r0 + 0x20] // arg1 // elf_shdr
        r3 = r3 << 1
        r3 = (word) [r4 + r3]
        bhs 0x37570   // unlikely
        goto loc_0x00037558;
    loc_0x00037570:
        // CODE XREF from fcn.0003751c @ 0x37554(x)
        r7 = lr >> 0xb
        r3 *= r7
        (a, b) = compare (ip, r3)
        bhs 0x37650   // likely
        goto loc_0x00037580;
    loc_0x00037650:
        // CODE XREF from fcn.0003751c @ 0x3757c(x)
        r0 = r8 + 0xc0
        rsb r7, r3, lr
        rsb ip, r3, ip
        (a, b) = compare (r7, 0x1000000)
        r3 = r0 << 1
        r0 = (word) [r4 + r3]
        bhs 0x37684   // unlikely
        goto loc_0x0003766c;
    loc_0x00037684:
        // CODE XREF from fcn.0003751c @ 0x37668(x)
        lr = r7 >> 0xb
        r5 = r0 * lr
        (a, b) = compare (ip, r5)
        blo 0x37758   // unlikely
        goto loc_0x00037694;
    loc_0x00037758:
        // CODE XREF from fcn.0003751c @ 0x37690(x)
        r7 = r4 + 0x660
        r0 = 2
        r7 += 4
        sl = 0
        
    loc_0x00037768:
        // CODE XREF from fcn.0003751c @ 0x37a78(x)
        (a, b) = compare (r5, 0x1000000)
        r3 = (word) [r7] // "ecompFinal"
        bhs 0x3778c   // unlikely
        goto loc_0x00037774;
        goto loc_0x000376ac;
        goto loc_0x00037674;
        goto loc_0x00037594;
        goto loc_0x00037560;
        return r0;
    loc_0x00037560:
        r7 = (byte) [r1] // arg2
        lr = lr << 8
        r1 += 1       // arg2
        ip = r7 | ip
        break;
    loc_0x00037580: // orphan
         lr = [r0 + 0x30]         // arg1
         r4 += 0xe60
         r4 += 0xc
         orrs lr, r5, lr
         je 0x375d0               // unlikely

         goto loc_0x00037594;
    loc_0x00037594: // orphan
         lr = [r0 + 0x24]         // arg1
         r7 = ~0
         r6 = [r0 + 4]            // arg1
         (a, b) = compare (lr, 0)
         bic r5, r5, r7, lsl r6
         r7 = [r0 + 0x14]         // arg1
         ldreq lr, [r0, 0x28]     // arg1
         r6 = [r0]                // arg1
         lr -= 1
         r5 = r5 << r6
         rsb r6, r6, 8
         lr = (byte) [r7 + lr]
         add r5, r5, lr, asr r6
         add r5, r5, r5, lsl 1
         add r4, r4, r5, lsl 9

    loc_0x000375d0: // orphan
         // CODE XREF from fcn.0003751c @ 0x37590(x)
         (a, b) = compare (r8, 6)
         bhi 0x376bc              // unlikely

         goto loc_0x000375d8;
    loc_0x000375d8: // orphan
         lr = 1

    loc_0x000375dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x37628(x)
         r5 = lr << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + 1
         r0 = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x37608              // unlikely

         goto loc_0x000375f0;
    loc_0x000375f0: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000375fc;
    loc_0x000375fc: // orphan
         r6 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r6 | ip

    loc_0x00037608: // orphan
         // CODE XREF from fcn.0003751c @ 0x375ec(x)
         r6 = r3 >> 0xb
         r0 *= r6
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         rsbhs ip, r0, ip
         movlo lr, r5
         movlo r3, r0
         (a, b) = compare (lr, 0xff)
         bls 0x375dc              // likely

         goto loc_0x0003762c;
    loc_0x0003762c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37754(x)
         r0 = 1

    loc_0x00037630: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37804(x), 0x37880(x), 0x378e0(x)
         (a, b) = compare (r3, 0x1000000)
         movlo r3, 1
         movhs r3, 0
         (a, b) = compare (r1, r2) // arg3
         movlo r3, 0
         (a, b) = compare (r3, 0)
         movne r0, 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x0003766c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037674;
    loc_0x00037674: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037694: // orphan
         rsb r7, r5, r7
         r3 = r4 + r3
         (a, b) = compare (r7, 0x1000000)
         rsb r0, r5, ip
         ip = (word) [r3 + 0x18]  // section..text
         bhs 0x37918              // unlikely

         goto loc_0x000376ac;
    loc_0x000376ac: // orphan
         (a, b) = compare (r1, r2) // arg3
         blo 0x37908              // unlikely

         return r0;
    loc_0x000376b4: // orphan
         // XREFS: CODE 0x0003755c  CODE 0x000375f8  CODE 0x00037670   // XREFS: CODE 0x00037714  CODE 0x00037778  CODE 0x000377cc   // XREFS: CODE 0x00037844  CODE 0x00037904  CODE 0x00037944   // XREFS: CODE 0x000379a4  CODE 0x00037a04  CODE 0x00037a40   // XREFS: CODE 0x00037a9c  
         r0 = 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc) // 0x178000 // r13

    loc_0x000376bc: // orphan
         // CODE XREF from fcn.0003751c @ 0x375d4(x)
         lr = [r0 + 0x24]         // arg1
         r6 = [r0 + 0x38]         // arg1 // fcn.000bbf24
         r5 = [r0 + 0x14]         // arg1
         (a, b) = compare (lr, r6)
         rsb r6, r6, lr
         lr = 1
         ldrlo r0, [r0, 0x28]     // arg1
         movhs r0, 0
         r0 = r5 + r0             // arg1
         r5 = 0x100
         r6 = (byte) [r0 + r6]    // arg1

    loc_0x000376e8: // orphan
         // CODE XREF from fcn.0003751c @ 0x37750(x)
         r6 = r6 << 1
         r0 = r5 + lr
         r7 = r5 & r6
         (a, b) = compare (r3, 0x1000000)
         r0 += r7
         r8 = lr << 1
         r0 = r0 << 1
         r0 = (word) [r4 + r0]    // "ecompFinal"
         bhs 0x37724              // unlikely

         goto loc_0x0003770c;
    loc_0x0003770c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037718;
    loc_0x00037718: // orphan
         sb = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = sb | ip

    loc_0x00037724: // orphan
         // CODE XREF from fcn.0003751c @ 0x37708(x)
         sb = r3 >> 0xb
         r0 *= sb
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         lsllo lr, lr, 1
         addhs lr, r8, 1
         biclo r5, r5, r7
         movlo r3, r0
         rsbhs ip, r0, ip
         andhs r5, r5, r7
         (a, b) = compare (lr, 0xff)
         bls 0x376e8              // likely

         goto loc_0x00037754;
    loc_0x00037754: // orphan
         
         goto loc_0x00037758;
    loc_0x00037774: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x0003777c;
    loc_0x0003777c: // orphan
         lr = (byte) [r1]         // arg2
         r5 = r5 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x0003778c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37770(x)
         lr = r5 >> 0xb
         r3 *= lr
         (a, b) = compare (ip, r3)
         bhs 0x3798c              // likely

         goto loc_0x0003779c;
    loc_0x0003779c: // orphan
         add r7, r7, r6, lsl 4
         sb = 0
         r7 += 4
         r8 = 8

    loc_0x000377ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x379e8(x)
         r5 = 1

    loc_0x000377b0: // orphan
         // CODE XREF from fcn.0003751c @ 0x377fc(x)
         r6 = r5 << 1
         (a, b) = compare (r3, 0x1000000)
         r5 = r6 + 1
         lr = (word) [r7 + r6]    // "ecompFinal"
         bhs 0x377dc              // unlikely

         goto loc_0x000377c4;
    loc_0x000377c4: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000377d0;
    loc_0x000377d0: // orphan
         fp = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = fp | ip

    loc_0x000377dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x377c0(x)
         fp = r3 >> 0xb
         lr *= fp
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r5, r6
         movlo r3, lr
         (a, b) = compare (r5, r8)
         blo 0x377b0              // unlikely

         goto loc_0x00037800;
    loc_0x00037800: // orphan
         (a, b) = compare (sl, 3)
         bhi 0x37630              // unlikely

         goto loc_0x00037808;
    loc_0x00037808: // orphan
         rsb r8, r8, sb
         r6 = 1
         r5 = r8 + r5
         (a, b) = compare (r5, 3)
         lslls r5, r5, 7
         addls r8, r5, 0x360
         movhi r8, 0x4e0

    loc_0x00037824: // orphan
         // CODE XREF from fcn.0003751c @ 0x37874(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + r8
         r6 = r5 + 1
         lr = (word) [r4 + lr]    // "ecompFinal"
         bhs 0x37854              // unlikely

         goto loc_0x0003783c;
    loc_0x0003783c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037848;
    loc_0x00037848: // orphan
         r7 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r7 | ip

    loc_0x00037854: // orphan
         // CODE XREF from fcn.0003751c @ 0x37838(x)
         r7 = r3 >> 0xb
         lr *= r7
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         (a, b) = compare (r6, 0x3f) // '?'
         bls 0x37824              // likely

         goto loc_0x00037878;
    loc_0x00037878: // orphan
         lr = r6 - 0x40
         (a, b) = compare (lr, 3)
         bls 0x37630              // unlikely

         goto loc_0x00037884;
    loc_0x00037884: // orphan
         (a, b) = compare (lr, 0xd)
         r5 = lr >> 1
         r7 = r5 - 1
         bhi 0x37a88              // unlikely

         goto loc_0x00037894;
    loc_0x00037894: // orphan
         r5 = lr & 1
         rsb lr, r6, 0x2ec
         r5 |= 2
         lr += 3
         add lr, lr, r5, lsl r7
         add r4, r4, lr, lsl 1

    loc_0x000378ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ad8(x)
         r6 = 1
         
         goto loc_0x000378b4;
    loc_0x000378b4: // orphan
         // CODE XREF from fcn.0003751c @ 0x37900(x)
         r8 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r8 | ip

    loc_0x000378c0: // orphan
         // CODE XREF from fcn.0003751c @ 0x378f4(x)
         r8 = r3 >> 0xb
         lr *= r8
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         r7 -= 1
         je 0x37630               // unlikely

         goto loc_0x000378e4;
    loc_0x000378e4: // orphan
         // CODE XREF from fcn.0003751c @ 0x378b0(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         r6 = r5 + 1
         lr = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x378c0              // unlikely

         goto loc_0x000378f8;
    loc_0x000378f8: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         blo 0x378b4              // unlikely

         goto loc_0x00037904;
    loc_0x00037904: // orphan
         
         goto loc_0x00037908;
    loc_0x00037908: // orphan
         // CODE XREF from fcn.0003751c @ 0x376b0(x)
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         r0 = lr | r0

    loc_0x00037918: // orphan
         // CODE XREF from fcn.0003751c @ 0x376a8(x)
         lr = r7 >> 0xb
         lr = ip * lr
         (a, b) = compare (r0, lr)
         bhs 0x379ec              // likely

         goto loc_0x00037928;
    loc_0x00037928: // orphan
         r3 = r8 + 0xf
         (a, b) = compare (lr, 0x1000000)
         add r3, r6, r3, lsl 4
         r3 = r3 << 1
         r3 = (word) [r4 + r3]
         bhs 0x37958              // unlikely

         goto loc_0x00037940;
    loc_0x00037940: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037948;
    loc_0x00037948: // orphan
         ip = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         r0 = ip | r0

    loc_0x00037958: // orphan
         // CODE XREF from fcn.0003751c @ 0x3793c(x)
         ip = lr >> 0xb
         ip = r3 * ip
         (a, b) = compare (r0, ip)
         bhs 0x37a7c              // likely

         goto loc_0x00037968;
    loc_0x00037968: // orphan
         (a, b) = compare (ip, 0x1000000)
         movlo ip, 1
         movhs ip, 0
         (a, b) = compare (r1, r2) // arg3
         movlo ip, 0
         (a, b) = compare (ip, 0)
         movne r0, 0
         moveq r0, 3

         goto loc_0x0003798c;
    loc_0x0003798c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37798(x)
         rsb lr, r3, r5
         rsb ip, r3, ip
         (a, b) = compare (lr, 0x1000000)
         r3 = (word) [r7 + 2]
         bhs 0x379b8              // unlikely

         goto loc_0x000379a0;
    loc_0x000379a0: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x000379a8;
    loc_0x000379a8: // orphan
         r5 = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         ip = r5 | ip

    loc_0x000379b8: // orphan
         // CODE XREF from fcn.0003751c @ 0x3799c(x)
         r5 = lr >> 0xb
         r3 *= r5
         (a, b) = compare (ip, r3)
         addlo r6, r7, r6, lsl 4
         rsbhs ip, r3, ip
         movlo sb, 8
         addlo r7, r6, 0x104
         movlo r8, sb
         addhs r7, r7, 0x204
         rsbhs r3, r3, lr
         movhs sb, 0x10
         movhs r8, 0x100
         
         goto loc_0x000379ec;
    loc_0x000379ec: // orphan
         // CODE XREF from fcn.0003751c @ 0x37924(x)
         rsb r7, lr, r7
         rsb ip, lr, r0
         (a, b) = compare (r7, 0x1000000)
         r0 = (word) [r3 + 0x30]
         bhs 0x37a18              // unlikely

         goto loc_0x00037a00;
    loc_0x00037a00: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a08;
    loc_0x00037a08: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037a18: // orphan
         // CODE XREF from fcn.0003751c @ 0x379fc(x)
         lr = r7 >> 0xb
         r5 = r0 * lr
         (a, b) = compare (ip, r5)
         blo 0x37a68              // unlikely

         goto loc_0x00037a28;
    loc_0x00037a28: // orphan
         rsb r7, r5, r7
         rsb ip, r5, ip
         (a, b) = compare (r7, 0x1000000)
         r3 = (word) [r3 + 0x48]
         bhs 0x37a54              // unlikely

         goto loc_0x00037a3c;
    loc_0x00037a3c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a44;
    loc_0x00037a44: // orphan
         r0 = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = r0 | ip

    loc_0x00037a54: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a38(x)
         lr = r7 >> 0xb
         r5 = r3 * lr
         (a, b) = compare (ip, r5)
         rsbhs ip, r5, ip
         rsbhs r5, r5, r7

    loc_0x00037a68: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37a24(x), 0x37a84(x)
         r7 = r4 + 0xa60
         r0 = 3
         r7 += 8
         sl = 0xc
         
         goto loc_0x00037a7c;
    loc_0x00037a7c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37964(x)
         rsb r5, ip, lr
         rsb ip, ip, r0
         
         goto loc_0x00037a88;
    loc_0x00037a88: // orphan
         // CODE XREF from fcn.0003751c @ 0x37890(x)
         r5 -= 5

    loc_0x00037a8c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ac8(x)
         (a, b) = compare (r3, 0x1000000)
         bhs 0x37aac              // unlikely

         goto loc_0x00037a94;
    loc_0x00037a94: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037aa0;
    loc_0x00037aa0: // orphan
         lr = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037aac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a90(x)
         r3 = r3 >> 1
         r5 -= 1
         rsb lr, r3, ip
         lr = lr >> 0x1f
         lr -= 1
         lr &= r3
         rsb ip, lr, ip
         bne 0x37a8c              // likely

         goto loc_0x00037acc;
    loc_0x00037acc: // orphan
         r4 += 0x640
         r7 = 4
         r4 += r7
         
}


========================================================
// Function at 0x0x37c44
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.00037adc (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_1h, int32_t arg_4h) {
        // CALL XREFS from fcn.000389f8 @ 0x38d60(x), 0x38eec(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r3 = r0       // arg1
        sp -= 0x64
        sb = [r3 + 0x18] // section..text
        fp = [r3 + 0x24]
        [var_48h] = r0 // arg1
        r0 = [0x00037afc] // [0x389cc:4]=0x7d4e8
        [var_5ch] = r1 // arg2
        r0 = pc + r0
        [var_2ch] = r2 // arg3
        [var_44h] = r0
        
    loc_0x00037b08:
        // CODE XREF from fcn.00037adc @ 0x37e78(x)
        r3 = [var_48h]
        r3 = [r3 + 0x30]
        (a, b) = compare (r3, 0)
        [var_20h] = r3
        r3 = [var_48h]
        je 0x38998    // unlikely
        goto loc_0x00037b20;
        return r0;
    loc_0x00037b20:
        r3 = [r3 + 0x2c]
        [var_8h] = r3
        r3 = [var_5ch]
        [var_14h] = r3
        
    loc_0x00037b30:
        // CODE XREF from fcn.00037adc @ 0x389c8(x)
        r1 = [var_48h]
        lr = 1
        r3 = [r1 + 0x10]
        r2 = [r1 + 8]
        r0 = r3
        [var_ch] = r3
        r3 = 0
        [var_1ch] = r3
        r3 = [r1 + 4]
        r2 = lr << r2
        r2 -= 1
        [var_28h] = r2
        r3 = lr << r3
        r2 = [r1 + 0x44]
        r3 -= 1
        [var_50h] = r3
        r3 = [r1]
        ip = r0 + 0x640
        lr = [r1 + 0x34]
        r0 += 0xe60
        ip += 4
        r0 += 0xc
        [var_40h] = r3
        r3 = [r1 + 0x14]
        [var_4h] = lr
        [var_58h] = ip
        [var_18h] = r3
        r3 = [r1 + 0x28]
        lr = [r1 + 0x38] // fcn.000bbf24
        ip = [r1 + 0x3c] // fcn.000bbf24
        [var_30h] = r0
        [var_4ch] = r2
        r0 = [r1 + 0x40] // fcn.000bbf24
        [var_24h] = r3
        r2 = [r1 + 0x1c] // elf_phdr
        r3 = [r1 + 0x20] // elf_shdr
        [var_10h] = lr
        [var_34h] = ip
        [var_3ch] = r0
        return r0;
    loc_0x00037c10: // orphan
         r2 = [var_8h]
         lr = [var_20h]
         r4 = [var_30h]
         orrs r2, r2, lr
         rsb r2, ip, 0x800
         add ip, ip, r2, lsr 5
         r2 = [var_ch]
         [r2 + r0] = (half) ip
         je 0x37c78               // unlikely

         goto loc_0x00037c34;
    loc_0x00037c34: // orphan
         r0 = [var_50h]
         (a, b) = compare (fp, 0)
         r2 = [var_8h]
         ip = [var_18h]
         r2 &= r0
         r0 = [var_40h]
         r2 = r2 << r0
         ldreq r0, [sp, 0x24]
         subne r0, fp, 1
         subeq r0, r0, 1
         ip = (byte) [ip + r0]
         r0 = [var_40h]
         rsb r0, r0, 8
         add r2, r2, ip, asr r0
         r0 = [var_30h]
         add r2, r2, r2, lsl 1
         add r4, r0, r2, lsl 9

    loc_0x00037c78: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c30(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 6)
         bhi 0x384e8              // likely

         goto loc_0x00037c84;
    loc_0x00037c84: // orphan
         r2 = r1
         ip = 1

    loc_0x00037c8c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37ce8(x)
         (a, b) = compare (r2, 0x1000000)
         r5 = ip << 1
         lsllo r2, r2, 8
         ip = r5 + 1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r1 = (word) [r4 + r5]    // "ecompFinal"
         r6 = r4 + r5
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb r7, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt" // "rfaceShow"
         add r1, r1, r7, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         rsbls r3, r0, r3
         movhi ip, r5
         movhi r2, r0
         (a, b) = compare (ip, 0xff)
         [r6] = (half) lr
         bls 0x37c8c              // likely

         goto loc_0x00037cec;
    loc_0x00037cec: // orphan
         // CODE XREF from fcn.00037adc @ 0x3859c(x)
         r1 = [var_18h]
         r0 = [var_4h]
         [r1 + fp] = (byte) ip
         fp += 1
         r1 = [var_8h]
         r1 += 1
         [var_8h] = r1
         r1 = [var_44h]
         r1 = (byte) [r1 + r0]
         [var_4h] = r1

    loc_0x00037d14: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383ec(x), 0x384e4(x), 0x38630(x)
         r1 = [var_2ch]
         r0 = [var_14h]
         (a, b) = compare (r1, sb)
         cmphi r0, fp
         bhi 0x37bcc              // unlikely

         goto loc_0x00037d28;
    loc_0x00037d28: // orphan
         // CODE XREF from fcn.00037adc @ 0x38934(x)
         (a, b) = compare (r2, 0x1000000)
         r0 = [var_48h]
         ip = [var_8h]
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         [r0 + 0x1c] = r2
         orrlo r3, r1, r3, lsl 8
         [r0 + 0x20] = r3
         r3 = [var_1ch]
         r1 = [r0 + 0xc]
         [r0 + 0x2c] = ip
         [r0 + 0x48] = r3
         (a, b) = compare (r1, ip)
         r3 = [var_10h]           // fcn.000bbf24
         ip = [var_1ch]
         [r0 + 0x18] = sb
         [r0 + 0x38] = r3
         r3 = [var_34h]           // fcn.000bbf24
         [r0 + 0x24] = fp
         [r0 + 0x3c] = r3
         r3 = [var_3ch]           // fcn.000bbf24
         [r0 + 0x40] = r3
         r3 = [var_4ch]
         [r0 + 0x44] = r3
         r3 = [var_4h]
         [r0 + 0x34] = r3
         movls r3, r0
         strls r1, [r3, 0x30]
         r3 = ip - 1
         (a, b) = compare (r3, 0x110)
         movhi sl, ip
         bhi 0x37e50              // likely

         goto loc_0x00037dac;
    loc_0x00037dac: // orphan
         r0 = [var_48h]
         r3 = [var_5ch]
         rsb lr, fp, r3
         r3 = [r0 + 0x30]
         (a, b) = compare (lr, ip)
         r2 = [r0 + 0x14]
         r0 = [r0 + 0x28]
         movhs lr, ip
         (a, b) = compare (r3, 0)
         bne 0x37de8              // unlikely

         goto loc_0x00037dd4;
    loc_0x00037dd4: // orphan
         r3 = [var_8h]
         rsb r3, r3, r1
         (a, b) = compare (r3, lr)
         ldrls r3, [sp, 0x48]
         strls r1, [r3, 0x30]

    loc_0x00037de8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37dd0(x)
         r1 = [var_1ch]
         (a, b) = compare (lr, 0)
         r3 = [var_8h]
         rsb sl, lr, r1
         r1 = [var_48h]
         r3 = lr + r3
         [r1 + 0x48] = sl
         [r1 + 0x2c] = r3
         je 0x37e48               // likely

         goto loc_0x00037e0c;
    loc_0x00037e0c: // orphan
         ip = [var_10h]           // fcn.000bbf24
         lr += fp                 // r13
         r1 = r2 + fp             // r13

    loc_0x00037e18: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e3c(x)
         (a, b) = compare (ip, fp)
         rsb r3, ip, fp           // r13
         r3 = r2 + r3             // r13
         fp += 1
         movhi r4, r0
         movls r4, 0
         r3 = (byte) [r3 + r4]
         (a, b) = compare (fp, lr)
         [r1] + 1 = (byte) r3
         bne 0x37e18              // likely

         goto loc_0x00037e40;
    loc_0x00037e40: // orphan
         r3 = [var_48h]
         sl = [r3 + 0x48]

    loc_0x00037e48: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e08(x)
         r3 = [var_48h]
         [r3 + 0x24] = fp

    loc_0x00037e50: // orphan
         // CODE XREF from fcn.00037adc @ 0x37da8(x)
         r3 = [var_5ch]
         (a, b) = compare (r3, fp)
         bls 0x37e7c              // likely

         goto loc_0x00037e5c;
    loc_0x00037e5c: // orphan
         r3 = [var_48h]
         sb = [r3 + 0x18]
         r3 = [var_2ch]
         (a, b) = compare (r3, sb)
         bls 0x37e7c              // likely

         goto loc_0x00037e70;
    loc_0x00037e70: // orphan
         r3 = 0x111
         (a, b) = compare (sl, r3)
         bls 0x37b08              // likely

         return r0;
    loc_0x00037e7c: // orphan
         // CODE XREFS from fcn.00037adc @ 0x37e58(x), 0x37e6c(x)
         r3 = 0x112
         (a, b) = compare (sl, r3)
         ldrhi r2, [sp, 0x48]
         strhi r3, [r2, 0x48]
         r0 = 0
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00037e98: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c0c(x)
         lr = [var_4h]
         rsb r2, r1, r2
         r4 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lr += 0xc0
         rsb r3, r1, r3
         lsllo r2, r2, 8
         sub ip, ip, ip, lsr 5
         lr = lr << 1
         [r4 + r0] = (half) ip
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x383f0              // likely

         goto loc_0x00037ee0;
    loc_0x00037ee0: // orphan
         r4 = [var_ch]
         rsb ip, r0, 0x800
         r5 = [var_4h]
         r2 = r4 + 0x660
         add r0, r0, ip, lsr 5
         r2 += 4
         r5 += 0xc
         [r4 + lr] = (half) r0
         [var_4h] = r5

    loc_0x00037f04: // orphan
         // CODE XREF from fcn.00037adc @ 0x386b4(x)
         (a, b) = compare (r1, 0x1000000)
         ip = (word) [r2]         // "oSha1Final"
         lsllo r1, r1, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x385a0              // likely

         goto loc_0x00037f2c;
    loc_0x00037f2c: // orphan
         add r6, r2, r6, lsl 4
         rsb r1, ip, 0x800
         r6 += 4
         sl = ~7
         r8 = 8
         add ip, ip, r1, lsr 5
         [r2] = (half) ip

    loc_0x00037f48: // orphan
         // CODE XREFS from fcn.00037adc @ 0x385f4(x), 0x386d4(x)
         r5 = 1

    loc_0x00037f4c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fa8(x)
         (a, b) = compare (r0, 0x1000000)
         lr = r5 << 1
         lsllo r0, r0, 8
         r5 = lr + 1
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r2 = (word) [r6 + lr]
         r4 = r6 + lr // DATA XREF from sym.s3eGLGetInt @ 0x8adf0(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb
         rsb r7, r2, 0x800
         r1 = r2 * r1
         sub ip, r2, r2, lsr 5
         add r2, r2, r7, lsr 5
         uxth ip, ip
         (a, b) = compare (r1, r3)
         rsb r0, r1, r0
         uxthhi ip, r2
         rsbls r3, r1, r3
         movhi r5, lr
         movhi r0, r1
         (a, b) = compare (r5, r8)
         [r4] = (half) ip
         blo 0x37f4c              // unlikely

         goto loc_0x00037fac;
    loc_0x00037fac: // orphan
         r2 = [var_4h]
         lr = sb
         (a, b) = compare (r2, 0xb)
         r2 = r5 + sl
         [var_1ch] = r2
         r2 = r0
         bls 0x382bc              // unlikely

         goto loc_0x00037fc8;
    loc_0x00037fc8: // orphan
         r1 = [var_1ch]
         ip = [var_ch]            // "@" // DATA XREF from sym.s3eGLGetInt @ 0x8ad8c(r)
         (a, b) = compare (r1, 3)
         lslls r1, r1, 7
         addls r1, r1, 0x360      // (pstr 0x00000000) ">"
         movhi r1, 0x4e0
         (a, b) = compare (r0, 0x1000000)
         r1 = ip + r1
         lsllo r2, r0, 8
         ip = (word) [r1 + 2]
         r0 = r2 >> 0xb
         ldrblo lr, [sb]
         addlo sb, sb, 1
         r0 = ip * r0
         orrlo r3, lr, r3, lsl 8
         (a, b) = compare (r0, r3)
         rsbhi lr, ip, 0x800
         rsbls r3, r0, r3
         rsbls r0, r0, r2
         movhi r2, 4
         movls r2, 6
         addhi ip, ip, lr, lsr 5
         subls ip, ip, ip, lsr 5
         (a, b) = compare (r0, 0x1000000)
         lsllo r0, r0, 8
         lr = r1 + r2
         uxth ip, ip
         [r1 + 2] = (half) ip
         ldrblo ip, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, ip, r3, lsl 8
         ip = r0 >> 0xb
         ip = r4 * ip
         (a, b) = compare (ip, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, ip, r3
         addls r2, r2, 1          // (pstr 0x00000000) ">"
         addhi r0, r4, r0, lsr 5
         rsbls ip, ip, r0
         uxthls r0, r4
         r2 = r2 << 1
         uxthhi r0, r0
         (a, b) = compare (ip, 0x1000000)
         [lr] = (half) r0
         lsllo ip, ip, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, r0, r3, lsl 8
         r0 = ip >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi ip, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, r2, 1
         addhi ip, r4, ip, lsr 5
         rsbls r0, r0, ip
         movhi lr, r2
         uxthls ip, r4
         uxthhi ip, ip
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + r2] = (half) ip
         lsllo r0, r0, 8
         ldrblo r2, [sb]          // (pstr 0x00000000) ">"
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r2, r3
         addls ip, lr, 1          // (pstr 0x00005400) "rGetLocaltimeOffset"
         addhi r0, r4, r0, lsr 5
         rsbls r2, r2, r0
         movhi ip, lr
         uxthls r0, r4
         uxthhi r0, r0
         (a, b) = compare (r2, 0x1000000)
         ip = ip << 1
         lsllo r2, r2, 8
         [r1 + lr] = (half) r0
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + ip]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi r2, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, ip, 1
         addhi r2, r4, r2, lsr 5
         rsbls r0, r0, r2
         movhi lr, ip
         uxthls r2, r4
         uxthhi r2, r2
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + ip] = (half) r2
         lsllo r0, r0, 8
         ldrblo r2, [sb]
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi ip, r4, 0x800
         rsbls r3, r2, r3
         rsbls r2, r2, r0
         addls r0, lr, 1
         addhi r4, r4, ip, lsr 5
         movhi r0, lr
         ip = r0 - 0x40
         subls r4, r4, r4, lsr 5
         (a, b) = compare (ip, 3)
         uxth r4, r4
         [r1 + lr] = (half) r4
         bls 0x38270              // unlikely

         goto loc_0x000381c0;
    loc_0x000381c0: // orphan
         (a, b) = compare (ip, 0xd)
         r1 = ip >> 1
         ip &= 1
         r6 = r1 - 1
         ip |= 2
         bhi 0x38774              // unlikely

         goto loc_0x000381d8;
    loc_0x000381d8: // orphan
         rsb r0, r0, 0x2ec
         ip = ip << r6
         r0 += 3
         r7 = [var_ch]            // "@"
         r8 = 1
         r0 += ip
         sl = r8
         [var_4ch] = fp
         r1 = r0 << r8
         [var_38h] = r1

    loc_0x00038200: // orphan
         // CODE XREF from fcn.00037adc @ 0x38268(x)
         r1 = [var_38h]
         (a, b) = compare (r2, 0x1000000)
         r4 = r8 << 1
         lsllo r2, r2, 8
         r5 = r4 + r1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r8 = r4 + 1
         r1 = (word) [r7 + r5]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb fp, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt"
         add r1, r1, fp, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         orrls ip, ip, sl
         movhi r8, r4
         movhi r2, r0
         rsbls r3, r0, r3
         r6 -= 1
         [r7 + r5] = (half) lr
         sl = sl << 1
         bne 0x38200              // likely

         goto loc_0x0003826c;
    loc_0x0003826c: // orphan
         fp = [var_4ch]           // (pstr 0x00000000) ">" r13

    loc_0x00038270: // orphan
         // CODE XREFS from fcn.00037adc @ 0x381bc(x), 0x38914(x)
         r1 = [var_20h]           // (cstr 0x00000000) ">"
         (a, b) = compare (r1, 0)
         r1 = ip + 1
         bne 0x386d8              // likely

         goto loc_0x00038280;
    loc_0x00038280: // orphan
         r0 = [var_8h]
         (a, b) = compare (r0, ip)
         bls 0x386e4              // unlikely

         goto loc_0x0003828c;
    loc_0x0003828c: // orphan
         // CODE XREF from fcn.00037adc @ 0x386e0(x)
         r0 = [var_4h]
         (a, b) = compare (r0, 0x12)
         r0 = [var_3ch]           // fcn.000bbf24
         [var_4ch] = r0
         movhi r0, 0xa            // (pstr 0x00000000) ">"
         movls r0, 7
         [var_4h] = r0
         r0 = [var_34h]           // fcn.000bbf24
         [var_3ch] = r0
         r0 = [var_10h]           // fcn.000bbf24
         [var_10h] = r1
         [var_34h] = r0

    loc_0x000382bc: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fc4(x)
         r1 = [var_1ch]
         r4 = [var_10h]
         r5 = r1 + 2
         r1 = [var_14h]
         lr = [var_24h]
         rsb r0, fp, r1
         rsb r1, r4, fp           // (pstr 0x00000000) ">" r13
         (a, b) = compare (r5, r0)
         ip = lr
         movlo r0, r5
         (a, b) = compare (fp, r4)
         rsb r4, r0, r5
         [var_1ch] = r4
         movhs ip, 0
         r1 += ip                 // (pstr 0x00000000) ">" r13
         ip = [var_8h]
         ip += r0
         [var_8h] = ip
         ip = r1 + r0
         (a, b) = compare (lr, ip)
         blo 0x385f8              // unlikely

         goto loc_0x00038310;
    loc_0x00038310: // orphan
         r6 = [var_18h]
         rsb r1, fp, r1
         r8 = arg_4h
         sl = fp + r1
         ip = r6 + fp
         r5 = r8 + r1
         lr = ip + 1
         r7 = ip + r0
         (a, b) = compare (sl, r8)
         cmplt fp, r5
         rsb r4, lr, r7
         r6 += sl
         [var_54h] = lr
         sl = r6 | ip
         lr = r4 + 1
         movge r5, 1
         movlt r5, 0
         (a, b) = compare (lr, 9)
         movls r5, 0
         andhi r5, r5, 1
         (a, b) = compare (sl, 3)
         andeq r5, r5, 1
         [var_38h] = lr
         movne r5, 0
         (a, b) = compare (r5, 0)
         r0 += fp                 // (pstr 0x00000000) ">" r13
         je 0x38938               // likely

         goto loc_0x0003837c;
    loc_0x0003837c: // orphan
         r4 -= 3
         r6 -= 4
         r8 = ip
         r5 = 0
         r4 = r4 >> 2
         lr = r4 + 1
         r4 = lr << 2

    loc_0x00038398: // orphan
         // CODE XREF from fcn.00037adc @ 0x383a8(x)
         r5 += 1
         sl = [r6 + 4]!
         [r8] + 4 = sl
         blo 0x38398              // unlikely

         goto loc_0x000383ac;
    loc_0x000383ac: // orphan
         r5 = [var_38h]
         (a, b) = compare (r5, r4)
         r4 = ip + r4
         je 0x383e8               // likely

         goto loc_0x000383bc;
    loc_0x000383bc: // orphan
         r6 = (byte) [r4 + r1]
         r5 = r4 + 1
         (a, b) = compare (r7, r5)
         strb r6, [ip, lr, lsl 2]
         je 0x383e8               // unlikely

         goto loc_0x000383d0;
    loc_0x000383d0: // orphan
         lr = (byte) [r5 + r1]
         ip = r4 + 2
         (a, b) = compare (r7, ip)
         [r4 + 1] = (byte) lr
         ldrbne r1, [ip, r1]
         strbne r1, [r4, 2]

    loc_0x000383e8: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383b8(x), 0x383cc(x), 0x3895c(x)
         fp = r0
         
         goto loc_0x000383f0;
    loc_0x000383f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37edc(x)
         ip = [var_8h]
         sub r0, r0, r0, lsr 5
         r4 = [var_20h]
         rsb r2, r1, r2
         rsb r3, r1, r3
         orrs ip, ip, r4
         ip = [var_ch]            // "@"
         [ip + lr] = (half) r0
         je 0x386e4               // unlikely

         goto loc_0x00038414;
    loc_0x00038414: // orphan
         (a, b) = compare (r2, 0x1000000)
         r4 = lr + 0x18
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [ip + r4]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38634              // likely

         goto loc_0x00038440;
    loc_0x00038440: // orphan
         r2 = [var_4h]
         rsb ip, r0, 0x800
         (a, b) = compare (r1, 0x1000000)
         r2 += 0xf
         add r0, r0, ip, lsr 5
         ip = [var_ch]
         lsllo r1, r1, 8
         add r2, r6, r2, lsl 4
         [ip + r4] = (half) r0
         r0 = r2 << 1
         ldrblo r2, [sb]
         addlo sb, sb, 1
         ip = (word) [ip + r0]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb
         r2 = ip * r2
         (a, b) = compare (r2, r3)
         bls 0x386f0              // likely

         goto loc_0x00038488;
    loc_0x00038488: // orphan
         rsb lr, ip, 0x800
         r5 = [var_10h]
         r6 = [var_18h]
         add ip, ip, lr, lsr 5
         lr = [var_ch]            // "@"
         r4 = [var_24h]
         (a, b) = compare (fp, r5)
         rsb r1, r5, fp           // r13
         r1 = r6 + r1
         movhs r4, 0
         [lr + r0] = (half) ip
         r0 = [var_4h]
         r1 = (byte) [r1 + r4]
         r4 = r6
         (a, b) = compare (r0, 7)
         r0 = [var_8h]
         [r6 + fp] = (byte) r1
         r0 += 1
         fp += 1
         [var_8h] = r0
         movlo r0, 9
         movhs r0, 0xb
         [var_4h] = r0
         
         goto loc_0x000384e8;
    loc_0x000384e8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c80(x)
         r2 = [var_48h]
         r6 = 0x100
         r0 = [var_24h]
         [var_38h] = fp
         lr = [r2 + 0x14]
         r2 = [var_10h]
         rsb ip, r2, fp           // r13
         (a, b) = compare (fp, r2)
         r2 = r1
         r1 = lr + ip
         movhs r0, 0
         ip = 1
         r7 = (byte) [r1 + r0]

    loc_0x0003851c: // orphan
         // CODE XREF from fcn.00037adc @ 0x38594(x)
         r7 = r7 << 1
         r1 = ip + r6
         r5 = r6 & r7
         (a, b) = compare (r2, 0x1000000)
         r1 += r5
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         fp = ip << 1
         r1 = r1 << 1
         addlo sb, sb, 1
         r8 = r4 + r1
         orrlo r3, r0, r3, lsl 8
         r1 = (word) [r4 + r1]
         r0 = r2 >> 0xb
         rsb sl, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5
         add r1, r1, sl, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         lslhi ip, ip, 1
         addls ip, fp, 1
         uxthhi lr, r1
         bichi r6, r6, r5
         movhi r2, r0
         rsbls r3, r0, r3
         andls r6, r6, r5
         (a, b) = compare (ip, 0xff)
         [r8] = (half) lr
         bls 0x3851c              // unlikely

         goto loc_0x00038598;
    loc_0x00038598: // orphan
         fp = [var_38h]           // r13
         
         goto loc_0x000385a0;
    loc_0x000385a0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37f28(x)
         rsb r1, r0, r1
         rsb r3, r0, r3
         (a, b) = compare (r1, 0x1000000)
         sub ip, ip, ip, lsr 5
         lsllo r1, r1, 8
         [r2] = (half) ip
         ldrblo r0, [sb]
         addlo sb, sb, 1
         ip = (word) [r2 + 2]
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x386b8              // likely

         goto loc_0x000385d8;
    loc_0x000385d8: // orphan
         rsb r1, ip, 0x800
         add r6, r2, r6, lsl 4
         r6 += 0x104
         sl = 0
         add ip, ip, r1, lsr 5
         r8 = 8
         [r2 + 2] = (half) ip
         
         goto loc_0x000385f8;
    loc_0x000385f8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3830c(x)
         r5 = lr
         lr = [var_18h]
         ip = lr + fp
         fp += r0                 // r13
         r0 = lr
         lr += fp
         r4 = r0

    loc_0x00038614: // orphan
         // CODE XREF from fcn.00037adc @ 0x3862c(x)
         r0 = (byte) [r4 + r1]
         r1 += 1
         (a, b) = compare (r5, r1)
         [ip] + 1 = (byte) r0
         moveq r1, 0
         (a, b) = compare (ip, lr)
         bne 0x38614              // likely

         goto loc_0x00038630;
    loc_0x00038630: // orphan
         
         goto loc_0x00038634;
    loc_0x00038634: // orphan
         // CODE XREF from fcn.00037adc @ 0x3843c(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         ip = lr + 0x30
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + r4] = (half) r0
         r0 = (word) [r1 + ip]
         ldrblo r1, [sb]
         addlo sb, sb, 1 // DATA XREFS from fcn.0007cae4 @ 0x7cc20(r), 0x7cc3c(r), 0x7cc48(r), 0x7cc4c(r), 0x7cc64(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38708              // likely

         goto loc_0x00038674;
    loc_0x00038674: // orphan
         rsb r2, r0, 0x800
         lr = [var_10h]
         r4 = [var_34h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_34h] = lr
         [var_10h] = r4
         [r0 + ip] = (half) r2

    loc_0x00038694: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38704(x), 0x38770(x), 0x38994(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 7)
         r2 = [var_ch]            // "@"
         r2 += 0xa60
         movlo r0, 8
         movhs r0, 0xb
         r2 += 8
         [var_4h] = r0
         
         goto loc_0x000386b8;
    loc_0x000386b8: // orphan
         // CODE XREF from fcn.00037adc @ 0x385d4(x)
         rsb r3, r0, r3
         sub ip, ip, ip, lsr 5
         rsb r0, r0, r1
         r6 = r2 + 0x204
         sl = ~0xef
         [r2 + 2] = (half) ip
         r8 = 0x100
         
         goto loc_0x000386d8;
    loc_0x000386d8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3827c(x)
         r0 = [var_20h]
         (a, b) = compare (r0, ip)
         bhi 0x3828c              // likely

         return r0;
    loc_0x000386e4: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38288(x), 0x38410(x)
         r0 = 1
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x000386f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x38484(x)
         lr = [var_ch]            // "@"
         sub ip, ip, ip, lsr 5
         rsb r1, r2, r1
         rsb r3, r2, r3
         [lr + r0] = (half) ip
         
         goto loc_0x00038708;
    loc_0x00038708: // orphan
         // CODE XREF from fcn.00037adc @ 0x38670(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]
         (a, b) = compare (r2, 0x1000000)
         lr += 0x48
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + ip] = (half) r0
         r0 = (word) [r1 + lr]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38960              // likely

         goto loc_0x00038748;
    loc_0x00038748: // orphan
         r4 = [var_3ch]           // fcn.000bbf24
         rsb r2, r0, 0x800
         ip = [var_10h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_10h] = r4
         r4 = [var_34h]
         [r0 + lr] = (half) r2
         [var_34h] = ip
         [var_3ch] = r4
         
         goto loc_0x00038774;
    loc_0x00038774: // orphan
         // CODE XREF from fcn.00037adc @ 0x381d4(x)
         r1 -= 5

    loc_0x00038778: // orphan
         // CODE XREF from fcn.00037adc @ 0x387ac(x)
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r2 = r2 >> 1
         orrlo r3, r0, r3, lsl 8
         r1 -= 1
         rsb r3, r2, r3
         r0 = r3 >> 0x1f
         add ip, r0, ip, lsl 1
         r0 &= r2
         ip += 1
         r3 = r0 + r3
         bne 0x38778              // likely

         goto loc_0x000387b0;
    loc_0x000387b0: // orphan
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ip = ip << 4
         r0 = r1 + 0x640
         r1 = sb
         r0 += 6
         ldrblo r1, [sb]
         lr = (word) [r0]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb           // 0x6c20 // "hatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r4, lr, 0x800
         subls lr, lr, lr, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r2
         addhi lr, lr, r4, lsr 5
         uxthls lr, lr
         orrls ip, ip, 1
         movls r2, 6
         uxthhi lr, lr
         [r0] = (half) lr
         r0 = [var_58h]
         movhi r2, 4
         (a, b) = compare (r1, 0x1000000)
         r4 = r0 + r2
         lsllo r1, r1, 8
         lr = (word) [r0 + r2]
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb           // 0x6c20 // "hatEv"
         r0 = lr * r0             // 0x6c20 // "hatEv"
         (a, b) = compare (r0, r3)
         rsbhi r1, lr, 0x800
         subls lr, lr, lr, lsr 5
         addls r2, r2, 1
         rsbls r3, r0, r3
         addhi lr, lr, r1, lsr 5
         rsbls r0, r0, r1
         r1 = [var_58h]
         r2 = r2 << 1
         orrls ip, ip, 2
         uxthhi lr, lr
         uxthls lr, lr
         (a, b) = compare (r0, 0x1000000)
         [r4] = (half) lr
         lsllo r0, r0, 8
         lr = (word) [r1 + r2]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r0, lr, 0x800
         addls r4, r2, 1
         subls lr, lr, lr, lsr 5
         movhi r4, r2
         addhi r0, lr, r0, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r0
         uxthls r0, lr
         lr = r4 << 1
         r4 = [var_58h]
         uxthhi r0, r0
         orrls ip, ip, 4
         (a, b) = compare (r1, 0x1000000)
         lsllo r1, r1, 8
         [r4 + r2] = (half) r0
         ldrblo r2, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r2 = r0 * r2
         (a, b) = compare (r2, r3)
         rsbhi r1, r0, 0x800
         rsbls r3, r2, r3
         orrls ip, ip, 8
         rsbls r2, r2, r1
         addhi r0, r0, r1, lsr 5
         r1 = [var_58h]
         subls r0, r0, r0, lsr 5
         if (ip != 1)
         uxth r0, r0
         [r1 + lr] = (half) r0
         bne 0x38270              // likely

         goto loc_0x00038918;
    loc_0x00038918: // orphan
         r1 = [var_1ch]
         r0 = [var_4h]
         r1 += 0x110
         r0 -= 0xc
         r1 += 2
         [var_4h] = r0
         [var_1ch] = r1
         
         goto loc_0x00038938;
    loc_0x00038938: // orphan
         // CODE XREF from fcn.00037adc @ 0x38378(x)
         r1 = ip + r1
         lr = [var_54h]
         
         goto loc_0x00038944;
    loc_0x00038944: // orphan
         // CODE XREF from fcn.00037adc @ 0x38958(x)
         lr += 1

    loc_0x00038948: // orphan
         // CODE XREF from fcn.00037adc @ 0x38940(x)
         r4 = (byte) [r1] + 1
         (a, b) = compare (lr, r7)
         [ip] = (byte) r4
         ip = lr
         bne 0x38944              // unlikely

         goto loc_0x0003895c;
    loc_0x0003895c: // orphan
         
         goto loc_0x00038960;
    loc_0x00038960: // orphan
         // CODE XREF from fcn.00037adc @ 0x38744(x)
         rsb r3, r1, r3
         rsb r1, r1, r2
         r2 = [var_4ch]           // fcn.000bbf24
         sub r0, r0, r0, lsr 5
         ip = [var_10h]           // fcn.000bbf24
         [var_10h] = r2
         r2 = [var_3ch]
         [var_4ch] = r2
         r2 = [var_34h]           // fcn.000bbf24
         [var_34h] = ip
         [var_3ch] = r2
         r2 = [var_ch]            // "@"
         [r2 + lr] = (half) r0
         
         goto loc_0x00038998;
    loc_0x00038998: // orphan
         // CODE XREF from fcn.00037adc @ 0x37b1c(x)
         r1 = [r3 + 0x2c]
         r2 = [var_5ch]
         r3 = [r3 + 0xc]
         rsb r2, fp, r2
         rsb r3, r1, r3
         (a, b) = compare (r3, r2)
         strlo r1, [sp, 8]
         addlo r3, fp, r3
         strhs r1, [sp, 8]
         ldrhs r3, [sp, 0x5c]
         strlo r3, [sp, 0x14]
         strhs r3, [sp, 0x14]
         
}


========================================================
// Function at 0x0x3772c
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.0003751c (int32_t arg1, int32_t arg2, int32_t arg3) {
        // CALL XREFS from fcn.000389f8 @ 0x38d28(x), 0x38ff8(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r6 = ~0
        r3 = [r0 + 8] // arg1
        r2 = r1 + r2  // arg3
        r5 = [r0 + 0x2c] // arg1
        r8 = [r0 + 0x34] // arg1
        bic r6, r5, r6, lsl r3
        lr = [r0 + 0x1c] // arg1 // elf_phdr
        r4 = [r0 + 0x10] // arg1
        add r3, r6, r8, lsl 4
        (a, b) = compare (lr, 0x1000000)
        ip = [r0 + 0x20] // arg1 // elf_shdr
        r3 = r3 << 1
        r3 = (word) [r4 + r3]
        bhs 0x37570   // unlikely
        goto loc_0x00037558;
    loc_0x00037570:
        // CODE XREF from fcn.0003751c @ 0x37554(x)
        r7 = lr >> 0xb
        r3 *= r7
        (a, b) = compare (ip, r3)
        bhs 0x37650   // likely
        goto loc_0x00037580;
    loc_0x00037650:
        // CODE XREF from fcn.0003751c @ 0x3757c(x)
        r0 = r8 + 0xc0
        rsb r7, r3, lr
        rsb ip, r3, ip
        (a, b) = compare (r7, 0x1000000)
        r3 = r0 << 1
        r0 = (word) [r4 + r3]
        bhs 0x37684   // unlikely
        goto loc_0x0003766c;
    loc_0x00037684:
        // CODE XREF from fcn.0003751c @ 0x37668(x)
        lr = r7 >> 0xb
        r5 = r0 * lr
        (a, b) = compare (ip, r5)
        blo 0x37758   // unlikely
        goto loc_0x00037694;
    loc_0x00037758:
        // CODE XREF from fcn.0003751c @ 0x37690(x)
        r7 = r4 + 0x660
        r0 = 2
        r7 += 4
        sl = 0
        
    loc_0x00037768:
        // CODE XREF from fcn.0003751c @ 0x37a78(x)
        (a, b) = compare (r5, 0x1000000)
        r3 = (word) [r7] // "ecompFinal"
        bhs 0x3778c   // unlikely
        goto loc_0x00037774;
        goto loc_0x000376ac;
        goto loc_0x00037674;
        goto loc_0x00037594;
        goto loc_0x00037560;
        return r0;
    loc_0x00037560:
        r7 = (byte) [r1] // arg2
        lr = lr << 8
        r1 += 1       // arg2
        ip = r7 | ip
        break;
    loc_0x00037580: // orphan
         lr = [r0 + 0x30]         // arg1
         r4 += 0xe60
         r4 += 0xc
         orrs lr, r5, lr
         je 0x375d0               // unlikely

         goto loc_0x00037594;
    loc_0x00037594: // orphan
         lr = [r0 + 0x24]         // arg1
         r7 = ~0
         r6 = [r0 + 4]            // arg1
         (a, b) = compare (lr, 0)
         bic r5, r5, r7, lsl r6
         r7 = [r0 + 0x14]         // arg1
         ldreq lr, [r0, 0x28]     // arg1
         r6 = [r0]                // arg1
         lr -= 1
         r5 = r5 << r6
         rsb r6, r6, 8
         lr = (byte) [r7 + lr]
         add r5, r5, lr, asr r6
         add r5, r5, r5, lsl 1
         add r4, r4, r5, lsl 9

    loc_0x000375d0: // orphan
         // CODE XREF from fcn.0003751c @ 0x37590(x)
         (a, b) = compare (r8, 6)
         bhi 0x376bc              // unlikely

         goto loc_0x000375d8;
    loc_0x000375d8: // orphan
         lr = 1

    loc_0x000375dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x37628(x)
         r5 = lr << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + 1
         r0 = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x37608              // unlikely

         goto loc_0x000375f0;
    loc_0x000375f0: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000375fc;
    loc_0x000375fc: // orphan
         r6 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r6 | ip

    loc_0x00037608: // orphan
         // CODE XREF from fcn.0003751c @ 0x375ec(x)
         r6 = r3 >> 0xb
         r0 *= r6
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         rsbhs ip, r0, ip
         movlo lr, r5
         movlo r3, r0
         (a, b) = compare (lr, 0xff)
         bls 0x375dc              // likely

         goto loc_0x0003762c;
    loc_0x0003762c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37754(x)
         r0 = 1

    loc_0x00037630: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37804(x), 0x37880(x), 0x378e0(x)
         (a, b) = compare (r3, 0x1000000)
         movlo r3, 1
         movhs r3, 0
         (a, b) = compare (r1, r2) // arg3
         movlo r3, 0
         (a, b) = compare (r3, 0)
         movne r0, 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x0003766c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037674;
    loc_0x00037674: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037694: // orphan
         rsb r7, r5, r7
         r3 = r4 + r3
         (a, b) = compare (r7, 0x1000000)
         rsb r0, r5, ip
         ip = (word) [r3 + 0x18]  // section..text
         bhs 0x37918              // unlikely

         goto loc_0x000376ac;
    loc_0x000376ac: // orphan
         (a, b) = compare (r1, r2) // arg3
         blo 0x37908              // unlikely

         return r0;
    loc_0x000376b4: // orphan
         // XREFS: CODE 0x0003755c  CODE 0x000375f8  CODE 0x00037670   // XREFS: CODE 0x00037714  CODE 0x00037778  CODE 0x000377cc   // XREFS: CODE 0x00037844  CODE 0x00037904  CODE 0x00037944   // XREFS: CODE 0x000379a4  CODE 0x00037a04  CODE 0x00037a40   // XREFS: CODE 0x00037a9c  
         r0 = 0
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc) // 0x178000 // r13

    loc_0x000376bc: // orphan
         // CODE XREF from fcn.0003751c @ 0x375d4(x)
         lr = [r0 + 0x24]         // arg1
         r6 = [r0 + 0x38]         // arg1 // fcn.000bbf24
         r5 = [r0 + 0x14]         // arg1
         (a, b) = compare (lr, r6)
         rsb r6, r6, lr
         lr = 1
         ldrlo r0, [r0, 0x28]     // arg1
         movhs r0, 0
         r0 = r5 + r0             // arg1
         r5 = 0x100
         r6 = (byte) [r0 + r6]    // arg1

    loc_0x000376e8: // orphan
         // CODE XREF from fcn.0003751c @ 0x37750(x)
         r6 = r6 << 1
         r0 = r5 + lr
         r7 = r5 & r6
         (a, b) = compare (r3, 0x1000000)
         r0 += r7
         r8 = lr << 1
         r0 = r0 << 1
         r0 = (word) [r4 + r0]    // "ecompFinal"
         bhs 0x37724              // unlikely

         goto loc_0x0003770c;
    loc_0x0003770c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037718;
    loc_0x00037718: // orphan
         sb = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = sb | ip

    loc_0x00037724: // orphan
         // CODE XREF from fcn.0003751c @ 0x37708(x)
         sb = r3 >> 0xb
         r0 *= sb
         (a, b) = compare (ip, r0)
         rsb r3, r0, r3
         lsllo lr, lr, 1
         addhs lr, r8, 1
         biclo r5, r5, r7
         movlo r3, r0
         rsbhs ip, r0, ip
         andhs r5, r5, r7
         (a, b) = compare (lr, 0xff)
         bls 0x376e8              // likely

         goto loc_0x00037754;
    loc_0x00037754: // orphan
         
         goto loc_0x00037758;
    loc_0x00037774: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x0003777c;
    loc_0x0003777c: // orphan
         lr = (byte) [r1]         // arg2
         r5 = r5 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x0003778c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37770(x)
         lr = r5 >> 0xb
         r3 *= lr
         (a, b) = compare (ip, r3)
         bhs 0x3798c              // likely

         goto loc_0x0003779c;
    loc_0x0003779c: // orphan
         add r7, r7, r6, lsl 4
         sb = 0
         r7 += 4
         r8 = 8

    loc_0x000377ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x379e8(x)
         r5 = 1

    loc_0x000377b0: // orphan
         // CODE XREF from fcn.0003751c @ 0x377fc(x)
         r6 = r5 << 1
         (a, b) = compare (r3, 0x1000000)
         r5 = r6 + 1
         lr = (word) [r7 + r6]    // "ecompFinal"
         bhs 0x377dc              // unlikely

         goto loc_0x000377c4;
    loc_0x000377c4: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x000377d0;
    loc_0x000377d0: // orphan
         fp = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = fp | ip

    loc_0x000377dc: // orphan
         // CODE XREF from fcn.0003751c @ 0x377c0(x)
         fp = r3 >> 0xb
         lr *= fp
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r5, r6
         movlo r3, lr
         (a, b) = compare (r5, r8)
         blo 0x377b0              // unlikely

         goto loc_0x00037800;
    loc_0x00037800: // orphan
         (a, b) = compare (sl, 3)
         bhi 0x37630              // unlikely

         goto loc_0x00037808;
    loc_0x00037808: // orphan
         rsb r8, r8, sb
         r6 = 1
         r5 = r8 + r5
         (a, b) = compare (r5, 3)
         lslls r5, r5, 7
         addls r8, r5, 0x360
         movhi r8, 0x4e0

    loc_0x00037824: // orphan
         // CODE XREF from fcn.0003751c @ 0x37874(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         lr = r5 + r8
         r6 = r5 + 1
         lr = (word) [r4 + lr]    // "ecompFinal"
         bhs 0x37854              // unlikely

         goto loc_0x0003783c;
    loc_0x0003783c: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037848;
    loc_0x00037848: // orphan
         r7 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r7 | ip

    loc_0x00037854: // orphan
         // CODE XREF from fcn.0003751c @ 0x37838(x)
         r7 = r3 >> 0xb
         lr *= r7
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         (a, b) = compare (r6, 0x3f) // '?'
         bls 0x37824              // likely

         goto loc_0x00037878;
    loc_0x00037878: // orphan
         lr = r6 - 0x40
         (a, b) = compare (lr, 3)
         bls 0x37630              // unlikely

         goto loc_0x00037884;
    loc_0x00037884: // orphan
         (a, b) = compare (lr, 0xd)
         r5 = lr >> 1
         r7 = r5 - 1
         bhi 0x37a88              // unlikely

         goto loc_0x00037894;
    loc_0x00037894: // orphan
         r5 = lr & 1
         rsb lr, r6, 0x2ec
         r5 |= 2
         lr += 3
         add lr, lr, r5, lsl r7
         add r4, r4, lr, lsl 1

    loc_0x000378ac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ad8(x)
         r6 = 1
         
         goto loc_0x000378b4;
    loc_0x000378b4: // orphan
         // CODE XREF from fcn.0003751c @ 0x37900(x)
         r8 = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = r8 | ip

    loc_0x000378c0: // orphan
         // CODE XREF from fcn.0003751c @ 0x378f4(x)
         r8 = r3 >> 0xb
         lr *= r8
         (a, b) = compare (ip, lr)
         rsb r3, lr, r3
         rsbhs ip, lr, ip
         movlo r6, r5
         movlo r3, lr
         r7 -= 1
         je 0x37630               // unlikely

         goto loc_0x000378e4;
    loc_0x000378e4: // orphan
         // CODE XREF from fcn.0003751c @ 0x378b0(x)
         r5 = r6 << 1
         (a, b) = compare (r3, 0x1000000)
         r6 = r5 + 1
         lr = (word) [r4 + r5]    // "ecompFinal"
         bhs 0x378c0              // unlikely

         goto loc_0x000378f8;
    loc_0x000378f8: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         blo 0x378b4              // unlikely

         goto loc_0x00037904;
    loc_0x00037904: // orphan
         
         goto loc_0x00037908;
    loc_0x00037908: // orphan
         // CODE XREF from fcn.0003751c @ 0x376b0(x)
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         r0 = lr | r0

    loc_0x00037918: // orphan
         // CODE XREF from fcn.0003751c @ 0x376a8(x)
         lr = r7 >> 0xb
         lr = ip * lr
         (a, b) = compare (r0, lr)
         bhs 0x379ec              // likely

         goto loc_0x00037928;
    loc_0x00037928: // orphan
         r3 = r8 + 0xf
         (a, b) = compare (lr, 0x1000000)
         add r3, r6, r3, lsl 4
         r3 = r3 << 1
         r3 = (word) [r4 + r3]
         bhs 0x37958              // unlikely

         goto loc_0x00037940;
    loc_0x00037940: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037948;
    loc_0x00037948: // orphan
         ip = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         r0 = ip | r0

    loc_0x00037958: // orphan
         // CODE XREF from fcn.0003751c @ 0x3793c(x)
         ip = lr >> 0xb
         ip = r3 * ip
         (a, b) = compare (r0, ip)
         bhs 0x37a7c              // likely

         goto loc_0x00037968;
    loc_0x00037968: // orphan
         (a, b) = compare (ip, 0x1000000)
         movlo ip, 1
         movhs ip, 0
         (a, b) = compare (r1, r2) // arg3
         movlo ip, 0
         (a, b) = compare (ip, 0)
         movne r0, 0
         moveq r0, 3

         goto loc_0x0003798c;
    loc_0x0003798c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37798(x)
         rsb lr, r3, r5
         rsb ip, r3, ip
         (a, b) = compare (lr, 0x1000000)
         r3 = (word) [r7 + 2]
         bhs 0x379b8              // unlikely

         goto loc_0x000379a0;
    loc_0x000379a0: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x000379a8;
    loc_0x000379a8: // orphan
         r5 = (byte) [r1]         // arg2
         lr = lr << 8
         r1 += 1                  // arg2
         ip = r5 | ip

    loc_0x000379b8: // orphan
         // CODE XREF from fcn.0003751c @ 0x3799c(x)
         r5 = lr >> 0xb
         r3 *= r5
         (a, b) = compare (ip, r3)
         addlo r6, r7, r6, lsl 4
         rsbhs ip, r3, ip
         movlo sb, 8
         addlo r7, r6, 0x104
         movlo r8, sb
         addhs r7, r7, 0x204
         rsbhs r3, r3, lr
         movhs sb, 0x10
         movhs r8, 0x100
         
         goto loc_0x000379ec;
    loc_0x000379ec: // orphan
         // CODE XREF from fcn.0003751c @ 0x37924(x)
         rsb r7, lr, r7
         rsb ip, lr, r0
         (a, b) = compare (r7, 0x1000000)
         r0 = (word) [r3 + 0x30]
         bhs 0x37a18              // unlikely

         goto loc_0x00037a00;
    loc_0x00037a00: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a08;
    loc_0x00037a08: // orphan
         lr = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037a18: // orphan
         // CODE XREF from fcn.0003751c @ 0x379fc(x)
         lr = r7 >> 0xb
         r5 = r0 * lr
         (a, b) = compare (ip, r5)
         blo 0x37a68              // unlikely

         goto loc_0x00037a28;
    loc_0x00037a28: // orphan
         rsb r7, r5, r7
         rsb ip, r5, ip
         (a, b) = compare (r7, 0x1000000)
         r3 = (word) [r3 + 0x48]
         bhs 0x37a54              // unlikely

         goto loc_0x00037a3c;
    loc_0x00037a3c: // orphan
         (a, b) = compare (r1, r2) // arg3
         bhs 0x376b4              // likely

         goto loc_0x00037a44;
    loc_0x00037a44: // orphan
         r0 = (byte) [r1]         // arg2
         r7 = r7 << 8
         r1 += 1                  // arg2
         ip = r0 | ip

    loc_0x00037a54: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a38(x)
         lr = r7 >> 0xb
         r5 = r3 * lr
         (a, b) = compare (ip, r5)
         rsbhs ip, r5, ip
         rsbhs r5, r5, r7

    loc_0x00037a68: // orphan
         // CODE XREFS from fcn.0003751c @ 0x37a24(x), 0x37a84(x)
         r7 = r4 + 0xa60
         r0 = 3
         r7 += 8
         sl = 0xc
         
         goto loc_0x00037a7c;
    loc_0x00037a7c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37964(x)
         rsb r5, ip, lr
         rsb ip, ip, r0
         
         goto loc_0x00037a88;
    loc_0x00037a88: // orphan
         // CODE XREF from fcn.0003751c @ 0x37890(x)
         r5 -= 5

    loc_0x00037a8c: // orphan
         // CODE XREF from fcn.0003751c @ 0x37ac8(x)
         (a, b) = compare (r3, 0x1000000)
         bhs 0x37aac              // unlikely

         goto loc_0x00037a94;
    loc_0x00037a94: // orphan
         (a, b) = compare (r1, r2) // arg3
         r3 = r3 << 8
         bhs 0x376b4              // likely

         goto loc_0x00037aa0;
    loc_0x00037aa0: // orphan
         lr = (byte) [r1]         // arg2
         r1 += 1                  // arg2
         ip = lr | ip

    loc_0x00037aac: // orphan
         // CODE XREF from fcn.0003751c @ 0x37a90(x)
         r3 = r3 >> 1
         r5 -= 1
         rsb lr, r3, ip
         lr = lr >> 0x1f
         lr -= 1
         lr &= r3
         rsb ip, lr, ip
         bne 0x37a8c              // likely

         goto loc_0x00037acc;
    loc_0x00037acc: // orphan
         r4 += 0x640
         r7 = 4
         r4 += r7
         
}


========================================================
// Function at 0x0x385b4
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.00037adc (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_1h, int32_t arg_4h) {
        // CALL XREFS from fcn.000389f8 @ 0x38d60(x), 0x38eec(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        r3 = r0       // arg1
        sp -= 0x64
        sb = [r3 + 0x18] // section..text
        fp = [r3 + 0x24]
        [var_48h] = r0 // arg1
        r0 = [0x00037afc] // [0x389cc:4]=0x7d4e8
        [var_5ch] = r1 // arg2
        r0 = pc + r0
        [var_2ch] = r2 // arg3
        [var_44h] = r0
        
    loc_0x00037b08:
        // CODE XREF from fcn.00037adc @ 0x37e78(x)
        r3 = [var_48h]
        r3 = [r3 + 0x30]
        (a, b) = compare (r3, 0)
        [var_20h] = r3
        r3 = [var_48h]
        je 0x38998    // unlikely
        goto loc_0x00037b20;
        return r0;
    loc_0x00037b20:
        r3 = [r3 + 0x2c]
        [var_8h] = r3
        r3 = [var_5ch]
        [var_14h] = r3
        
    loc_0x00037b30:
        // CODE XREF from fcn.00037adc @ 0x389c8(x)
        r1 = [var_48h]
        lr = 1
        r3 = [r1 + 0x10]
        r2 = [r1 + 8]
        r0 = r3
        [var_ch] = r3
        r3 = 0
        [var_1ch] = r3
        r3 = [r1 + 4]
        r2 = lr << r2
        r2 -= 1
        [var_28h] = r2
        r3 = lr << r3
        r2 = [r1 + 0x44]
        r3 -= 1
        [var_50h] = r3
        r3 = [r1]
        ip = r0 + 0x640
        lr = [r1 + 0x34]
        r0 += 0xe60
        ip += 4
        r0 += 0xc
        [var_40h] = r3
        r3 = [r1 + 0x14]
        [var_4h] = lr
        [var_58h] = ip
        [var_18h] = r3
        r3 = [r1 + 0x28]
        lr = [r1 + 0x38] // fcn.000bbf24
        ip = [r1 + 0x3c] // fcn.000bbf24
        [var_30h] = r0
        [var_4ch] = r2
        r0 = [r1 + 0x40] // fcn.000bbf24
        [var_24h] = r3
        r2 = [r1 + 0x1c] // elf_phdr
        r3 = [r1 + 0x20] // elf_shdr
        [var_10h] = lr
        [var_34h] = ip
        [var_3ch] = r0
        return r0;
    loc_0x00037c10: // orphan
         r2 = [var_8h]
         lr = [var_20h]
         r4 = [var_30h]
         orrs r2, r2, lr
         rsb r2, ip, 0x800
         add ip, ip, r2, lsr 5
         r2 = [var_ch]
         [r2 + r0] = (half) ip
         je 0x37c78               // unlikely

         goto loc_0x00037c34;
    loc_0x00037c34: // orphan
         r0 = [var_50h]
         (a, b) = compare (fp, 0)
         r2 = [var_8h]
         ip = [var_18h]
         r2 &= r0
         r0 = [var_40h]
         r2 = r2 << r0
         ldreq r0, [sp, 0x24]
         subne r0, fp, 1
         subeq r0, r0, 1
         ip = (byte) [ip + r0]
         r0 = [var_40h]
         rsb r0, r0, 8
         add r2, r2, ip, asr r0
         r0 = [var_30h]
         add r2, r2, r2, lsl 1
         add r4, r0, r2, lsl 9

    loc_0x00037c78: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c30(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 6)
         bhi 0x384e8              // likely

         goto loc_0x00037c84;
    loc_0x00037c84: // orphan
         r2 = r1
         ip = 1

    loc_0x00037c8c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37ce8(x)
         (a, b) = compare (r2, 0x1000000)
         r5 = ip << 1
         lsllo r2, r2, 8
         ip = r5 + 1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r1 = (word) [r4 + r5]    // "ecompFinal"
         r6 = r4 + r5
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb r7, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt" // "rfaceShow"
         add r1, r1, r7, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         rsbls r3, r0, r3
         movhi ip, r5
         movhi r2, r0
         (a, b) = compare (ip, 0xff)
         [r6] = (half) lr
         bls 0x37c8c              // likely

         goto loc_0x00037cec;
    loc_0x00037cec: // orphan
         // CODE XREF from fcn.00037adc @ 0x3859c(x)
         r1 = [var_18h]
         r0 = [var_4h]
         [r1 + fp] = (byte) ip
         fp += 1
         r1 = [var_8h]
         r1 += 1
         [var_8h] = r1
         r1 = [var_44h]
         r1 = (byte) [r1 + r0]
         [var_4h] = r1

    loc_0x00037d14: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383ec(x), 0x384e4(x), 0x38630(x)
         r1 = [var_2ch]
         r0 = [var_14h]
         (a, b) = compare (r1, sb)
         cmphi r0, fp
         bhi 0x37bcc              // unlikely

         goto loc_0x00037d28;
    loc_0x00037d28: // orphan
         // CODE XREF from fcn.00037adc @ 0x38934(x)
         (a, b) = compare (r2, 0x1000000)
         r0 = [var_48h]
         ip = [var_8h]
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         [r0 + 0x1c] = r2
         orrlo r3, r1, r3, lsl 8
         [r0 + 0x20] = r3
         r3 = [var_1ch]
         r1 = [r0 + 0xc]
         [r0 + 0x2c] = ip
         [r0 + 0x48] = r3
         (a, b) = compare (r1, ip)
         r3 = [var_10h]           // fcn.000bbf24
         ip = [var_1ch]
         [r0 + 0x18] = sb
         [r0 + 0x38] = r3
         r3 = [var_34h]           // fcn.000bbf24
         [r0 + 0x24] = fp
         [r0 + 0x3c] = r3
         r3 = [var_3ch]           // fcn.000bbf24
         [r0 + 0x40] = r3
         r3 = [var_4ch]
         [r0 + 0x44] = r3
         r3 = [var_4h]
         [r0 + 0x34] = r3
         movls r3, r0
         strls r1, [r3, 0x30]
         r3 = ip - 1
         (a, b) = compare (r3, 0x110)
         movhi sl, ip
         bhi 0x37e50              // likely

         goto loc_0x00037dac;
    loc_0x00037dac: // orphan
         r0 = [var_48h]
         r3 = [var_5ch]
         rsb lr, fp, r3
         r3 = [r0 + 0x30]
         (a, b) = compare (lr, ip)
         r2 = [r0 + 0x14]
         r0 = [r0 + 0x28]
         movhs lr, ip
         (a, b) = compare (r3, 0)
         bne 0x37de8              // unlikely

         goto loc_0x00037dd4;
    loc_0x00037dd4: // orphan
         r3 = [var_8h]
         rsb r3, r3, r1
         (a, b) = compare (r3, lr)
         ldrls r3, [sp, 0x48]
         strls r1, [r3, 0x30]

    loc_0x00037de8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37dd0(x)
         r1 = [var_1ch]
         (a, b) = compare (lr, 0)
         r3 = [var_8h]
         rsb sl, lr, r1
         r1 = [var_48h]
         r3 = lr + r3
         [r1 + 0x48] = sl
         [r1 + 0x2c] = r3
         je 0x37e48               // likely

         goto loc_0x00037e0c;
    loc_0x00037e0c: // orphan
         ip = [var_10h]           // fcn.000bbf24
         lr += fp                 // r13
         r1 = r2 + fp             // r13

    loc_0x00037e18: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e3c(x)
         (a, b) = compare (ip, fp)
         rsb r3, ip, fp           // r13
         r3 = r2 + r3             // r13
         fp += 1
         movhi r4, r0
         movls r4, 0
         r3 = (byte) [r3 + r4]
         (a, b) = compare (fp, lr)
         [r1] + 1 = (byte) r3
         bne 0x37e18              // likely

         goto loc_0x00037e40;
    loc_0x00037e40: // orphan
         r3 = [var_48h]
         sl = [r3 + 0x48]

    loc_0x00037e48: // orphan
         // CODE XREF from fcn.00037adc @ 0x37e08(x)
         r3 = [var_48h]
         [r3 + 0x24] = fp

    loc_0x00037e50: // orphan
         // CODE XREF from fcn.00037adc @ 0x37da8(x)
         r3 = [var_5ch]
         (a, b) = compare (r3, fp)
         bls 0x37e7c              // likely

         goto loc_0x00037e5c;
    loc_0x00037e5c: // orphan
         r3 = [var_48h]
         sb = [r3 + 0x18]
         r3 = [var_2ch]
         (a, b) = compare (r3, sb)
         bls 0x37e7c              // likely

         goto loc_0x00037e70;
    loc_0x00037e70: // orphan
         r3 = 0x111
         (a, b) = compare (sl, r3)
         bls 0x37b08              // likely

         return r0;
    loc_0x00037e7c: // orphan
         // CODE XREFS from fcn.00037adc @ 0x37e58(x), 0x37e6c(x)
         r3 = 0x112
         (a, b) = compare (sl, r3)
         ldrhi r2, [sp, 0x48]
         strhi r3, [r2, 0x48]
         r0 = 0
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00037e98: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c0c(x)
         lr = [var_4h]
         rsb r2, r1, r2
         r4 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lr += 0xc0
         rsb r3, r1, r3
         lsllo r2, r2, 8
         sub ip, ip, ip, lsr 5
         lr = lr << 1
         [r4 + r0] = (half) ip
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x383f0              // likely

         goto loc_0x00037ee0;
    loc_0x00037ee0: // orphan
         r4 = [var_ch]
         rsb ip, r0, 0x800
         r5 = [var_4h]
         r2 = r4 + 0x660
         add r0, r0, ip, lsr 5
         r2 += 4
         r5 += 0xc
         [r4 + lr] = (half) r0
         [var_4h] = r5

    loc_0x00037f04: // orphan
         // CODE XREF from fcn.00037adc @ 0x386b4(x)
         (a, b) = compare (r1, 0x1000000)
         ip = (word) [r2]         // "oSha1Final"
         lsllo r1, r1, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x385a0              // likely

         goto loc_0x00037f2c;
    loc_0x00037f2c: // orphan
         add r6, r2, r6, lsl 4
         rsb r1, ip, 0x800
         r6 += 4
         sl = ~7
         r8 = 8
         add ip, ip, r1, lsr 5
         [r2] = (half) ip

    loc_0x00037f48: // orphan
         // CODE XREFS from fcn.00037adc @ 0x385f4(x), 0x386d4(x)
         r5 = 1

    loc_0x00037f4c: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fa8(x)
         (a, b) = compare (r0, 0x1000000)
         lr = r5 << 1
         lsllo r0, r0, 8
         r5 = lr + 1
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r2 = (word) [r6 + lr]
         r4 = r6 + lr // DATA XREF from sym.s3eGLGetInt @ 0x8adf0(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb
         rsb r7, r2, 0x800
         r1 = r2 * r1
         sub ip, r2, r2, lsr 5
         add r2, r2, r7, lsr 5
         uxth ip, ip
         (a, b) = compare (r1, r3)
         rsb r0, r1, r0
         uxthhi ip, r2
         rsbls r3, r1, r3
         movhi r5, lr
         movhi r0, r1
         (a, b) = compare (r5, r8)
         [r4] = (half) ip
         blo 0x37f4c              // unlikely

         goto loc_0x00037fac;
    loc_0x00037fac: // orphan
         r2 = [var_4h]
         lr = sb
         (a, b) = compare (r2, 0xb)
         r2 = r5 + sl
         [var_1ch] = r2
         r2 = r0
         bls 0x382bc              // unlikely

         goto loc_0x00037fc8;
    loc_0x00037fc8: // orphan
         r1 = [var_1ch]
         ip = [var_ch]            // "@" // DATA XREF from sym.s3eGLGetInt @ 0x8ad8c(r)
         (a, b) = compare (r1, 3)
         lslls r1, r1, 7
         addls r1, r1, 0x360      // (pstr 0x00000000) ">"
         movhi r1, 0x4e0
         (a, b) = compare (r0, 0x1000000)
         r1 = ip + r1
         lsllo r2, r0, 8
         ip = (word) [r1 + 2]
         r0 = r2 >> 0xb
         ldrblo lr, [sb]
         addlo sb, sb, 1
         r0 = ip * r0
         orrlo r3, lr, r3, lsl 8
         (a, b) = compare (r0, r3)
         rsbhi lr, ip, 0x800
         rsbls r3, r0, r3
         rsbls r0, r0, r2
         movhi r2, 4
         movls r2, 6
         addhi ip, ip, lr, lsr 5
         subls ip, ip, ip, lsr 5
         (a, b) = compare (r0, 0x1000000)
         lsllo r0, r0, 8
         lr = r1 + r2
         uxth ip, ip
         [r1 + 2] = (half) ip
         ldrblo ip, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, ip, r3, lsl 8
         ip = r0 >> 0xb
         ip = r4 * ip
         (a, b) = compare (ip, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, ip, r3
         addls r2, r2, 1          // (pstr 0x00000000) ">"
         addhi r0, r4, r0, lsr 5
         rsbls ip, ip, r0
         uxthls r0, r4
         r2 = r2 << 1
         uxthhi r0, r0
         (a, b) = compare (ip, 0x1000000)
         [lr] = (half) r0
         lsllo ip, ip, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + r2]
         orrlo r3, r0, r3, lsl 8
         r0 = ip >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi ip, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, r2, 1
         addhi ip, r4, ip, lsr 5
         rsbls r0, r0, ip
         movhi lr, r2
         uxthls ip, r4
         uxthhi ip, ip
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + r2] = (half) ip
         lsllo r0, r0, 8
         ldrblo r2, [sb]          // (pstr 0x00000000) ">"
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi r0, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r2, r3
         addls ip, lr, 1          // (pstr 0x00005400) "rGetLocaltimeOffset"
         addhi r0, r4, r0, lsr 5
         rsbls r2, r2, r0
         movhi ip, lr
         uxthls r0, r4
         uxthhi r0, r0
         (a, b) = compare (r2, 0x1000000)
         ip = ip << 1
         lsllo r2, r2, 8
         [r1 + lr] = (half) r0
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r4 = (word) [r1 + ip]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         r0 = r4 * r0
         (a, b) = compare (r0, r3)
         rsbhi r2, r4, 0x800
         subls r4, r4, r4, lsr 5
         rsbls r3, r0, r3
         addls lr, ip, 1
         addhi r2, r4, r2, lsr 5
         rsbls r0, r0, r2
         movhi lr, ip
         uxthls r2, r4
         uxthhi r2, r2
         (a, b) = compare (r0, 0x1000000)
         lr = lr << 1
         [r1 + ip] = (half) r2
         lsllo r0, r0, 8
         ldrblo r2, [sb]
         r4 = (word) [r1 + lr]
         addlo sb, sb, 1
         orrlo r3, r2, r3, lsl 8
         r2 = r0 >> 0xb
         r2 = r4 * r2
         (a, b) = compare (r2, r3)
         rsbhi ip, r4, 0x800
         rsbls r3, r2, r3
         rsbls r2, r2, r0
         addls r0, lr, 1
         addhi r4, r4, ip, lsr 5
         movhi r0, lr
         ip = r0 - 0x40
         subls r4, r4, r4, lsr 5
         (a, b) = compare (ip, 3)
         uxth r4, r4
         [r1 + lr] = (half) r4
         bls 0x38270              // unlikely

         goto loc_0x000381c0;
    loc_0x000381c0: // orphan
         (a, b) = compare (ip, 0xd)
         r1 = ip >> 1
         ip &= 1
         r6 = r1 - 1
         ip |= 2
         bhi 0x38774              // unlikely

         goto loc_0x000381d8;
    loc_0x000381d8: // orphan
         rsb r0, r0, 0x2ec
         ip = ip << r6
         r0 += 3
         r7 = [var_ch]            // "@"
         r8 = 1
         r0 += ip
         sl = r8
         [var_4ch] = fp
         r1 = r0 << r8
         [var_38h] = r1

    loc_0x00038200: // orphan
         // CODE XREF from fcn.00037adc @ 0x38268(x)
         r1 = [var_38h]
         (a, b) = compare (r2, 0x1000000)
         r4 = r8 << 1
         lsllo r2, r2, 8
         r5 = r4 + r1
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r8 = r4 + 1
         r1 = (word) [r7 + r5]
         orrlo r3, r0, r3, lsl 8
         r0 = r2 >> 0xb
         rsb fp, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5    // 0x441a // "SurfaceGetInt"
         add r1, r1, fp, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         uxthhi lr, r1
         orrls ip, ip, sl
         movhi r8, r4
         movhi r2, r0
         rsbls r3, r0, r3
         r6 -= 1
         [r7 + r5] = (half) lr
         sl = sl << 1
         bne 0x38200              // likely

         goto loc_0x0003826c;
    loc_0x0003826c: // orphan
         fp = [var_4ch]           // (pstr 0x00000000) ">" r13

    loc_0x00038270: // orphan
         // CODE XREFS from fcn.00037adc @ 0x381bc(x), 0x38914(x)
         r1 = [var_20h]           // (cstr 0x00000000) ">"
         (a, b) = compare (r1, 0)
         r1 = ip + 1
         bne 0x386d8              // likely

         goto loc_0x00038280;
    loc_0x00038280: // orphan
         r0 = [var_8h]
         (a, b) = compare (r0, ip)
         bls 0x386e4              // unlikely

         goto loc_0x0003828c;
    loc_0x0003828c: // orphan
         // CODE XREF from fcn.00037adc @ 0x386e0(x)
         r0 = [var_4h]
         (a, b) = compare (r0, 0x12)
         r0 = [var_3ch]           // fcn.000bbf24
         [var_4ch] = r0
         movhi r0, 0xa            // (pstr 0x00000000) ">"
         movls r0, 7
         [var_4h] = r0
         r0 = [var_34h]           // fcn.000bbf24
         [var_3ch] = r0
         r0 = [var_10h]           // fcn.000bbf24
         [var_10h] = r1
         [var_34h] = r0

    loc_0x000382bc: // orphan
         // CODE XREF from fcn.00037adc @ 0x37fc4(x)
         r1 = [var_1ch]
         r4 = [var_10h]
         r5 = r1 + 2
         r1 = [var_14h]
         lr = [var_24h]
         rsb r0, fp, r1
         rsb r1, r4, fp           // (pstr 0x00000000) ">" r13
         (a, b) = compare (r5, r0)
         ip = lr
         movlo r0, r5
         (a, b) = compare (fp, r4)
         rsb r4, r0, r5
         [var_1ch] = r4
         movhs ip, 0
         r1 += ip                 // (pstr 0x00000000) ">" r13
         ip = [var_8h]
         ip += r0
         [var_8h] = ip
         ip = r1 + r0
         (a, b) = compare (lr, ip)
         blo 0x385f8              // unlikely

         goto loc_0x00038310;
    loc_0x00038310: // orphan
         r6 = [var_18h]
         rsb r1, fp, r1
         r8 = arg_4h
         sl = fp + r1
         ip = r6 + fp
         r5 = r8 + r1
         lr = ip + 1
         r7 = ip + r0
         (a, b) = compare (sl, r8)
         cmplt fp, r5
         rsb r4, lr, r7
         r6 += sl
         [var_54h] = lr
         sl = r6 | ip
         lr = r4 + 1
         movge r5, 1
         movlt r5, 0
         (a, b) = compare (lr, 9)
         movls r5, 0
         andhi r5, r5, 1
         (a, b) = compare (sl, 3)
         andeq r5, r5, 1
         [var_38h] = lr
         movne r5, 0
         (a, b) = compare (r5, 0)
         r0 += fp                 // (pstr 0x00000000) ">" r13
         je 0x38938               // likely

         goto loc_0x0003837c;
    loc_0x0003837c: // orphan
         r4 -= 3
         r6 -= 4
         r8 = ip
         r5 = 0
         r4 = r4 >> 2
         lr = r4 + 1
         r4 = lr << 2

    loc_0x00038398: // orphan
         // CODE XREF from fcn.00037adc @ 0x383a8(x)
         r5 += 1
         sl = [r6 + 4]!
         [r8] + 4 = sl
         blo 0x38398              // unlikely

         goto loc_0x000383ac;
    loc_0x000383ac: // orphan
         r5 = [var_38h]
         (a, b) = compare (r5, r4)
         r4 = ip + r4
         je 0x383e8               // likely

         goto loc_0x000383bc;
    loc_0x000383bc: // orphan
         r6 = (byte) [r4 + r1]
         r5 = r4 + 1
         (a, b) = compare (r7, r5)
         strb r6, [ip, lr, lsl 2]
         je 0x383e8               // unlikely

         goto loc_0x000383d0;
    loc_0x000383d0: // orphan
         lr = (byte) [r5 + r1]
         ip = r4 + 2
         (a, b) = compare (r7, ip)
         [r4 + 1] = (byte) lr
         ldrbne r1, [ip, r1]
         strbne r1, [r4, 2]

    loc_0x000383e8: // orphan
         // CODE XREFS from fcn.00037adc @ 0x383b8(x), 0x383cc(x), 0x3895c(x)
         fp = r0
         
         goto loc_0x000383f0;
    loc_0x000383f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37edc(x)
         ip = [var_8h]
         sub r0, r0, r0, lsr 5
         r4 = [var_20h]
         rsb r2, r1, r2
         rsb r3, r1, r3
         orrs ip, ip, r4
         ip = [var_ch]            // "@"
         [ip + lr] = (half) r0
         je 0x386e4               // unlikely

         goto loc_0x00038414;
    loc_0x00038414: // orphan
         (a, b) = compare (r2, 0x1000000)
         r4 = lr + 0x18
         lsllo r2, r2, 8
         ldrblo r1, [sb]
         addlo sb, sb, 1
         r0 = (word) [ip + r4]
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38634              // likely

         goto loc_0x00038440;
    loc_0x00038440: // orphan
         r2 = [var_4h]
         rsb ip, r0, 0x800
         (a, b) = compare (r1, 0x1000000)
         r2 += 0xf
         add r0, r0, ip, lsr 5
         ip = [var_ch]
         lsllo r1, r1, 8
         add r2, r6, r2, lsl 4
         [ip + r4] = (half) r0
         r0 = r2 << 1
         ldrblo r2, [sb]
         addlo sb, sb, 1
         ip = (word) [ip + r0]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb
         r2 = ip * r2
         (a, b) = compare (r2, r3)
         bls 0x386f0              // likely

         goto loc_0x00038488;
    loc_0x00038488: // orphan
         rsb lr, ip, 0x800
         r5 = [var_10h]
         r6 = [var_18h]
         add ip, ip, lr, lsr 5
         lr = [var_ch]            // "@"
         r4 = [var_24h]
         (a, b) = compare (fp, r5)
         rsb r1, r5, fp           // r13
         r1 = r6 + r1
         movhs r4, 0
         [lr + r0] = (half) ip
         r0 = [var_4h]
         r1 = (byte) [r1 + r4]
         r4 = r6
         (a, b) = compare (r0, 7)
         r0 = [var_8h]
         [r6 + fp] = (byte) r1
         r0 += 1
         fp += 1
         [var_8h] = r0
         movlo r0, 9
         movhs r0, 0xb
         [var_4h] = r0
         
         goto loc_0x000384e8;
    loc_0x000384e8: // orphan
         // CODE XREF from fcn.00037adc @ 0x37c80(x)
         r2 = [var_48h]
         r6 = 0x100
         r0 = [var_24h]
         [var_38h] = fp
         lr = [r2 + 0x14]
         r2 = [var_10h]
         rsb ip, r2, fp           // r13
         (a, b) = compare (fp, r2)
         r2 = r1
         r1 = lr + ip
         movhs r0, 0
         ip = 1
         r7 = (byte) [r1 + r0]

    loc_0x0003851c: // orphan
         // CODE XREF from fcn.00037adc @ 0x38594(x)
         r7 = r7 << 1
         r1 = ip + r6
         r5 = r6 & r7
         (a, b) = compare (r2, 0x1000000)
         r1 += r5
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         fp = ip << 1
         r1 = r1 << 1
         addlo sb, sb, 1
         r8 = r4 + r1
         orrlo r3, r0, r3, lsl 8
         r1 = (word) [r4 + r1]
         r0 = r2 >> 0xb
         rsb sl, r1, 0x800
         r0 = r1 * r0
         sub lr, r1, r1, lsr 5
         add r1, r1, sl, lsr 5
         uxth lr, lr
         (a, b) = compare (r0, r3)
         rsb r2, r0, r2
         lslhi ip, ip, 1
         addls ip, fp, 1
         uxthhi lr, r1
         bichi r6, r6, r5
         movhi r2, r0
         rsbls r3, r0, r3
         andls r6, r6, r5
         (a, b) = compare (ip, 0xff)
         [r8] = (half) lr
         bls 0x3851c              // unlikely

         goto loc_0x00038598;
    loc_0x00038598: // orphan
         fp = [var_38h]           // r13
         
         goto loc_0x000385a0;
    loc_0x000385a0: // orphan
         // CODE XREF from fcn.00037adc @ 0x37f28(x)
         rsb r1, r0, r1
         rsb r3, r0, r3
         (a, b) = compare (r1, 0x1000000)
         sub ip, ip, ip, lsr 5
         lsllo r1, r1, 8
         [r2] = (half) ip
         ldrblo r0, [sb]
         addlo sb, sb, 1
         ip = (word) [r2 + 2]
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb
         r0 = ip * r0
         (a, b) = compare (r0, r3)
         bls 0x386b8              // likely

         goto loc_0x000385d8;
    loc_0x000385d8: // orphan
         rsb r1, ip, 0x800
         add r6, r2, r6, lsl 4
         r6 += 0x104
         sl = 0
         add ip, ip, r1, lsr 5
         r8 = 8
         [r2 + 2] = (half) ip
         
         goto loc_0x000385f8;
    loc_0x000385f8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3830c(x)
         r5 = lr
         lr = [var_18h]
         ip = lr + fp
         fp += r0                 // r13
         r0 = lr
         lr += fp
         r4 = r0

    loc_0x00038614: // orphan
         // CODE XREF from fcn.00037adc @ 0x3862c(x)
         r0 = (byte) [r4 + r1]
         r1 += 1
         (a, b) = compare (r5, r1)
         [ip] + 1 = (byte) r0
         moveq r1, 0
         (a, b) = compare (ip, lr)
         bne 0x38614              // likely

         goto loc_0x00038630;
    loc_0x00038630: // orphan
         
         goto loc_0x00038634;
    loc_0x00038634: // orphan
         // CODE XREF from fcn.00037adc @ 0x3843c(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         ip = lr + 0x30
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + r4] = (half) r0
         r0 = (word) [r1 + ip]
         ldrblo r1, [sb]
         addlo sb, sb, 1 // DATA XREFS from fcn.0007cae4 @ 0x7cc20(r), 0x7cc3c(r), 0x7cc48(r), 0x7cc4c(r), 0x7cc64(r)
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38708              // likely

         goto loc_0x00038674;
    loc_0x00038674: // orphan
         rsb r2, r0, 0x800
         lr = [var_10h]
         r4 = [var_34h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_34h] = lr
         [var_10h] = r4
         [r0 + ip] = (half) r2

    loc_0x00038694: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38704(x), 0x38770(x), 0x38994(x)
         r2 = [var_4h]
         (a, b) = compare (r2, 7)
         r2 = [var_ch]            // "@"
         r2 += 0xa60
         movlo r0, 8
         movhs r0, 0xb
         r2 += 8
         [var_4h] = r0
         
         goto loc_0x000386b8;
    loc_0x000386b8: // orphan
         // CODE XREF from fcn.00037adc @ 0x385d4(x)
         rsb r3, r0, r3
         sub ip, ip, ip, lsr 5
         rsb r0, r0, r1
         r6 = r2 + 0x204
         sl = ~0xef
         [r2 + 2] = (half) ip
         r8 = 0x100
         
         goto loc_0x000386d8;
    loc_0x000386d8: // orphan
         // CODE XREF from fcn.00037adc @ 0x3827c(x)
         r0 = [var_20h]
         (a, b) = compare (r0, ip)
         bhi 0x3828c              // likely

         return r0;
    loc_0x000386e4: // orphan
         // CODE XREFS from fcn.00037adc @ 0x38288(x), 0x38410(x)
         r0 = 1
         sp += 0x64
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x000386f0: // orphan
         // CODE XREF from fcn.00037adc @ 0x38484(x)
         lr = [var_ch]            // "@"
         sub ip, ip, ip, lsr 5
         rsb r1, r2, r1
         rsb r3, r2, r3
         [lr + r0] = (half) ip
         
         goto loc_0x00038708;
    loc_0x00038708: // orphan
         // CODE XREF from fcn.00037adc @ 0x38670(x)
         rsb r2, r1, r2
         rsb r3, r1, r3
         r1 = [var_ch]
         (a, b) = compare (r2, 0x1000000)
         lr += 0x48
         sub r0, r0, r0, lsr 5
         lsllo r2, r2, 8
         [r1 + ip] = (half) r0
         r0 = (word) [r1 + lr]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb
         r1 = r0 * r1
         (a, b) = compare (r1, r3)
         bls 0x38960              // likely

         goto loc_0x00038748;
    loc_0x00038748: // orphan
         r4 = [var_3ch]           // fcn.000bbf24
         rsb r2, r0, 0x800
         ip = [var_10h]           // fcn.000bbf24
         add r2, r0, r2, lsr 5
         r0 = [var_ch]
         [var_10h] = r4
         r4 = [var_34h]
         [r0 + lr] = (half) r2
         [var_34h] = ip
         [var_3ch] = r4
         
         goto loc_0x00038774;
    loc_0x00038774: // orphan
         // CODE XREF from fcn.00037adc @ 0x381d4(x)
         r1 -= 5

    loc_0x00038778: // orphan
         // CODE XREF from fcn.00037adc @ 0x387ac(x)
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ldrblo r0, [sb]
         addlo sb, sb, 1
         r2 = r2 >> 1
         orrlo r3, r0, r3, lsl 8
         r1 -= 1
         rsb r3, r2, r3
         r0 = r3 >> 0x1f
         add ip, r0, ip, lsl 1
         r0 &= r2
         ip += 1
         r3 = r0 + r3
         bne 0x38778              // likely

         goto loc_0x000387b0;
    loc_0x000387b0: // orphan
         r1 = [var_ch]            // "@"
         (a, b) = compare (r2, 0x1000000)
         lsllo r2, r2, 8
         ip = ip << 4
         r0 = r1 + 0x640
         r1 = sb
         r0 += 6
         ldrblo r1, [sb]
         lr = (word) [r0]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r2 >> 0xb           // 0x6c20 // "hatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r4, lr, 0x800
         subls lr, lr, lr, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r2
         addhi lr, lr, r4, lsr 5
         uxthls lr, lr
         orrls ip, ip, 1
         movls r2, 6
         uxthhi lr, lr
         [r0] = (half) lr
         r0 = [var_58h]
         movhi r2, 4
         (a, b) = compare (r1, 0x1000000)
         r4 = r0 + r2
         lsllo r1, r1, 8
         lr = (word) [r0 + r2]
         ldrblo r0, [sb]
         addlo sb, sb, 1
         orrlo r3, r0, r3, lsl 8
         r0 = r1 >> 0xb           // 0x6c20 // "hatEv"
         r0 = lr * r0             // 0x6c20 // "hatEv"
         (a, b) = compare (r0, r3)
         rsbhi r1, lr, 0x800
         subls lr, lr, lr, lsr 5
         addls r2, r2, 1
         rsbls r3, r0, r3
         addhi lr, lr, r1, lsr 5
         rsbls r0, r0, r1
         r1 = [var_58h]
         r2 = r2 << 1
         orrls ip, ip, 2
         uxthhi lr, lr
         uxthls lr, lr
         (a, b) = compare (r0, 0x1000000)
         [r4] = (half) lr
         lsllo r0, r0, 8
         lr = (word) [r1 + r2]
         ldrblo r1, [sb]
         addlo sb, sb, 1
         orrlo r3, r1, r3, lsl 8
         r1 = r0 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r1 = lr * r1
         (a, b) = compare (r1, r3)
         rsbhi r0, lr, 0x800
         addls r4, r2, 1
         subls lr, lr, lr, lsr 5
         movhi r4, r2
         addhi r0, lr, r0, lsr 5
         rsbls r3, r1, r3
         rsbls r1, r1, r0
         uxthls r0, lr
         lr = r4 << 1
         r4 = [var_58h]
         uxthhi r0, r0
         orrls ip, ip, 4
         (a, b) = compare (r1, 0x1000000)
         lsllo r1, r1, 8
         [r4 + r2] = (half) r0
         ldrblo r2, [sb]
         addlo sb, sb, 1
         r0 = (word) [r4 + lr]
         orrlo r3, r2, r3, lsl 8
         r2 = r1 >> 0xb           // 0x6c12 // "St9bad_alloc4whatEv"
         r2 = r0 * r2
         (a, b) = compare (r2, r3)
         rsbhi r1, r0, 0x800
         rsbls r3, r2, r3
         orrls ip, ip, 8
         rsbls r2, r2, r1
         addhi r0, r0, r1, lsr 5
         r1 = [var_58h]
         subls r0, r0, r0, lsr 5
         if (ip != 1)
         uxth r0, r0
         [r1 + lr] = (half) r0
         bne 0x38270              // likely

         goto loc_0x00038918;
    loc_0x00038918: // orphan
         r1 = [var_1ch]
         r0 = [var_4h]
         r1 += 0x110
         r0 -= 0xc
         r1 += 2
         [var_4h] = r0
         [var_1ch] = r1
         
         goto loc_0x00038938;
    loc_0x00038938: // orphan
         // CODE XREF from fcn.00037adc @ 0x38378(x)
         r1 = ip + r1
         lr = [var_54h]
         
         goto loc_0x00038944;
    loc_0x00038944: // orphan
         // CODE XREF from fcn.00037adc @ 0x38958(x)
         lr += 1

    loc_0x00038948: // orphan
         // CODE XREF from fcn.00037adc @ 0x38940(x)
         r4 = (byte) [r1] + 1
         (a, b) = compare (lr, r7)
         [ip] = (byte) r4
         ip = lr
         bne 0x38944              // unlikely

         goto loc_0x0003895c;
    loc_0x0003895c: // orphan
         
         goto loc_0x00038960;
    loc_0x00038960: // orphan
         // CODE XREF from fcn.00037adc @ 0x38744(x)
         rsb r3, r1, r3
         rsb r1, r1, r2
         r2 = [var_4ch]           // fcn.000bbf24
         sub r0, r0, r0, lsr 5
         ip = [var_10h]           // fcn.000bbf24
         [var_10h] = r2
         r2 = [var_3ch]
         [var_4ch] = r2
         r2 = [var_34h]           // fcn.000bbf24
         [var_34h] = ip
         [var_3ch] = r2
         r2 = [var_ch]            // "@"
         [r2 + lr] = (half) r0
         
         goto loc_0x00038998;
    loc_0x00038998: // orphan
         // CODE XREF from fcn.00037adc @ 0x37b1c(x)
         r1 = [r3 + 0x2c]
         r2 = [var_5ch]
         r3 = [r3 + 0xc]
         rsb r2, fp, r2
         rsb r3, r1, r3
         (a, b) = compare (r3, r2)
         strlo r1, [sp, 8]
         addlo r3, fp, r3
         strhs r1, [sp, 8]
         ldrhs r3, [sp, 0x5c]
         strlo r3, [sp, 0x14]
         strhs r3, [sp, 0x14]
         
}


========================================================
// Function at 0x0x389f8
========================================================
// callconv: r0 reg (r0, r1, r2, r3);
void fcn.000389f8 (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg_50h, int32_t arg_54h, int32_t arg_58h, int32_t arg_50h_2, int32_t arg_58h_2) {
        // CALL XREF from fcn.00051f60 @ 0x52008(x)
        push (r4, r5, r6, r7, r8, sb, sl, fp, lr)
        sp -= 0x2c
        ip = 0xf8cb
        sb = r0       // arg1
        [var_1ch] = r1 // arg2
        ip |= 0xffff << 16
        [var_18h] = r3 // arg4
        r1 = [arg_50h] // 0x178000 // r13
        r3 = [r2]     // arg3
        [var_24h] = r2
        r1 = [r1]
        [var_10h] = r3
        r3 = 0
        [r2] = r3
        r2 = [arg_50h] // 0x178000 // r13
        [var_20h] = ip
        [var_14h] = r1
        [r2] = r3
        
    loc_0x00038a40:
        // CODE XREF from fcn.000389f8 @ 0x38fdc(x)
        r3 = [sb + 0x24]
        r4 = [sb + 0x28]
        r2 = [var_10h]
        (a, b) = compare (r3, r4)
        moveq r3, 0
        streq r3, [sb, 0x24]
        [var_ch] = r3
        rsb r3, r3, r4
        (a, b) = compare (r2, r3)
        ldrls r3, [sp, 0xc]
        movhi r3, 0
        movhi r8, r4
        addls r8, r3, r2
        ldrls r3, [sp, 0x54]
        [var_8h] = r3
        r3 = [sb + 0x48]
        r2 = r3 - 1
        (a, b) = compare (r2, 0x110)
        bhi 0x38b04   // likely
        goto loc_0x00038a8c;
        return r0;
    loc_0x00038a8c:
        r2 = [var_ch]
        r5 = [sb + 0x14]
        rsb r1, r2, r8
        r2 = [sb + 0x30]
        (a, b) = compare (r1, r3)
        r0 = [sb + 0x38] // fcn.000bbf24
        movhs r1, r3
        (a, b) = compare (r2, 0)
        je 0x390a8    // unlikely
        goto loc_0x00038ab0;
    loc_0x000390a8:
        // CODE XREF from fcn.000389f8 @ 0x38aac(x)
        ip = [sb + 0xc]
        r2 = [sb + 0x2c]
        rsb lr, r2, ip
        (a, b) = compare (lr, r1)
        strls ip, [sb, 0x30]
        goto 0x38ab4
        
    loc_0x00038ab4:
        // CODE XREF from fcn.000389f8 @ 0x390bc(x)
        (a, b) = compare (r1, 0)
        r2 += r1
        rsb r3, r1, r3
        [sb + 0x2c] = r2
        [sb + 0x48] = r3
        je 0x39120    // likely
        goto loc_0x00038acc;
        return r0;
    loc_0x00038acc: // orphan
         r3 = [var_ch]
         r1 += r3
         ip = r5 + r3

    loc_0x00038ad8: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38afc(x)
         (a, b) = compare (r0, r3)
         rsb r2, r0, r3
         r2 = r5 + r2
         r3 += 1
         movhi lr, r4
         movls lr, 0
         r2 = (byte) [r2 + lr]
         (a, b) = compare (r3, r1)
         [ip] + 1 = (byte) r2
         bne 0x38ad8              // likely

         goto loc_0x00038b00;
    loc_0x00038b00: // orphan
         // CODE XREF from fcn.000389f8 @ 0x39124(x)
         [sb + 0x24] = r1

    loc_0x00038b04: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38a88(x)
         r3 = [arg_58h]
         r7 = 0
         r4 = [var_14h]
         r5 = [var_18h]
         [r3] = r7

    loc_0x00038b18: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38f0c(x)
         r1 = [sb + 0x48]
         r3 = 0x112
         (a, b) = compare (r1, r3)
         je 0x38da0               // unlikely

         goto loc_0x00038b28;
    loc_0x00038b28: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38d9c(x)
         r3 = [sb + 0x4c]
         (a, b) = compare (r3, 0)
         je 0x38c60               // unlikely

         goto loc_0x00038b34;
    loc_0x00038b34: // orphan
         (a, b) = compare (r4, 0)
         je 0x390cc               // likely

         goto loc_0x00038b3c;
    loc_0x00038b3c: // orphan
         r2 = [sb + 0x58]
         (a, b) = compare (r2, 4)
         bhi 0x38c20              // unlikely

         goto loc_0x00038b48;
    loc_0x00038b48: // orphan
         r3 = r2 + 1
         [sb + 0x58] = r3
         lr = (byte) [r5]
         r0 = sb + r2
         r6 = r4 - 1
         ip = r5 + 1
         [r0 + 0x5c] = (byte) lr
         r0 = r7 + 1
         je 0x38f10               // unlikely

         goto loc_0x00038b6c;
    loc_0x00038b6c: // orphan
         (a, b) = compare (r3, 5)
         je 0x39088               // unlikely

         goto loc_0x00038b74;
    loc_0x00038b74: // orphan
         r0 = r2 + 2
         [sb + 0x58] = r0
         lr = (byte) [r5 + 1]
         r3 = sb + r3
         r6 = r4 - 2
         ip = r5 + 2
         [r3 + 0x5c] = (byte) lr
         r3 = r7 + 2
         je 0x390e0               // unlikely

         goto loc_0x00038b98;
    loc_0x00038b98: // orphan
         (a, b) = compare (r0, 5)
         je 0x390d4               // unlikely

         goto loc_0x00038ba0;
    loc_0x00038ba0: // orphan
         r3 = r2 + 3
         [sb + 0x58] = r3
         lr = (byte) [r5 + 2]
         r0 = sb + r0
         r6 = r4 - 3
         ip = r5 + 3
         [r0 + 0x5c] = (byte) lr
         r0 = r7 + 3
         je 0x38f10               // unlikely

         goto loc_0x00038bc4;
    loc_0x00038bc4: // orphan
         (a, b) = compare (r3, 5)
         je 0x39088               // unlikely

         goto loc_0x00038bcc;
    loc_0x00038bcc: // orphan
         r2 += 4
         [sb + 0x58] = r2
         r0 = (byte) [r5 + 3]
         r3 = sb + r3
         lr = r4 - 4
         ip = r5 + 4
         [r3 + 0x5c] = (byte) r0
         r3 = r7 + 4
         je 0x390fc               // unlikely

         goto loc_0x00038bf0;
    loc_0x00038bf0: // orphan
         (a, b) = compare (r2, 4)
         bne 0x390f0              // likely

         goto loc_0x00038bf8;
    loc_0x00038bf8: // orphan
         r3 = 5
         [sb + 0x58] = r3
         r3 = (byte) [r5 + 4]
         r4 -= 5
         ip = r5 + 5
         r7 += 5
         [sb + 0x60] = (byte) r3
         moveq r5, ip
         je 0x38f20               // unlikely

         goto loc_0x00038c1c;
    loc_0x00038c1c: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x39090(x), 0x390dc(x), 0x390f8(x)
         r5 = ip

    loc_0x00038c20: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b44(x)
         r2 = (byte) [sb + 0x5c]
         (a, b) = compare (r2, 0)
         bne 0x38f30              // unlikely

         goto loc_0x00038c2c;
    loc_0x00038c2c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38f2c(x)
         r3 = (byte) [sb + 0x5e]
         ip = ~0
         r0 = (byte) [sb + 0x5d]
         [sb + 0x4c] = r2
         r3 = r3 << 0x10
         [sb + 0x58] = r2
         r3 |= r0
         r2 = (byte) [sb + 0x60]
         r0 = (byte) [sb + 0x5f]
         r3 |= r2
         [sb + 0x1c] = ip
         r3 |= r0
         [sb + 0x20] = r3

    loc_0x00038c60: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b30(x)
         r3 = [sb + 0x24]
         (a, b) = compare (r8, r3)
         bhi 0x38dc4              // unlikely

         goto loc_0x00038c6c;
    loc_0x00038c6c: // orphan
         (a, b) = compare (r1, 0)
         bne 0x38f38              // unlikely

         goto loc_0x00038c74;
    loc_0x00038c74: // orphan
         r3 = [sb + 0x20]
         (a, b) = compare (r3, 0)
         je 0x39128               // unlikely

         goto loc_0x00038c80;
    loc_0x00038c80: // orphan
         r3 = [var_8h]
         (a, b) = compare (r3, 0)
         je 0x3910c               // likely

         goto loc_0x00038c8c;
    loc_0x00038c8c: // orphan
         r3 = [sb + 0x50]
         sl = 1
         (a, b) = compare (r3, 0)
         bne 0x38dd4              // likely

         goto loc_0x00038c9c;
    loc_0x00038c9c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38dd0(x)
         r6 = [sb + 0x58]
         (a, b) = compare (r6, 0)
         je 0x38ec4               // likely

         goto loc_0x00038ca8;
    loc_0x00038ca8: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38ec0(x)
         (a, b) = compare (r6, 0x13)
         movhi r2, 0
         movls r2, 1
         (a, b) = compare (r4, 0)
         moveq fp, 0
         andne fp, r2, 1
         (a, b) = compare (fp, 0)
         addne r3, r6, 0x5b
         subne r1, r5, 1
         addne r3, sb, r3
         movne fp, 0
         je 0x38d08               // unlikely

         goto loc_0x00038cd8;
    loc_0x00038cd8: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38d04(x)
         r6 += 1
         fp += 1
         (a, b) = compare (r6, 0x13)
         r0 = (byte) [r1 + 1]!
         movhi r2, 0
         movls r2, 1
         (a, b) = compare (r4, fp)
         movls ip, 0
         andhi ip, r2, 1
         [r3 + 1]! = (byte) r0
         (a, b) = compare (ip, 0)
         bne 0x38cd8              // unlikely

         goto loc_0x00038d08;
    loc_0x00038d08: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38cd4(x)
         orrs r3, r2, sl
         [sb + 0x58] = r6
         r3 = sb + 0x5c
         je 0x38d4c               // likely

         goto loc_0x00038d18;
    loc_0x00038d18: // orphan
         r1 = r3
         r0 = sb
         r2 = r6
         [var_4h] = r3
         fcn.0003751c ()          // fcn.0003751c(0x0, 0x0, 0x0)
         r3 = [var_24h]
         (a, b) = compare (r0, 0)
         je 0x3913c               // likely

         goto loc_0x00038d38;
    loc_0x00038d38: // orphan
         (a, b) = compare (r0, 2)
         moveq r0, 0
         andne r0, sl, 1
         (a, b) = compare (r0, 0)
         bne 0x38f44              // unlikely

         goto loc_0x00038d4c;
    loc_0x00038d4c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38d14(x)
         [sb + 0x18] = r3
         r0 = sb
         r1 = r8
         r2 = r3
         [var_4h] = r3
         fcn.00037adc ()          // fcn.00037adc(0x0, 0x0, 0x0, 0x0, 0x0)
         (a, b) = compare (r0, 0)
         bne 0x38f30              // unlikely

         goto loc_0x00038d6c;
    loc_0x00038d6c: // orphan
         r2 = [sb + 0x18]
         r3 = [var_24h]
         r1 = [sb + 0x48]
         rsb r3, r2, r3
         [sb + 0x58] = r0
         rsb fp, r3, fp           // r13
         r3 = 0x112
         (a, b) = compare (r1, r3)
         rsb r6, r6, fp           // r13
         r7 += r6                 // r13
         r5 += r6                 // r13
         rsb r4, r6, r4
         bne 0x38b28              // likely

         goto loc_0x00038da0;
    loc_0x00038da0: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b24(x)
         r1 = [sb + 0x20]
         (a, b) = compare (r1, 0)
         ldreq r2, [sp, 0x58]
         moveq r3, 1
         streq r3, [r2]
         ldreq r1, [sb, 0x20]
         r5 = r1 + 0              // 0xc324c // elf_shdr
         movne r5, 1
         
         goto loc_0x00038dc4;
    loc_0x00038dc4: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38c68(x)
         r3 = [sb + 0x50]
         sl = 0
         (a, b) = compare (r3, 0)
         je 0x38c9c               // unlikely

         goto loc_0x00038dd4;
    loc_0x00038dd4: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38c98(x)
         r2 = [sb + 4]
         r3 = 0x300
         r1 = [sb]
         ip = [sb + 0x10]
         r2 += r1
         r2 = r3 << r2
         r3 = r2 + 0x730
         r3 += 6
         (a, b) = compare (r3, 0)
         je 0x38e98               // unlikely

         goto loc_0x00038dfc;
    loc_0x00038dfc: // orphan
         ubfx r1, ip, 1, 1
         (a, b) = compare (r3, r1)
         movlo r0, r3
         movhs r0, r1
         (a, b) = compare (r3, 6)
         bhi 0x39020              // unlikely

         goto loc_0x00038e14;
    loc_0x00038e14: // orphan
         (a, b) = compare (r3, 1)
         r1 = 0x400
         [ip] = (half) r1
         je 0x3907c               // unlikely

         goto loc_0x00038e24;
    loc_0x00038e24: // orphan
         (a, b) = compare (r3, 2)
         [ip + 2] = (half) r1
         je 0x3907c               // unlikely

         goto loc_0x00038e30;
    loc_0x00038e30: // orphan
         (a, b) = compare (r3, 3)
         [ip + 4] = (half) r1
         je 0x3907c               // unlikely

         goto loc_0x00038e3c;
    loc_0x00038e3c: // orphan
         (a, b) = compare (r3, 4)
         [ip + 6] = (half) r1
         je 0x3907c               // unlikely

         goto loc_0x00038e48;
    loc_0x00038e48: // orphan
         (a, b) = compare (r3, 6)
         [ip + 8] = (half) r1
         bne 0x390c0              // likely

         goto loc_0x00038e54;
    loc_0x00038e54: // orphan
         r0 = r3
         r6 = r3
         [ip + 0xa] = (half) r1

    loc_0x00038e60: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x39034(x), 0x39084(x), 0x390c8(x)
         (a, b) = compare (r3, r0)
         je 0x38e98               // likely

         goto loc_0x00038e68;
    loc_0x00038e68: // orphan
         rsb r3, r0, r3
         r1 = [var_20h]
         lr = r3 - 2
         rsb r2, r0, r2
         (a, b) = compare (r2, r1)
         lr = lr >> 1
         lr += 1
         fp = lr << 1
         bne 0x3904c              // unlikely

         goto loc_0x00038e8c;
    loc_0x00038e8c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x39074(x)
         r6 = r6 << 1
         r3 = 0x400
         [ip + r6] = (half) r3

    loc_0x00038e98: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38df8(x), 0x38e64(x), 0x39078(x)
         r6 = [sb + 0x58]
         r3 = 1
         r2 = 0
         [sb + 0x44] = r3
         (a, b) = compare (r6, 0)
         [sb + 0x40] = r3
         [sb + 0x3c] = r3
         [sb + 0x38] = r3
         [sb + 0x34] = r2
         [sb + 0x50] = r2
         bne 0x38ca8              // unlikely

         goto loc_0x00038ec4;
    loc_0x00038ec4: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38ca4(x)
         (a, b) = compare (r4, 0x13)
         movhi r3, sl
         orrls r3, sl, 1
         (a, b) = compare (r3, 0)
         subeq r2, r4, 0x14
         addeq r2, r5, r2
         bne 0x38fec              // likely

         goto loc_0x00038ee0;
    loc_0x00038ee0: // orphan
         // CODE XREF from fcn.000389f8 @ 0x3901c(x)
         [sb + 0x18] = r5
         r0 = sb
         r1 = r8
         fcn.00037adc ()          // fcn.00037adc(0x0, 0x0, 0x0, 0x0, 0x0)
         (a, b) = compare (r0, 0)
         bne 0x38f30              // unlikely

         goto loc_0x00038ef8;
    loc_0x00038ef8: // orphan
         r3 = [sb + 0x18]
         rsb r5, r5, r3
         r7 += r5
         rsb r4, r5, r4
         r5 = r3
         
         goto loc_0x00038f10;
    loc_0x00038f10: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38b68(x), 0x38bc0(x)
         r7 = r0
         r5 = ip

    loc_0x00038f18: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x390d0(x), 0x390ec(x), 0x39108(x)
         (a, b) = compare (r3, 4)
         bls 0x39094              // likely

         goto loc_0x00038f20;
    loc_0x00038f20: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38c18(x)
         r2 = (byte) [sb + 0x5c]
         r4 = 0
         (a, b) = compare (r2, 0)
         je 0x38c2c               // likely

         goto loc_0x00038f30;
    loc_0x00038f30: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38c28(x), 0x38d68(x), 0x38ef4(x)
         r5 = 1
         
         goto loc_0x00038f38;
    loc_0x00038f38: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38c70(x)
         r3 = [var_8h]
         (a, b) = compare (r3, 0)
         je 0x3910c               // likely

         goto loc_0x00038f44;
    loc_0x00038f44: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38d48(x), 0x39014(x)
         r2 = [arg_58h_2]
         r5 = 1
         r3 = 2 // DATA XREF from fcn.0008128c @ 0x81704(r)
         [r2] = r3

    loc_0x00038f54: // orphan
         // XREFS: CODE 0x00038dc0  CODE 0x00038f34  CODE 0x000390a4   // XREFS: CODE 0x0003911c  CODE 0x00039138  CODE 0x00039150   // XREFS: CODE 0x00039190  
         r3 = [arg_50h]           // 0x178000 // r13
         r2 = [var_18h]
         ip = [var_1ch]
         r3 = [r3]
         r2 += r7
         [var_18h] = r2
         r2 = [var_14h]
         r0 = ip
         rsb r2, r7, r2
         r7 = r3 + r7
         r3 = [arg_50h]           // 0x178000 // r13
         [var_14h] = r2 // DATA XREF from fcn.0008128c @ 0x816d0(r)
         [r3] = r7
         r3 = [var_ch]
         r4 = [sb + 0x24]
         r1 = [sb + 0x14]
         rsb r4, r3, r4
         r1 += r3
         r3 = [var_10h]
         r2 = r4
         rsb r3, r4, r3
         [var_10h] = r3
         r6 = r3
         r3 = ip + r4
         [var_1ch] = r3
         sym.imp.memcpy ()
         r2 = [var_24h]
         (a, b) = compare (r5, 0)
         r3 = [r2]
         r3 += r4
         [r2] = r3
         bne 0x39154              // unlikely

         goto loc_0x00038fd4;
    loc_0x00038fd4: // orphan
         (a, b) = compare (r4, 0)
         cmpne r6, 0
         bne 0x38a40              // unlikely

         return r0;
    loc_0x00038fe0: // orphan
         r0 = r5
         sp += 0x2c
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00038fec: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38edc(x)
         r0 = sb
         r1 = r5
         r2 = r4
         fcn.0003751c ()          // fcn.0003751c(0x0, 0x0, 0x0)
         (a, b) = compare (r0, 0)
         je 0x39164               // likely

         goto loc_0x00039004;
    loc_0x00039004: // orphan
         (a, b) = compare (r0, 2)
         moveq r3, 0
         andne r3, sl, 1
         (a, b) = compare (r3, 0)
         bne 0x38f44              // unlikely

         goto loc_0x00039018;
    loc_0x00039018: // orphan
         r2 = r5
         
         goto loc_0x00039020;
    loc_0x00039020: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38e10(x)
         (a, b) = compare (r0, 0)
         movne r0, 1
         movne r1, 0x400
         movne r6, r0
         strhne r1, [ip]
         bne 0x38e60              // unlikely

         goto loc_0x00039038;
    loc_0x00039038: // orphan
         lr = r3 - 2
         r6 = r0
         lr = lr >> 1
         lr += 1
         fp = lr << 1

    loc_0x0003904c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38e88(x)
         add r0, ip, r0, lsl 1
         r1 = 0

    loc_0x00039054: // orphan
         // CODE XREF from fcn.000389f8 @ 0x39068(x)
         r1 += 1
         r2 = 0x400
         (a, b) = compare (r1, lr)
         bfi r2, r2, 0x10, 0x10
         [r0] + 4 = r2
         blo 0x39054              // unlikely

         goto loc_0x0003906c;
    loc_0x0003906c: // orphan
         (a, b) = compare (fp, r3)
         r6 += fp                 // r13
         bne 0x38e8c              // likely

         goto loc_0x00039078;
    loc_0x00039078: // orphan
         
         goto loc_0x0003907c;
    loc_0x0003907c: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38e20(x), 0x38e2c(x), 0x38e38(x), 0x38e44(x)
         r0 = r3
         r6 = r3
         
         goto loc_0x00039088;
    loc_0x00039088: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38b70(x), 0x38bc8(x)
         r4 = r6
         r7 = r0
         
         goto loc_0x00039094;
    loc_0x00039094: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38f1c(x)
         r2 = [arg_58h]
         r3 = 3
         r5 = 0
         [r2] = r3
         
         goto loc_0x000390a8;
    loc_0x000390c0: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38e50(x)
         r0 = r3
         r6 = 5
         
         goto loc_0x000390cc;
    loc_0x000390cc: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b38(x)
         r3 = [sb + 0x58]
         
         goto loc_0x000390d4;
    loc_0x000390d4: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b9c(x)
         r4 = r6
         r7 = r3
         
         goto loc_0x000390e0;
    loc_0x000390e0: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38b94(x)
         r7 = r3
         r5 = ip
         r3 = r0
         
         goto loc_0x000390f0;
    loc_0x000390f0: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38bf4(x)
         r4 = lr
         r7 = r3
         
         goto loc_0x000390fc;
    loc_0x000390fc: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38bec(x)
         r7 = r3
         r5 = ip
         r3 = r2
         
         goto loc_0x0003910c;
    loc_0x0003910c: // orphan
         // CODE XREFS from fcn.000389f8 @ 0x38c88(x), 0x38f40(x)
         r2 = [arg_58h]
         r3 = 2
         r5 = 0
         [r2] = r3
         
         goto loc_0x00039120;
    loc_0x00039120: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38ac8(x)
         r1 = [var_ch]
         
         goto loc_0x00039128;
    loc_0x00039128: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38c7c(x)
         r2 = [arg_58h]
         r3 = 4
         r5 = r1
         [r2] = r3
         
         goto loc_0x0003913c;
    loc_0x0003913c: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38d34(x)
         r2 = [arg_58h_2]
         r3 = 3
         r7 += fp                 // r13
         r5 = r0
         [r2] = r3
         
         return r0;
    loc_0x00039154: // orphan
         // CODE XREF from fcn.000389f8 @ 0x38fd0(x)
         r5 = 1
         r0 = r5
         sp += 0x2c
         pop (r4, r5, r6, r7, r8, sb, sl, fp, pc)

    loc_0x00039164: // orphan
         // CODE XREF from fcn.000389f8 @ 0x39000(x)
         r3 = r0
         r1 = r5
         r2 = r4
         r0 = sb + 0x5c
         r5 = r3
         sym.imp.memcpy ()
         r2 = [arg_58h_2]
         r3 = 3
         [sb + 0x58] = r4
         r7 += r4
         [r2] = r3
         
}


