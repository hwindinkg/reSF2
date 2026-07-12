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
void method.Model.virtual_16 (uint32_t arg1, int32_t arg2) {
        // JUMP XREF from method.WeaponModel.virtual_16 @ 0x10296b64(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031d478)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x24
        push (esi)
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        esi = ecx     // arg3
        dword [var_18h] = 0
        dword [var_14h] = 0
        dword [var_10h] = 0
        eax = dword [esi + 0x1a0]
        dword [var_4h] = 0
        eax = dword [eax + 0xa8]
        v = eax & eax
        if (!v) goto loc_0x10159ce0 // unlikely
        goto loc_0x10159cd4;
    loc_0x10159ce0:
        // CODE XREF from method.Model.virtual_16 @ 0x10159cd2(x)
        eax = dword [esi + 0x1a0]
        eax = dword [eax + 0xb4]
        v = eax & eax
        if (!v) goto loc_0x10159cfc // unlikely
        goto loc_0x10159cf0;
    loc_0x10159cfc:
        // CODE XREF from method.Model.virtual_16 @ 0x10159cee(x)
        eax = dword [esi + 0x1a0]
        eax = dword [eax + 0xb8]
        v = eax & eax
        if (!v) goto loc_0x10159d18 // unlikely
        goto loc_0x10159d0c;
    loc_0x10159d18:
        // CODE XREF from method.Model.virtual_16 @ 0x10159d0a(x)
        ecx = dword [esi + 0x1a0]
        eax = var_30h
        push (eax)
        fcn.1016ffb0 () // fcn.1016ffb0(0x177fd0, 0x0, -1)
        ecx = dword [esi + 0x1a0]
        eax = var_24h
        push (eax)
        byte [var_4h] = 1
        fcn.10170840 () // fcn.10170840(0x177fdc, 0x0, -1)
        eax = dword [esi + 0x1a0]
        ecx = var_30h
        push (ecx)
        push (dword [eax + 0xc])
        eax += 0x40   // 64
        push (eax)    // "?"
        push (0)
        eax = var_24h
        push (eax)
        eax = esi + 0x62c
        push (eax)
        byte [var_4h] = 2
        fcn.10059530 () // fcn.10059530(0x62c, 0x0, 0x177fd0, 0x0, 0x0, 0x0, 0x0)
        esp += 0x18
        ecx = esi
        fcn.1015a540 () // fcn.1015a540(0x62c, 0x0)
        eax = dword [var_24h]
        byte [var_4h] = 1
        v = eax & eax
        if (!v) goto loc_0x10159d7f // unlikely
        goto loc_0x10159d76;
    loc_0x10159d7f:
        // CODE XREF from method.Model.virtual_16 @ 0x10159d74(x)
        eax = dword [var_30h]
        byte [var_4h] = 0
        v = eax & eax
        if (!v) goto loc_0x10159d93 // likely
        goto loc_0x10159d8a;
    loc_0x10159d93:
        // CODE XREF from method.Model.virtual_16 @ 0x10159d88(x)
        ecx = var_18h
        dword [var_4h] = 0xffffffff // -1
        fcn.10127920 () // fcn.10127920(0x0, 0x0)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()  // ebp
        esi = pop ()
        esp = ebp
        ebp = pop ()
        return
        return eax;
    loc_0x10159cf0: // orphan
         eax += 0x2c              // 44
         push (eax)               // ","
         ecx = var_18h
         fcn.1000fd30 ()          // fcn.1000fd30(0x2c, 0x0)

    loc_0x10159d0c: // orphan
         eax += 0x2c              // 44
         push (eax)               // ","
         ecx = var_18h
         fcn.1000fd30 ()          // fcn.1000fd30(0x2c, 0x0)

    loc_0x10159d76: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

    loc_0x10159d8a: // orphan
         push (eax)
         fcn.102f7780 ()          // fcn.102f7780(0x0)
         esp += 4

}

