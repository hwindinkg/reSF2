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
void fcn.100d5d10 (int32_t arg_8h, int32_t arg_14h) {
        // CALL XREFS from fcn.100d8680 @ 0x100d8d1d(x), 0x100d8e38(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x10310a68) // 'h\n1\x10'
        eax = dword fs:[0]
        push (eax)
        esp -= 0x24
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        edi = dword [arg_8h]
        push (str.Packs) // 0x1038aacc // "Packs" // (pstr 0x1038aacc) "Packs"
        eax = var_10h
        push (eax)
        ecx = edi
        fcn.101bb190 () // fcn.101bb190(0x177fec, 0x0, 0x0)
        ecx = var_10h
        fcn.101ba930 () // fcn.101ba930(0x177fec)
        v = al & al
        if (!v) goto loc_0x100d5d6b // unlikely
        goto loc_0x100d5d56;
    loc_0x100d5d6b:
        // CODE XREF from fcn.100d5d10 @ 0x100d5d54(x)
        push (str.Pack) // 0x1038b2dc // "Pack" // (pstr 0x1038b2dc) "Pack"
        eax = var_14h
        push (eax)
        ecx = var_10h
        fcn.101bad70 () // fcn.101bad70(0x177fec, 0x0, 0x177ff0)
        push (str.files) // 0x1038ad58 // "files" // (pstr 0x1038ad58) "files"
        push (0x10378db0) // "Name" // (pstr 0x10378db0) "Name"
        eax = var_18h
        push (eax)
        ecx = var_14h // (pstr 0x10378db0) "Name"
        fcn.101bac90 () // fcn.101bac90(0x177fe8, 0x0, 0x177fec)
        ecx = eax
        fcn.101bf4a0 () // fcn.101bf4a0(0x177fe8, 0x0)
        push (str.assets_files.dz) // 0x1038ad68 // "assets/files.dz" // (pstr 0x1038ad68) "assets/files.dz"
        push (0x1038b2e4) // "Url" // (pstr 0x1038b2e4) "Url"
        eax = var_18h
        push (eax)
        ecx = var_14h // (pstr 0x10378db0) "Name"
        fcn.101bac90 () // fcn.101bac90(0x177fe8, 0x0, 0x177fec)
        ecx = eax
        fcn.101bf4a0 () // fcn.101bf4a0(0x177fe8, 0x0)
        push (0)
        eax = var_24h // (pstr 0x1038ad68) "assets/files.dz"
        push (eax)
        ecx = arg_14h
        fcn.1028ed00 () // fcn.1028ed00(0x177fdc, 0x0, 0x178014, 0x0)
        push (dword [eax]) // (pstr 0x1038ad68) "assets/files.dz"
        eax = var_18h
        push (str.Version) // 0x1038ad60 // "Version" // (pstr 0x1038ad60) "Version"
        push (eax)
        ecx = var_14h // (pstr 0x10378db0) "Name"
        dword [var_4h] = 0
        fcn.101bac90 () // fcn.101bac90(0x177fe8, 0x0, 0x177fec)
        ecx = eax
        fcn.101bf4a0 () // fcn.101bf4a0(0x177fe8, 0x0)
        eax = dword [var_24h] // "assets/files.dz" str.assets_files.dz
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x100d5dfc // unlikely
        goto loc_0x100d5df3;
    loc_0x100d5dfc:
        // CODE XREF from fcn.100d5d10 @ 0x100d5df1(x)
        esp -= 0xc
        esi = esp
        dword [var_18h] = esi
        dword [esi] = 0
        dword [esi + 4] = 0
        dword [esi + 8] = 0
        push (0x11)   // 17
        dword [var_4h] = 1
        fcn.102f7820 ()
        ecx = eax
        esp += 4
        v = ecx & ecx
        if (v) goto loc_0x100d5e3b // unlikely
        goto loc_0x100d5e2f;
    loc_0x100d5e3b:
        // CODE XREF from fcn.100d5d10 @ 0x100d5e2d(x)
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
        eax = var_30h
        push (eax)
        dword [var_4h] = 0xffffffff // -1
        fcn.1009ccd0 () // fcn.1009ccd0(0x177fd0, 0x0, 0x0)
        push (dword [eax])
        dword [var_4h] = 2
        push (edi)
        fcn.100f9360 () // fcn.100f9360(0x177fd0, 0x0)
        eax = dword [var_30h]
        esp += 0x18   // ebp
        dword [var_4h] = 0xffffffff // -1
        v = eax & eax
        if (!v) goto loc_0x100d5e98 // unlikely
        goto loc_0x100d5e8f;
    loc_0x100d5e98:
        // CODE XREF from fcn.100d5d10 @ 0x100d5e8d(x)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        esp = ebp
        ebp = pop ()
        return
        return eax;
    loc_0x100d5df3: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x100d5e2f: // orphan
         push (0x11)              // 17
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4
         ecx = eax

    loc_0x100d5e8f: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

