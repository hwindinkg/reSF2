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
void fcn.100d8680 (int32_t arg1) {
        // CALL XREF from method.StageSF.virtual_328 @ 0x10231b97(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x10311157) // 'W\x111\x10'
        eax = dword fs:[0]
        push (eax)
        esp -= 0x12c
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_10h] = eax
        push (ebx)    // arg2
        push (esi)
        push (edi)
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        esp -= 0xc
        esi = esp
        dword [var_ech] = esp
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [var_e8h] = esi
        dword [esi + 8] = 0
        push (4)      // 4
        dword [var_4h] = 0
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x100d86f6 // likely
        goto loc_0x100d86ea;
    loc_0x100d86f6:
        // CODE XREF from fcn.100d8680 @ 0x100d86e8(x)
        push (3)      // 3
        eax = ecx + 4
        push (0x10377338) // '8s7\x10' // "://" // (pstr 0x10377338) "://"
        push (ecx)
        dword [esi] = ecx
        dword [esi + 4] = ecx
        dword [esi + 8] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 3
        dword [esi + 4] = eax
        byte [eax] = 0
        esi = esp
        dword [var_4h] = 1
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [var_e8h] = esi
        dword [esi + 8] = 0
        push (0x11)   // 17
        byte [var_4h] = 2
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x100d875a // likely
        goto loc_0x100d874e;
    loc_0x100d875a:
        // CODE XREF from fcn.100d8680 @ 0x100d874c(x)
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
        eax = var_e4h
        push (eax)
        byte [var_4h] = 1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177f1c, 0x0, 0x0)
        edx = dword [eax]
        esp += 4
        esi = esp
        dword [var_e8h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        ecx = edx
        byte [var_4h] = 4
        edi = ecx + 1
        
    do {
        // CODE XREF from fcn.100d8680 @ 0x100d87bd(x)
        al = byte [ecx]
        ecx++
        v = al & al
    } while (al & al);
    loc_0x100d87bf:
        ecx -= edi
        eax = ecx + edx
        push (eax)
        push (edx)
        ecx = esi
        fcn.1000cd20 () // fcn.1000cd20(0x0, 0x0, 0x0, 0x0)
        byte [var_4h] = 3
        byte [var_4h] = 5
        fcn.1009d770 () // fcn.1009d770(0x0)
        eax = dword [var_e4h]
        esp += 0x18
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x100d87f7 // likely
        goto loc_0x100d87ee;
    loc_0x100d87f7:
        // CODE XREF from fcn.100d8680 @ 0x100d87ec(x)
        eax = var_11ch
        push (eax)
        byte [var_edh] = 0
        fcn.100d7d60 () // fcn.100d7d60(0x177ee4, 0x0)
        esp += 4
        ecx = var_d8h
        fcn.101ba8e0 () // fcn.101ba8e0(0x177ee4)
        esp -= 0xc
        esi = esp
        dword [var_4h] = 6
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [var_ech_2] = esi
        dword [esi + 8] = 0
        push (0x11)   // 17
        byte [var_4h] = 7
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x100d885e // likely
        goto loc_0x100d8852;
    loc_0x100d885e:
        // CODE XREF from fcn.100d8680 @ 0x100d8850(x)
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
        eax = var_e4h
        push (eax)
        byte [var_4h] = 6
        fcn.1009ccd0 () // fcn.1009ccd0(0x177f1c, 0x0, 0x0)
        push (dword [eax])
        eax = var_d8h
        push (eax)
        byte [var_4h] = 8
        fcn.102992d0 () // fcn.102992d0(0x177f28, 0x0)
        eax = dword [var_e4h]
        esp += 0x18   // ebp
        byte [var_4h] = 6
        v = eax & eax
        if (!v) goto loc_0x100d88be // likely
        goto loc_0x100d88b5;
    loc_0x100d88be:
        // CODE XREF from fcn.100d8680 @ 0x100d88b3(x)
        push (str.Packs) // 0x1038aacc // "Packs" // (pstr 0x1038aacc) "Packs"
        eax = var_f4h
        push (eax)
        ecx = var_d8h
        fcn.101bb190 () // fcn.101bb190(0x177f0c, 0x0, 0x177f28)
        ecx = var_f4h
        fcn.101ba9a0 () // fcn.101ba9a0(0x177f0c)
        v = eax & eax
        if (!v) goto loc_0x100d8d2b // unlikely
        goto loc_0x100d88e8;
    loc_0x100d8d2b:
        // CODE XREF from fcn.100d8680 @ 0x100d88e2(x)
        dword [var_e4h] = 0
        dword [var_e0h] = 0
        dword [var_dch] = 0
        push (0x10)   // 16
        byte [var_4h] = 0x12
        fcn.102f7820 ()
        esi = eax
        esp += 4
        v = esi & esi
        if (v) goto loc_0x100d8d69 // unlikely
        goto loc_0x100d8d5d;
    loc_0x100d8d69:
        // CODE XREF from fcn.100d8680 @ 0x100d8d5b(x)
        push (0xf)    // 15
        eax = esi + 0x10
        push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
        push (esi)
        dword [var_e4h] = esi
        dword [var_e0h] = esi
        dword [var_dch] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 0xf    // 15
        dword [var_e0h] = eax
        byte [eax] = 0
        dword [var_100h] = 0
        dword [var_fch] = 0
        dword [var_f8h] = 0
        push (6)      // 6
        byte [var_4h] = 0x14
        fcn.102f7820 ()
        edi = eax
        esp += 0x10   // (pstr 0x1038ad68) "assets/files.dz"
        v = edi & edi
        if (v) goto loc_0x100d8dd5 // likely
        goto loc_0x100d8dc9;
    loc_0x100d8dd5:
        // CODE XREF from fcn.100d8680 @ 0x100d8dc7(x)
        push (5)
        eax = edi + 6
        push (str.files) // 0x1038ad58 // "files" // (pstr 0x1038ad58) "files"
        push (edi)
        dword [var_100h] = edi
        dword [var_fch] = edi
        dword [var_f8h] = eax
        sub.MSVCR110.dll_memmove ()
        eax += 5
        dword [var_fch] = eax
        byte [eax] = 0
        xmm0 = qword [var_11ch]
        push (ecx)
        eax = esp
        byte [var_4h] = 0x15
        qword [eax] = xmm0
        xmm0 = qword [var_114h]
        qword [eax + 8] = xmm0
        eax = var_e4h
        push (eax)
        eax = var_100h
        push (eax)
        eax = var_d8h
        push (eax)
        fcn.100d5d10 () // fcn.100d5d10(0x177f28, 0x0)
        byte [var_4h] = 0x13
        
    loc_0x100d8e41:
        // CODE XREF from fcn.100d8680 @ 0x100d8d26(x)
        esp += 0x1c
        v = edi & edi
        if (!v) goto loc_0x100d8e51 // likely
        goto loc_0x100d8e48;
        goto loc_0x100d892b;
    loc_0x100d874e:
        push (0x11)   // 17
        fcn.102e3ef0 () // fcn.102e3ef0(0x0)
        esp += 4
        ecx = eax
        return eax;
    loc_0x100d87ee: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d8852: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x100d88b5: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d88e8: // orphan
         ecx = var_e8h
         fcn.10022be0 ()          // fcn.10022be0(0x0)
         eax = var_fch
         push (eax)
         ecx = var_f4h
         fcn.101bb160 ()          // fcn.101bb160(0x177f04, 0x0)
         eax = var_108h
         push (eax)
         ecx = var_f4h
         fcn.101bc630 ()          // fcn.101bc630(0x177ef8, 0x0)
         push (eax)
         ecx = var_fch
         fcn.101ba980 ()          // fcn.101ba980(0x177ef8, 0x0)
         v = al & al
         if (!v) 
         goto loc_0x100d892b;
    loc_0x100d892b: // orphan
         
    loc_0x100d8930: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d892b(x), 0x100d89b4(x)
         push (0x10374b40)        // '@K7\x10'
         push (0x10378db0)        // "Name" // (pstr 0x10378db0) "Name"
         eax = var_ech_2          // int32_t arg1
         push (eax)
         ecx = var_fch
         esi = str.files          // 0x1038ad58 // "files"
         fcn.1026b1d0 ()          // method.tinyxml2::XMLDocument.virtual_28 // fcn.1026b1d0(0x177f14)
         ecx = eax
         fcn.101bb0e0 ()          // fcn.101bb0e0(0x177f14, 0x0, 0x177f14)
         ecx = eax
         fcn.101bb070 ()          // fcn.101bb070(0x177f14, 0x0)

    loc_0x100d8960: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d897a(x)
         cl = byte [eax]
         v = cl - byte [esi]
         if (v) 
         goto loc_0x100d8966;
    loc_0x100d8966: // orphan
         v = cl & cl
         if (!v) 
         goto loc_0x100d896a;
    loc_0x100d896a: // orphan
         cl = byte [eax + 1]
         v = cl - byte [esi + 1]
         if (v) 
         goto loc_0x100d8972;
    loc_0x100d8972: // orphan
         eax += 2
         esi += 2
         v = cl & cl
         if (v) 
         goto loc_0x100d897c;
    loc_0x100d897c: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8968(x)
         eax = 0
         
         goto loc_0x100d8980;
    loc_0x100d8980: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d8964(x), 0x100d8970(x)
         eax = eax - eax
         eax |= 1

    loc_0x100d8985: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d897e(x)
         ecx = var_fch
         v = eax & eax
         if (!v) 
         goto loc_0x100d898f;
    loc_0x100d898f: // orphan
         fcn.101ba9d0 ()          // fcn.101ba9d0(0x0)
         eax = var_108h
         push (eax)
         ecx = var_f4h
         fcn.101bc630 ()          // fcn.101bc630(0x177ef8, 0x0)
         push (eax)
         ecx = var_fch
         fcn.101ba980 ()          // fcn.101ba980(0x177ef8, 0x0)
         v = al & al
         if (v) 
         goto loc_0x100d89ba;
    loc_0x100d89ba: // orphan
         
         goto loc_0x100d89bc;
    loc_0x100d89bc: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d898d(x)
         fcn.1026b1d0 ()          // method.tinyxml2::XMLDocument.virtual_28 // fcn.1026b1d0(0x0)
         eax = dword [eax]
         dword [var_e8h] = eax

    loc_0x100d89c9: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d8925(x), 0x100d89ba(x)
         ecx = var_e8h
         fcn.101ba9a0 ()          // fcn.101ba9a0(0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x100d89dc;
    loc_0x100d89dc: // orphan
         push (0x10374b40)        // '@K7\x10'
         push (str.Version)       // 0x1038ad60 // "Version" // (pstr 0x1038ad60) "Version"
         eax = var_104h
         push (eax)
         ecx = var_e8h
         fcn.101bb0e0 ()          // fcn.101bb0e0(0x177efc, 0x0, 0x177f18)
         ecx = eax
         fcn.101bb070 ()          // fcn.101bb070(0x177efc, 0x0)
         edi = eax
         dword [var_e4h] = 0
         dword [var_e0h] = 0
         dword [var_dch] = 0
         ecx = edi
         byte [var_4h] = 9
         edx = ecx + 1

    loc_0x100d8a28: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8a2d(x)
         al = byte [ecx]
         ecx++
         v = al & al
         if (v) 
         goto loc_0x100d8a2f;
    loc_0x100d8a2f: // orphan
         ecx -= edx
         eax = ecx + edi
         esi = eax
         esi -= edi
         dword [var_ech_2] = eax
         ebx = esi + 1
         eax = ebx - 1
         v = eax - 0xfffffffe
         if (((unsigned) v) > 0) 
         goto loc_0x100d8a49;
    loc_0x100d8a49: // orphan
         push (ebx)
         fcn.102f7820 ()
         ecx = eax
         esp += 4
         v = ecx & ecx
         if (v) 
         goto loc_0x100d8a58;
    loc_0x100d8a58: // orphan
         push (ebx)
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x100d8a63: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8a56(x)
         eax = ebx + ecx
         dword [var_e4h] = ecx
         dword [var_e0h] = ecx
         dword [var_dch] = eax
         
         goto loc_0x100d8a7a;
    loc_0x100d8a7a: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8a47(x)
         push (str.basic_string)  // 0x10374404 // "basic_string" // (pstr 0x10374404) "basic_string"
         fcn.102e3f60 ()          // fcn.102e3f60(0x0)
         ecx = dword [var_e4h]
         esp += 4                 // (pstr 0x10374404) "basic_string"

    loc_0x100d8a8d: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8a78(x)
         v = dword [var_ech_2] - edi
         if (!v) 
         goto loc_0x100d8a95;
    loc_0x100d8a95: // orphan
         push (esi)
         push (edi)
         push (ecx)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         ecx = esi + eax

    loc_0x100d8aa3: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8a93(x)
         dword [var_e0h] = ecx
         byte [ecx] = 0
         eax = var_e4h
         push (eax)
         ecx = var_138h
         byte [var_4h] = 0xa
         fcn.1028e920 ()          // fcn.1028e920(0x177f1c, 0x0, 0x177ec8)
         eax = dword [var_e4h]
         dword [var_12ch] = 0
         v = eax - dword [var_e0h]
         if (!v) 
         goto loc_0x100d8ada;
    loc_0x100d8ada: // orphan
         eax = var_138h
         push (eax)
         ecx = var_11ch
         fcn.1028e9a0 ()          // fcn.1028e9a0(0x177ec8, 0x0, 0x177ee4)
         v = al & al
         if (!v) 
         goto loc_0x100d8af4;
    loc_0x100d8af4: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8ad8(x)
         push (0)
         eax = var_128h
         push (eax)
         ecx = var_11ch
         bl = 1
         fcn.1028ed00 ()          // fcn.1028ed00(0x177ed8, 0x1, 0x177ee4, 0x0)
         push (dword [eax])
         eax = var_ech_2
         push (str.Version)       // 0x1038ad60 // "Version" // (pstr 0x1038ad60) "Version"
         push (eax)
         ecx = var_e8h
         byte [var_4h] = 0xb
         fcn.101bb0e0 ()          // fcn.101bb0e0(0x177f14, 0x1, 0x177f18)
         ecx = eax
         fcn.101bf4a0 ()          // fcn.101bf4a0(0x177f14, 0x1)
         eax = dword [var_128h]
         byte [var_4h] = 0xa
         v = eax & eax
         if (!v) 
         goto loc_0x100d8b3c;
    loc_0x100d8b3c: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d8b45: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8b3a(x)
         esp -= 0xc
         esi = esp
         dword [var_ech_2] = esi
         dword [esi] = 0
         dword [esi + 4] = 0
         dword [esi + 8] = 0
         push (0x11)              // 17
         byte [var_4h] = 0xc
         fcn.102f7820 ()
         ecx = eax
         esp += 4
         v = ecx & ecx
         if (v) 
         goto loc_0x100d8b78;
    loc_0x100d8b78: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x100d8b84: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8b76(x)
         push (0x10)              // 16
         eax = ecx + 0x11
         push (str.assets_packs.xml) // 0x1038aab8 // "assets/packs.xml" // (pstr 0x1038aab8) "assets/packs.xml"
         push (ecx)
         dword [esi] = ecx
         dword [esi + 4] = ecx
         dword [esi + 8] = eax
         sub.MSVCR110.dll_memmove ()
         eax += 0x10              // 16
         dword [esi + 4] = eax
         byte [eax] = 0
         esp += 0xc
         eax = var_128h
         push (eax)
         byte [var_4h] = 0xa
         fcn.1009ccd0 ()          // fcn.1009ccd0(0x177ed8, 0x0, 0x0)
         push (dword [eax])
         eax = var_d8h
         push (eax)
         byte [var_4h] = 0xd
         fcn.100f9360 ()          // fcn.100f9360(0x177f28, 0x0)
         eax = dword [var_128h]
         esp += 0x18              // ebp
         byte [var_4h] = 0xa
         v = eax & eax
         if (!v) 
         goto loc_0x100d8bdb;
    loc_0x100d8bdb: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4
         
         goto loc_0x100d8be6;
    loc_0x100d8be6: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8aee(x)
         bl = byte [var_edh]

    loc_0x100d8bec: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d8bd9(x), 0x100d8be4(x)
         eax = dword [var_e4h]
         byte [var_4h] = 6
         v = eax & eax
         if (!v) 
         goto loc_0x100d8bfa;
    loc_0x100d8bfa: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d8c03: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8bf8(x)
         v = bl & bl
         if (!v) 
         goto loc_0x100d8c0b;
    loc_0x100d8c0b: // orphan
         
         goto loc_0x100d8c10;
    loc_0x100d8c10: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d89d6(x)
         dword [var_e4h] = 0
         dword [var_e0h] = 0
         dword [var_dch] = 0
         push (0x10)              // 16
         byte [var_4h] = 0xe
         fcn.102f7820 ()
         esi = eax
         esp += 4
         v = esi & esi
         if (v) 
         goto loc_0x100d8c42;
    loc_0x100d8c42: // orphan
         push (0x10)              // 16
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         esi = eax

    loc_0x100d8c4e: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8c40(x)
         push (0xf)               // 15
         eax = esi + 0x10
         push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
         push (esi)
         dword [var_e4h] = esi
         dword [var_e0h] = esi
         dword [var_dch] = eax
         sub.MSVCR110.dll_memmove ()
         eax += 0xf               // 15
         dword [var_e0h] = eax
         byte [eax] = 0
         dword [var_10ch] = 0
         dword [var_108h] = 0
         dword [var_104h] = 0
         push (6)                 // 6
         byte [var_4h] = 0x10
         fcn.102f7820 ()
         edi = eax
         esp += 0x10              // (pstr 0x1038ad68) "assets/files.dz"
         v = edi & edi
         if (v) 
         goto loc_0x100d8cae;
    loc_0x100d8cae: // orphan
         push (6)                 // 6
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         edi = eax

    loc_0x100d8cba: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8cac(x)
         push (5)
         eax = edi + 6
         push (str.files)         // 0x1038ad58 // "files" // (pstr 0x1038ad58) "files"
         push (edi)
         dword [var_10ch] = edi
         dword [var_108h] = edi
         dword [var_104h] = eax
         sub.MSVCR110.dll_memmove ()
         eax += 5
         dword [var_108h] = eax
         byte [eax] = 0
         xmm0 = qword [var_11ch]
         push (ecx)
         eax = esp
         byte [var_4h] = 0x11
         qword [eax] = xmm0
         xmm0 = qword [var_114h]
         qword [eax + 8] = xmm0
         eax = var_e4h
         push (eax)
         eax = var_10ch
         push (eax)
         eax = var_d8h
         push (eax)
         fcn.100d5d10 ()          // fcn.100d5d10(0x177f28, 0x0)
         byte [var_4h] = 0xf
         
         goto loc_0x100d8d2b;
    loc_0x100d8d5d: // orphan
         push (0x10)              // 16
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         esi = eax

    loc_0x100d8dc9: // orphan
         push (6)                 // 6
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         edi = eax

    loc_0x100d8e48: // orphan
         push (edi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d8e51: // orphan
         // CODE XREF from fcn.100d8680 @ 0x100d8e46(x)
         byte [var_4h] = 6
         v = esi & esi
         if (!v) 
         goto loc_0x100d8e59;
    loc_0x100d8e59: // orphan
         push (esi)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d8e62: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d8c0b(x), 0x100d8e57(x)
         push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
         fcn.102ca2cf ()          // fcn.102ca2cf(0x0, 0x0)
         esp += 4                 // (pstr 0x1038ad68) "assets/files.dz"
         v = eax & eax
         if (v) 
         goto loc_0x100d8e73;
    loc_0x100d8e73: // orphan
         push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
         fcn.100a8100 ()          // fcn.100a8100(0x0)
         push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
         fcn.102ca3bc ()          // fcn.102ca3bc(0x0, 0x0)
         esp += 8

    loc_0x100d8e8a: // orphan
         // CODE XREFS from fcn.100d8680 @ 0x100d8c05(x), 0x100d8e71(x)
         ecx = var_d8h
         dword [var_4h] = 0xffffffff // -1
         fcn.101ba910 ()          // fcn.101ba910(0x0, 0x0)
         ecx = dword [var_ch_2]   // "assets/files.dz" str.assets_files.dz
         dword fs:[0] = ecx
         ecx = pop ()             // ebp
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         ecx = dword [var_10h_2]
         ecx ^= ebp
         fcn.102fff32 ()          // fcn.102fff32(0x0)
         esp = ebp
         ebp = pop ()
         return

}

