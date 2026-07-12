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
void fcn.1015eeb0 (int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg_8h) {
        // CALL XREFS from fcn.10161ad0 @ 0x10161cd2(x), 0x10161d32(x)
        push (ebp)
        ebp = esp
        esp -= 0xc
        push (ebx)    // arg2
        ebx = ecx     // arg3
        ecx = dword [ebx + 0xc]
        eax = ecx
        cdq
        dword [0x10657c9c] /= // [0x10657c9c:4]=0
        v = edx & edx // arg4
        al = v != 0
        v = dword [ebx + 0x58] - 0
        byte [ebx + 0x80] = al
        if (!v) goto loc_0x1015efc0 // unlikely
        goto loc_0x1015eeda;
    loc_0x1015efc0:
        // CODE XREFS from fcn.1015eeb0 @ 0x1015eed4(x), 0x1015eede(x)
        eax = 0
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x1015eee4;
        return eax;
    loc_0x1015eee4:
        movss xmm0 dword [ebx + 0x5c]
        push (esi)
        esi = dword [arg_8h]
        push (0)
        push (ecx)
        movss dword [esp] xmm0
        push (ecx)
        push (dword [ebx + 0x10])
        ecx = esi
        fcn.10159770 () // fcn.10159770(0x0)
        ecx = eax
        fcn.10164ac0 () // fcn.10164ac0(0x0)
        push (ecx)
        ecx = dword [ebx + 0x24]
        fstp dword [esp]
        fcn.10164ac0 () // fcn.10164ac0(0x0)
        push (ecx)
        ecx = ebx
        fstp dword [esp]
        push (dword [ebx + 0x54])
        push (dword [ebx + 0x58])
        push (esi)
        fcn.1015efd0 () // fcn.1015efd0(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0)
        esi = eax
        v = esi & esi
        if (!v) goto loc_0x1015efb6 // likely
        goto loc_0x1015ef2f;
    loc_0x1015efb6:
        // CODE XREFS from fcn.1015eeb0 @ 0x1015ef29(x), 0x1015ef47(x)
        eax = esi
        esi = pop ()
        ebx = pop ()
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x1015ef49;
        return eax;
    loc_0x1015ef49:
        push (edi)
        edi = 0
        esi = 0
        edi = edi
        
    loc_0x1015ef50:
        // CODE XREF from fcn.1015eeb0 @ 0x1015efa4(x)
        eax = dword [ebx + 0xb8]
        ecx = ebx + 0xb8
        eax = dword [eax + esi*8]
        dword [var_4h] = eax
        v = eax & eax
        if (!v) goto loc_0x1015ef8b // unlikely
        goto loc_0x1015ef66;
        return eax;
    loc_0x1015ef66:
        ecx = dword [arg_8h]
        fcn.101598a0 () // fcn.101598a0(0x0, 0x0)
        push (str.NPivot) // 0x1038b6a4 // "NPivot" // (pstr 0x1038b6a4) "NPivot"
        push (eax)
        push (dword [ebx + 0x14])
        ecx = ebx
        push (dword [var_4h])
        fcn.10162ba0 () // fcn.10162ba0(0x0, 0x0, 0x0, 0x0, 0x0, 0x0)
        v = al & al
        if (!v) goto loc_0x1015efa0 // likely
        goto loc_0x1015ef85;
    while (((unsigned) v) < 0) {
        ecx = ebx + 0xb8
        // CODE XREF from fcn.1015eeb0 @ 0x1015ef64(x)
        v = edi - esi
        jae 0x1015ef9f // likely
        ecx = dword [ecx]
        eax = dword [ecx + esi*8]
        dword [ecx + edi*8] = eax
        eax = dword [ecx + esi*8 + 4]
        dword [ecx + edi*8 + 4] = eax
        // CODE XREF from fcn.1015eeb0 @ 0x1015ef8d(x)
        edi++
    }
    loc_0x1015efa6:
        ecx = dword [var_ch]
        push (0xffffffffffffffff)
        push (0)
        push (edi)
        esi = edi
        fcn.101617e0 () // fcn.101617e0(0x0, 0x0, 0x0, 0x0)
        edi = pop ()
            goto loc_0x1015ef85;
        return eax;
    loc_0x1015ef2f: // orphan
         ecx = ebx + 0xb8
         dword [var_ch] = ecx
         push (ecx)
         ecx = ebx
         fcn.1015f980 ()          // fcn.1015f980(0x0, 0x0, 0x0)
         esi = eax
         dword [var_8h] = esi
         v = esi & esi
         if (!v) 
}

