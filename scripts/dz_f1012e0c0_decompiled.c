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
void fcn.1012e0c0 (int32_t arg1, int32_t arg_8h) {
        // CALL XREF from fcn.100d7f40 @ 0x100d82eb(x)
        // CALL XREFS from fcn.1012ac10 @ 0x1012afb6(x), 0x1012b0fe(x)
        // CALL XREF from fcn.10130b00 @ 0x10130c23(x)
        // CALL XREF from fcn.101324b0 @ +0x746(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x103196eb)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x78
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        edi = ecx     // arg3
        eax = dword [arg_8h]
        push (dword [eax])
        push (str.Hack_error:__s) // 0x105af428 // "Hack error: %s" // (pstr 0x105af428) "Hack error: %s"
        fcn.101471b0 () // fcn.101471b0(0x0, 0x0)
        push (ecx)
        esi = esp
        dword [arg_8h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        push (0x11)   // 17
        dword [var_4h] = 0
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e135 // unlikely
        goto loc_0x1012e129;
    loc_0x1012e135:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e127(x)
        push (0x10)   // 16
        eax = ecx + 0x11
        push (str.assets_users.xml) // 0x1038aa7c // "assets/users.xml" // (pstr 0x1038aa7c) "assets/users.xml"
        push (ecx)
        dword [esi] = ecx
        dword [esi + 4] = ecx
        dword [esi + 8] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0x10   // 16
        dword [esi + 4] = eax
        byte [eax] = 0
        esp += 0xc
        eax = var_54h
        push (eax)
        dword [var_4h] = 0xffffffff // -1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177fac, 0x0, 0x0)
        push (dword [eax])
        dword [var_4h] = 1
        fcn.102fc110 () // fcn.102fc110(0x177fac)
        eax = dword [var_54h]
        esp += 0x14   // ebp
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x1012e191 // likely
        goto loc_0x1012e188;
    loc_0x1012e191:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e186(x)
        esp -= 0xc
        esi = esp
        dword [arg_8h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        push (0x11)   // 17
        dword [var_4h] = 2
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e1d0 // unlikely
        goto loc_0x1012e1c4;
    loc_0x1012e1d0:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e1c2(x)
        push (0x10)   // 16
        eax = ecx + 0x11
        push (str.assets_users.xml) // 0x1038aa7c // "assets/users.xml" // (pstr 0x1038aa7c) "assets/users.xml"
        push (ecx)
        dword [esi] = ecx
        dword [esi + 4] = ecx
        dword [esi + 8] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0x10   // 16
        dword [esi + 4] = eax
        byte [eax] = 0
        esp += 0xc
        eax = var_60h
        push (eax)
        dword [var_4h] = 0xffffffff // -1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177fa0, 0x0, 0x0)
        push (dword [eax])
        eax = var_54h // int32_t arg1
        push (eax)
        dword [var_4h] = 3
        fcn.100f8ed0 () // fcn.100f8ed0(0x177fac, 0x0, 0x0, 0x0)
        push (dword [eax])
        byte [var_4h] = 4
        fcn.102fc110 () // fcn.102fc110(0x177fac)
        eax = dword [var_54h]
        esp += 0x1c   // (pstr 0x00000004) "!"
        byte [var_4h] = 3
        v = eax & eax
        if (!v) goto loc_0x1012e238 // likely
        goto loc_0x1012e22f;
    loc_0x1012e238:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e22d(x)
        eax = dword [var_60h]
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x1012e24f // likely
        goto loc_0x1012e246;
    loc_0x1012e24f:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e244(x)
        esp -= 0xc
        esi = esp
        dword [arg_8h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        push (0x11)   // 17
        dword [var_4h] = 5
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e28e // unlikely
        goto loc_0x1012e282;
    loc_0x1012e28e:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e280(x)
        push (0x10)   // 16
        eax = ecx + 0x11
        push (str.assets_packs.xml) // 0x1038aab8 // "assets/packs.xml" // (pstr 0x1038aab8) "assets/packs.xml"
        push (ecx)
        dword [esi] = ecx
        dword [esi + 4] = ecx
        dword [esi + 8] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0x10   // 16
        dword [esi + 4] = eax
        byte [eax] = 0
        esp += 0xc
        eax = var_60h
        push (eax)
        dword [var_4h] = 0xffffffff // -1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177fa0, 0x0, 0x0)
        push (dword [eax])
        dword [var_4h] = 6
        fcn.102fc110 () // fcn.102fc110(0x177fa0)
        eax = dword [var_60h]
        esp += 0x14   // ebp
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x1012e2ea // likely
        goto loc_0x1012e2e1;
    loc_0x1012e2ea:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e2df(x)
        esp -= 0xc
        esi = esp
        dword [arg_8h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        push (0x11)   // 17
        dword [var_4h] = 7
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e329 // unlikely
        goto loc_0x1012e31d;
    loc_0x1012e329:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e31b(x)
        push (0x10)   // 16
        eax = ecx + 0x11
        push (str.assets_packs.xml) // 0x1038aab8 // "assets/packs.xml" // (pstr 0x1038aab8) "assets/packs.xml"
        push (ecx)
        dword [esi] = ecx
        dword [esi + 4] = ecx
        dword [esi + 8] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0x10   // 16
        dword [esi + 4] = eax
        byte [eax] = 0
        esp += 0xc
        eax = var_54h
        push (eax)
        dword [var_4h] = 0xffffffff // -1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177fac, 0x0, 0x0)
        push (dword [eax])
        eax = var_60h // int32_t arg1
        push (eax)
        dword [var_4h] = 8
        fcn.100f8ed0 () // fcn.100f8ed0(0x177fa0, 0x0, 0x0, 0x0)
        push (dword [eax])
        byte [var_4h] = 9
        fcn.102fc110 () // fcn.102fc110(0x177fa0)
        eax = dword [var_60h]
        esp += 0x1c
        byte [var_4h] = 8
        v = eax & eax
        if (!v) goto loc_0x1012e391 // likely
        goto loc_0x1012e388;
    loc_0x1012e391:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e386(x)
        eax = dword [var_54h]
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x1012e3a8 // likely
        goto loc_0x1012e39f;
    loc_0x1012e3a8:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e39d(x)
        push (1)      // 1
        fcn.100f5ab0 () // fcn.100f5ab0(0x0, 0x0, 0x0)
        esp += 4
        ecx = edi
        fcn.10139270 () // fcn.10139270(0x0, 0x0)
        push (0)
        fcn.100f93f0 () // fcn.100f93f0(0x0)
        dword [var_48h] = 0
        dword [var_44h] = 0
        dword [var_40h] = 0
        push (1)      // 1
        dword [var_4h] = 0xa
        fcn.102f7820 ()
        ecx = eax
        esp += 8
        v = ecx & ecx
        if (v) goto loc_0x1012e3f8 // unlikely
        goto loc_0x1012e3ec;
    loc_0x1012e3f8:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e3ea(x)
        eax = ecx + 1
        dword [var_48h] = ecx
        dword [var_40h] = eax
        dword [var_44h] = ecx
        byte [ecx] = 0
        dword [var_4h] = 0xb // 11
        dword [var_3ch] = 0
        dword [var_38h] = 0
        dword [var_34h] = 0
        push (1)      // 1
        byte [var_4h] = 0xc
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e443 // likely
        goto loc_0x1012e437;
    loc_0x1012e443:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e435(x)
        eax = ecx + 1
        dword [var_3ch] = ecx
        dword [var_34h] = eax
        dword [var_38h] = ecx
        byte [ecx] = 0
        dword [var_30h] = 0
        dword [var_2ch] = 0
        dword [var_28h] = 0
        push (0xb)    // 11
        byte [var_4h] = 0xe
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x1012e487 // likely
        goto loc_0x1012e47b;
    loc_0x1012e487:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e479(x)
        push (0xa)    // 10
        eax = ecx + 0xb
        push (str.HackButton) // 0x105af438 // "HackButton" // (pstr 0x105af438) "HackButton"
        push (ecx)
        dword [var_30h] = ecx
        dword [var_2ch] = ecx
        dword [var_28h] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0xa
        dword [var_2ch] = eax
        byte [eax] = 0
        dword [var_24h] = 0
        dword [var_20h] = 0
        dword [var_1ch] = 0
        push (0xc)    // 12
        byte [var_4h] = 0x10
        fcn.102f7820 ()
        ecx = eax
        esp += 0x10   // (pstr 0x105af438) "HackButton"
        v = ecx & ecx
        if (v) goto loc_0x1012e4de // likely
        goto loc_0x1012e4d2;
    loc_0x1012e4de:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e4d0(x)
        push (0xb)    // 11
        eax = ecx + 0xc
        push (str.HackMessage) // 0x105af444 // "HackMessage" // (pstr 0x105af444) "HackMessage"
        push (ecx)
        dword [var_24h] = ecx
        dword [var_20h] = ecx
        dword [var_1ch] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0xb    // 11
        dword [var_20h] = eax
        byte [eax] = 0
        dword [var_18h] = 0
        dword [var_14h] = 0
        dword [var_10h] = 0
        push (0xa)
        byte [var_4h] = 0x12
        fcn.102f7820 ()
        ecx = eax
        esp += 0x10   // (pstr 0x105af444) "HackMessage"
        v = ecx & ecx
        if (v) goto loc_0x1012e535 // likely
        goto loc_0x1012e529;
    loc_0x1012e535:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e527(x)
        push (9)      // 9
        eax = ecx + 0xa
        push (str.HackTitle) // 0x105af450 // "HackTitle" // (pstr 0x105af450) "HackTitle"
        push (ecx)
        dword [var_18h] = ecx
        dword [var_14h] = ecx
        dword [var_10h] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 9
        dword [var_14h] = eax
        byte [eax] = 0
        push (0)
        eax = var_30h
        push (eax)
        eax = var_84h
        push (eax)
        byte [var_4h] = 0x13
        fcn.10140f60 () // fcn.10140f60(0x177f7c, 0x0, 0x0, 0x0, 0x0)
        edi = eax
        push (0)
        eax = var_24h
        push (eax)
        eax = var_78h
        push (eax)
        byte [var_4h] = 0x14
        fcn.10140f60 () // fcn.10140f60(0x177f88, 0x0, 0x0, 0x0, 0x0)
        esi = eax
        push (0)
        eax = var_18h
        push (eax)
        eax = var_6ch
        push (eax)
        byte [var_4h] = 0x15
        fcn.10140f60 () // fcn.10140f60(0x177f94, 0x0, 0x0, 0x0, 0x177f88)
        ecx = var_48h
        push (ecx)
        ecx = var_3ch
        push (ecx)
        push (0)
        push (0x1012e0a0)
        push (edi)
        push (esi)
        push (eax)
        byte [var_4h] = 0x16
        fcn.1009c380 () // fcn.1009c380(0x177f94, 0x0, 0x177fc4, 0x0, 0x177f88, 0x177f7c, 0x0, 0x0)
        eax = dword [var_6ch]
        esp += 0x4c
        byte [var_4h] = 0x15
        v = eax & eax
        if (!v) goto loc_0x1012e5c9 // likely
        goto loc_0x1012e5c0;
    loc_0x1012e5c9:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e5be(x)
        eax = dword [var_78h]
        byte [var_4h] = 0x14
        v = eax & eax
        if (!v) goto loc_0x1012e5dd // likely
        goto loc_0x1012e5d4;
    loc_0x1012e5dd:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e5d2(x)
        eax = dword [var_84h]
        byte [var_4h] = 0x13
        v = eax & eax
        if (!v) goto loc_0x1012e5f4 // likely
        goto loc_0x1012e5eb;
    loc_0x1012e5f4:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e5e9(x)
        eax = dword [var_18h]
        byte [var_4h] = 0x11
        v = eax & eax
        if (!v) goto loc_0x1012e608 // unlikely
        goto loc_0x1012e5ff;
    loc_0x1012e608:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e5fd(x)
        eax = dword [var_24h]
        byte [var_4h] = 0xf
        v = eax & eax
        if (!v) goto loc_0x1012e61c // likely
        goto loc_0x1012e613;
    loc_0x1012e61c:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e611(x)
        eax = dword [var_30h]
        byte [var_4h] = 0xd
        v = eax & eax
        if (!v) goto loc_0x1012e630 // unlikely
        goto loc_0x1012e627;
    loc_0x1012e630:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e625(x)
        eax = dword [var_3ch]
        byte [var_4h] = 0xb
        v = eax & eax
        if (!v) goto loc_0x1012e644 // unlikely
        goto loc_0x1012e63b;
    loc_0x1012e644:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e639(x)
        eax = dword [var_48h]
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x1012e65b // unlikely
        goto loc_0x1012e652;
    loc_0x1012e65b:
        // CODE XREF from fcn.1012e0c0 @ 0x1012e650(x)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        esp = ebp
        ebp = pop ()
        return
        return eax;
    loc_0x1012e188: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e1c4: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e22f: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e246: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e282: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e2e1: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e31d: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e388: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e39f: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e3ec: // orphan
         push (1)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e437: // orphan
         push (1)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e47b: // orphan
         push (0xb)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e4d2: // orphan
         push (0xc)               // 12
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e529: // orphan
         push (0xa)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x1012e5c0: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e5d4: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e5eb: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e5ff: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e613: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e627: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e63b: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x1012e652: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

