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
void fcn.1015a410 (int32_t arg1, int32_t arg2) {
        // CODE XREF from fcn.1015acd0 @ 0x1015ad21(x)
        push (ebp)
        ebp = esp
        push (0xffffffffffffffff)
        push (0x1031d4e0)
        eax = dword fs:[0]
        push (eax)
        esp -= 0x3c
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_10h] = eax
        push (esi)
        push (edi)
        push (eax)
        eax = var_ch
        dword fs:[0] = eax
        esi = ecx     // arg3
        eax = esi + 0x250
        v = eax & eax
        if (v) goto loc_0x1015a479 // likely
        goto loc_0x1015a446;
    while (!(edi & edi)) {
        // CODE XREF from fcn.1015a410 @ 0x1015a494(x)
        ecx = esi + 0x47c
        fcn.1016a160 () // fcn.1016a160(0x0, 0x0)
        // CODE XREFS from fcn.1015a410 @ 0x1015a4be(x), 0x1015a522(x), 0x1015a531(x)
        ecx = dword [var_ch]
        dword fs:[0] = ecx
        ecx = pop ()
        edi = pop ()
        esi = pop ()
        ecx = dword [var_10h]
        ecx ^= ebp
        fcn.102fff32 () // fcn.102fff32(0x0)
        esp = ebp
        ebp = pop ()
        return
    }
    loc_0x1015a496:
        ecx = edi
        fcn.10103130 () // fcn.10103130(0x0)
        v = eax & eax
        if (v) goto loc_0x1015a4c0 // unlikely
        goto loc_0x1015a4a1;
    loc_0x1015a4c0:
        // CODE XREF from fcn.1015a410 @ 0x1015a49f(x)
        eax += 0x14   // 20
        push (eax)
        ecx = var_48h
        fcn.10087330 () // fcn.10087330(0x14, 0x0, 0x177fb8)
        eax = dword [esi + 0x598]
        esi += 0x47c  // 1148
        dword [eax + 0x40] = edi
        ecx = esi
        dword [var_4h] = 0
        byte [var_14h] = 1
        fcn.1016a160 () // fcn.1016a160(-1, 0x0)
        eax = var_48h // int32_t arg1
        push (eax)
        ecx = esi     // int32_t arg_8h
        fcn.1016a2c0 () // fcn.1016a2c0(0x177fb8, 0x0, 0x47c)
        ecx = esi
        fcn.10169c10 () // fcn.10169c10(0x177fb8)
        eax = dword [var_3ch]
        dword [var_4h] = 1
        v = eax & eax
        if (!v) goto loc_0x1015a516 // likely
        goto loc_0x1015a50d;
    while (!(eax & eax)) {
        ecx = edi
        fcn.10103690 () // fcn.10103690(0x0)
        push (dword [eax])
        push (str.tactics:_conditionKeys_is_null_for__s) // 0x105b19a4 // "tactics: conditionKeys is null for %s" // (pstr 0x105b19a4) "tactics: conditionKeys is null for %s"
        fcn.101472f0 () // fcn.101472f0(0x0, 0x0)
        esp += 8
        ecx = edi
        fcn.10103130 () // fcn.10103130(0x0)
        goto loc_0x1015a45e
        push (eax)
        fcn.102f7780 () // fcn.102f7780(0x0)
        esp += 4
    }
    loc_0x1015a528:
        push (eax)
        fcn.102f7780 () // fcn.102f7780(0x0)
        esp += 4
        goto loc_0x1015a45e
        
        return eax;
}

