// callconv: eax reg (eax, ebx, ecx, edx);
void method.ConditionKeys.virtual_8 (int32_t arg1, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        eax = dword [arg_8h]
        push (esi)
        esi = ecx     // arg3
        ecx = dword [eax]
        v = byte [ecx + 0x34] - 0
        if (v) goto loc_0x100875ba // likely
        goto loc_0x100875b1;
    loc_0x100875ba:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875af(x)
        edx = esi + 0x14
        
    loc_0x100875bd:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875b8(x)
        v = byte [eax + 0x9c] - 0
        if (!v) goto loc_0x100875d2 // unlikely
        goto loc_0x100875c6;
        goto loc_0x100875ba;
        return eax;
        return eax;
    loc_0x100875c6:
        push (ecx)
        ecx = edx
        fcn.10121fa0 () // fcn.10121fa0(0x0, 0x0)
        cl = al
        goto loc_0x100875d4
        
    loc_0x100875d4:
        // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875d0(x)
        v = byte [esi + 0xc] - 0
        esi = pop ()
        if (!v) goto loc_0x100875e6 // unlikely
        return eax;
        return eax;
    loc_0x100875db:
        eax = 0
        v = cl & cl
        al = v == 0
        ebp = pop ()
        return
    loc_0x100875d2: // orphan
         // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875c4(x)
         cl = 1

    loc_0x100875e6: // orphan
         // CODE XREF from method.ConditionKeys.virtual_8 @ 0x100875d9(x)
         eax = cl
         ebp = pop ()
         return

}

