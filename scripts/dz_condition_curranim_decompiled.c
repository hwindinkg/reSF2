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
void method.ConditionCurrentAnimation.virtual_8 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        push (ebx)    // arg2
        push (esi)
        esi = ecx     // arg3
        bl = 0
        v = dword [esi + 0x14] - 0
        push (edi)
        if (!v) goto loc_0x10083c37 // unlikely
        goto loc_0x10083bc0;
    loc_0x10083c37:
        // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083bbe(x)
        ecx = dword [esi + 0x10]
        eax = ecx
        eax--
        if (!v) goto loc_0x10083c53 // unlikely
        goto loc_0x10083c3f;
    loc_0x10083c53:
        // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c3d(x)
        eax = dword [arg_8h]
        al = byte [eax + 0xa0]
        
    loc_0x10083c5c:
        // CODE XREFS from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c7f(x), 0x10083c8a(x)
        v = al - byte [esi + 0x19]
        goto loc_0x10083c42;
        goto loc_0x10083bde;
        return eax;
    loc_0x10083bde:
        ecx = dword [edi + 4]
        ecx -= dword [edi]
        eax = 0x2aaaaaab
        eax = eax * ecx
        edx >>= 1
        eax = edx
        eax >>>= 0x1f
        eax += edx
        if (!v) goto loc_0x10083c62 // likely
        goto loc_0x10083bf5;
    loc_0x10083c62:
        // CODE XREFS from method.ConditionCurrentAnimation.virtual_8 @ 0x10083bf3(x), 0x10083c08(x), 0x10083c35(x)
        v = byte [esi + 0xc] - 0
        if (!v) goto loc_0x10083c8c // unlikely
        return eax;
    loc_0x10083c8c:
        // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c66(x)
        edi = pop ()
        esi = pop ()
        eax = bl
        ebx = pop ()
        ebp = pop ()
        return
    loc_0x10083bf5: // orphan
         eax = dword [arg_8h]
         eax += 0x14              // 20
         push (eax)
         push (dword [edi])
         fcn.10083ca0 ()          // fcn.10083ca0(0x14, 0x0, 0x0, 0x0, 0x0)
         esp += 8
         bl = al
         
         goto loc_0x10083c0a;
    loc_0x10083c0a: // orphan
         // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083bdc(x)
         v = byte [esi + 0x18] - 0
         if (!v) 
         goto loc_0x10083c10;
    loc_0x10083c10: // orphan
         ecx = dword [edi + 4]
         ecx -= dword [edi]
         eax = 0x2aaaaaab
         eax = eax * ecx
         edx >>= 1
         eax = edx
         eax >>>= 0x1f
         eax += edx
         
         goto loc_0x10083c27;
    loc_0x10083c27: // orphan
         // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c0e(x)
         push (dword [esi + 0x14])
         push (edi)
         fcn.10083d60 ()          // fcn.10083d60(0x0, 0x0, 0x0, 0x0, 0x0)
         esp += 8
         bl = al
         
         goto loc_0x10083c37;
    loc_0x10083c3f: // orphan
         eax--
         if (!v) 
         goto loc_0x10083c42;
    loc_0x10083c42: // orphan
         eax--
         if (!v) 
         goto loc_0x10083c45;
    loc_0x10083c45: // orphan
         push (ecx)
         push (str.ConditionCurrentAnimation:_getAnimationNames___wrong_type:__i) // 0x103824b8 // "ConditionCurrentAnimation: getAnimationNames - wrong type: %i" // (pstr 0x103824b8) "ConditionCurrentAnimation: getAnimationNames - wrong type: %i"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         esp += 8

    loc_0x10083c5f: // orphan
         // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c25(x)
         bl = v == 0

    loc_0x10083c68: // orphan
         edi = pop ()
         eax = 0
         v = bl & bl
         esi = pop ()
         al = v == 0
         ebx = pop ()
         ebp = pop ()
         return

    loc_0x10083c76: // orphan
         // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c43(x)
         eax = dword [arg_8h]
         al = byte [eax + 0xa2]
         
         goto loc_0x10083c81;
    loc_0x10083c81: // orphan
         // CODE XREF from method.ConditionCurrentAnimation.virtual_8 @ 0x10083c40(x)
         eax = dword [arg_8h]
         al = byte [eax + 0xa1]
         
         return eax;
}

