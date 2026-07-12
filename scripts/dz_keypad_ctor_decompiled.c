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
void fcn.10120650 (int32_t arg1, int32_t arg2) {
        // CALL XREF from fcn.1008ca20 @ 0x1008ca63(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031836e)
        eax = dword fs:[0]
        push (eax)
        esp -= 8
        push (ebx)    // arg2
        push (esi)
        push (edi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch  // int32_t arg1
        dword fs:[0] = eax
        esi = ecx     // arg3
        dword [var_10h] = esi
        fcn.10062300 () // fcn.10062300(0x177ff0)
        dword [esi] = vtable.KeyPad.0 // [0x105ae474:4]=0x10120bb0 method.KeyPad.virtual_0
        dword [esi + 0x38] = 0
        dword [esi + 0x3c] = 0
        dword [var_4h] = 0
        dword [esi + 0x40] = 0
        dword [esi + 0x44] = 0
        dword [esi + 0x48] = 0
        dword [esi + 0x4c] = 0
        push (0x18)   // 24
        byte [var_4h] = 2
        dword [esi + 0x50] = 0
        fcn.102f7820 ()
        esp += 4
        v = eax & eax
        if (v) goto loc_0x101206dc // likely
        goto loc_0x101206d2;
    loc_0x101206dc:
        // CODE XREF from fcn.10120650 @ 0x101206d0(x)
        dword [esi + 0x50] = eax
        dword [esi + 0x54] = 0
        byte [eax] = 0
        eax = dword [esi + 0x50]
        dword [eax + 4] = 0
        eax = dword [esi + 0x50]
        dword [eax + 8] = eax
        eax = dword [esi + 0x50]
        dword [eax + 0xc] = eax
        push (0x18)   // 24
        byte [var_4h] = 3
        dword [esi + 0x5c] = 0
        fcn.102f7820 ()
        esp += 4
        v = eax & eax
        if (v) goto loc_0x10120722 // unlikely
        goto loc_0x10120718;
    loc_0x10120722:
        // CODE XREF from fcn.10120650 @ 0x10120716(x)
        dword [esi + 0x5c] = eax
        dword [esi + 0x60] = 0
        byte [eax] = 0
        eax = dword [esi + 0x5c]
        dword [eax + 4] = 0
        eax = dword [esi + 0x5c]
        dword [eax + 8] = eax
        eax = dword [esi + 0x5c]
        dword [eax + 0xc] = eax
        esp -= 0xc
        eax = esp     // int32_t arg1
        push (2)
        dword [eax] = 0
        dword [eax + 4] = 0
        ecx = esi + 0x38 // int32_t arg_8h
        byte [var_4h] = 4
        dword [eax + 8] = 0
        fcn.101212f0 () // fcn.101212f0(0x177ff4, 0x0, 0x38, 0x0)
        push (0x10)   // 16
        fcn.102e3340 ()
        esp += 4
        dword [var_14h] = eax
        byte [var_4h] = 5
        v = eax & eax
        if (!v) goto loc_0x10120792 // unlikely
        goto loc_0x10120781;
    loc_0x10120792:
        // CODE XREF from fcn.10120650 @ 0x1012077f(x)
        eax = 0
        
    loc_0x10120794:
        // CODE XREF from fcn.10120650 @ 0x10120790(x)
        push (eax)
        push (0x25)   // '%' // "%"
        ecx = esi     // int32_t arg_bh
        byte [var_4h] = 4
        fcn.10121150 () // fcn.10121150(0x0, 0x0, 0x0, 0x0)
        push (0x10)   // 16
        fcn.102e3340 ()
        esp += 4
        dword [var_14h] = eax
        byte [var_4h] = 6
        v = eax & eax
        if (!v) goto loc_0x101207c8 // likely
        goto loc_0x101207b7;
        return eax;
    loc_0x10120718: // orphan
         push (0x18)              // 24
         fcn.102e3ef0 ()          // fcn.102e3ef0(0x0)
         esp += 4

    loc_0x10120781: // orphan
         push (0)
         push (0)
         push (0)
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120792;
    loc_0x101207b7: // orphan
         push (0)
         push (0)
         push (1)
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101207c8;
    loc_0x101207c8: // orphan
         // CODE XREF from fcn.10120650 @ 0x101207b5(x)
         eax = 0

    loc_0x101207ca: // orphan
         // CODE XREF from fcn.10120650 @ 0x101207c6(x)
         push (eax)
         push (0x26)              // '&' // "&"
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 7
         v = eax & eax
         if (!v) 
         goto loc_0x101207ed;
    loc_0x101207ed: // orphan
         push (0)
         push (0)
         push (2)
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101207fe;
    loc_0x101207fe: // orphan
         // CODE XREF from fcn.10120650 @ 0x101207eb(x)
         eax = 0

    loc_0x10120800: // orphan
         // CODE XREF from fcn.10120650 @ 0x101207fc(x)
         push (eax)
         push (0x21)              // '!' // 33 // "!"
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 8
         v = eax & eax
         if (!v) 
         goto loc_0x10120823;
    loc_0x10120823: // orphan
         push (0)
         push (0)
         push (3)
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120834;
    loc_0x10120834: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120821(x)
         eax = 0

    loc_0x10120836: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120832(x)
         push (eax)
         push (0x22)              // '\"' // 34 // "\""
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 9
         v = eax & eax
         if (!v) 
         goto loc_0x10120859;
    loc_0x10120859: // orphan
         push (0)
         push (0)
         push (4)                 // 4
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x1012086a;
    loc_0x1012086a: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120857(x)
         eax = 0

    loc_0x1012086c: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120868(x)
         push (eax)
         push (0x1f)              // 31
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xa
         v = eax & eax
         if (!v) 
         goto loc_0x1012088f;
    loc_0x1012088f: // orphan
         push (0)
         push (0)
         push (5)                 // 5
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101208a0;
    loc_0x101208a0: // orphan
         // CODE XREF from fcn.10120650 @ 0x1012088d(x)
         eax = 0

    loc_0x101208a2: // orphan
         // CODE XREF from fcn.10120650 @ 0x1012089e(x)
         push (eax)
         push (0x20)              // 32 // " "
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xb
         v = eax & eax
         if (!v) 
         goto loc_0x101208c5;
    loc_0x101208c5: // orphan
         push (0)
         push (0)
         push (0xa)               // 10
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101208d6;
    loc_0x101208d6: // orphan
         // CODE XREF from fcn.10120650 @ 0x101208c3(x)
         eax = 0

    loc_0x101208d8: // orphan
         // CODE XREF from fcn.10120650 @ 0x101208d4(x)
         push (eax)
         push (0x2f)              // '/' // "/"
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xc
         v = eax & eax
         if (!v) 
         goto loc_0x101208fb;
    loc_0x101208fb: // orphan
         push (0)
         push (0)
         push (0xb)               // 11
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x1012090c;
    loc_0x1012090c: // orphan
         // CODE XREF from fcn.10120650 @ 0x101208f9(x)
         eax = 0

    loc_0x1012090e: // orphan
         // CODE XREF from fcn.10120650 @ 0x1012090a(x)
         push (eax)
         push (0x2b)              // '+' // "+"
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xd
         v = eax & eax
         if (!v) 
         goto loc_0x10120931;
    loc_0x10120931: // orphan
         push (0)
         push (0)
         push (6)                 // 6
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120942;
    loc_0x10120942: // orphan
         // CODE XREF from fcn.10120650 @ 0x1012092f(x)
         eax = 0

    loc_0x10120944: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120940(x)
         push (eax)
         push (1)
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xe
         v = eax & eax
         if (!v) 
         goto loc_0x10120967;
    loc_0x10120967: // orphan
         push (0)
         push (0)
         push (7)                 // 7
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120978;
    loc_0x10120978: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120965(x)
         eax = 0

    loc_0x1012097a: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120976(x)
         push (eax)
         push (4)                 // 4
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0xf
         v = eax & eax
         if (!v) 
         goto loc_0x1012099d;
    loc_0x1012099d: // orphan
         push (0)
         push (0)
         push (0xe)               // 14
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101209ae;
    loc_0x101209ae: // orphan
         // CODE XREF from fcn.10120650 @ 0x1012099b(x)
         eax = 0

    loc_0x101209b0: // orphan
         // CODE XREF from fcn.10120650 @ 0x101209ac(x)
         push (eax)
         push (0xa)               // 10
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0x10
         v = eax & eax
         if (!v) 
         goto loc_0x101209d3;
    loc_0x101209d3: // orphan
         push (0)
         push (0)
         push (0xf)               // 15
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x101209e4;
    loc_0x101209e4: // orphan
         // CODE XREF from fcn.10120650 @ 0x101209d1(x)
         eax = 0

    loc_0x101209e6: // orphan
         // CODE XREF from fcn.10120650 @ 0x101209e2(x)
         push (eax)
         push (0xc)               // 12
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0x11
         v = eax & eax
         if (!v) 
         goto loc_0x10120a09;
    loc_0x10120a09: // orphan
         push (0)
         push (0)
         push (0x10)              // 16
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120a1a;
    loc_0x10120a1a: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120a07(x)
         eax = 0

    loc_0x10120a1c: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120a18(x)
         push (eax)
         push (9)                 // 9
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         push (0x10)              // 16
         fcn.102e3340 ()
         esp += 4
         dword [var_14h] = eax
         byte [var_4h] = 0x12
         v = eax & eax
         if (!v) 
         goto loc_0x10120a3f;
    loc_0x10120a3f: // orphan
         push (0)
         push (0)
         push (0x11)              // 17
         push (0)
         ecx = eax
         fcn.100eeed0 ()          // fcn.100eeed0(0x0, 0x0, 0x0, 0x0, 0x0)
         
         goto loc_0x10120a50;
    loc_0x10120a50: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120a3d(x)
         eax = 0

    loc_0x10120a52: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120a4e(x)
         push (eax)
         push (0xb)               // 11
         ecx = esi                // int32_t arg_bh
         byte [var_4h] = 4
         fcn.10121150 ()          // fcn.10121150(0x0, 0x0, 0x0, 0x0)
         esp -= 0xc
         eax = esp                // int32_t arg1
         push (2)
         dword [eax] = 0
         dword [eax + 4] = 0
         ecx = esi + 0x44         // int32_t arg_8h
         dword [eax + 8] = 0
         fcn.101212f0 ()          // fcn.101212f0(0x177fe8, 0x0, 0x44, 0x0)
         fcn.1005af30 ()
         ecx = eax
         fcn.1001b460 ()          // method.VideoSprite.1.virtual_16 // fcn.1001b460(0x177fe8)
         v = al & al
         if (v) 
         goto loc_0x10120a93;
    loc_0x10120a93: // orphan
         fcn.1005af30 ()
         ecx = eax
         fcn.1005af20 ()          // fcn.1005af20(0x0)
         v = al & al
         if (!v) 
         goto loc_0x10120aa3;
    loc_0x10120aa3: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120a91(x)
         push (1)
         push (0x2d)              // '-' // "-\x01"
         ecx = esi                // int32_t arg_bh
         fcn.101210b0 ()          // fcn.101210b0(0x0, 0x0, 0x0, 0x0)
         push (5)                 // 5
         push (0x29)              // ')' // ")"
         ecx = esi                // int32_t arg_bh
         fcn.101210b0 ()          // fcn.101210b0(0x0, 0x0, 0x0, 0x0)
         push (7)                 // 7
         push (0x17)              // 23
         ecx = esi                // int32_t arg_bh
         fcn.101210b0 ()          // fcn.101210b0(0x0, 0x0, 0x0, 0x0)
         push (3)
         push (0x1a)              // 26
         ecx = esi                // int32_t arg_bh
         fcn.101210b0 ()          // fcn.101210b0(0x0, 0x0, 0x0, 0x0)

    loc_0x10120acf: // orphan
         // CODE XREF from fcn.10120650 @ 0x10120aa1(x)
         eax = esi
         dword [esi + 0x30] = 1
         ecx = dword [var_ch]
         dword fs:[0] = ecx
         ecx = pop ()
         edi = pop ()
         esi = pop ()
         ebx = pop ()
         esp = ebp
         ebp = pop ()
         return

}

