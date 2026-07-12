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
void method.ConditionInterval.virtual_8 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        esp -= 0xc
        eax = dword [arg_8h]
        push (ebx)    // arg2
        push (esi)
        esi = dword [eax + 8]
        edx = ecx     // arg3
        cl = 0
        dword [var_8h] = edx
        byte [var_1h] = cl
        v = esi & esi
        if (!v) goto loc_0x10086c16 // unlikely
        goto loc_0x10086bac;
    loc_0x10086c16:
        // CODE XREFS from method.ConditionInterval.virtual_8 @ 0x10086baa(x), 0x10086c35(x)
        eax = dword [var_8h]
        esi = pop ()
        v = byte [eax + 0xc] - 0
        ebx = pop ()
        if (!v) goto loc_0x10086c37 // unlikely
        return eax;
    loc_0x10086c37:
        // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086c1f(x)
        eax = cl
        esp = ebp
        ebp = pop ()
        return
        goto loc_0x10086bbf;
        return eax;
    loc_0x10086bbf:
        push (edi)
        edi = dword [esi]
        v = esi - ecx
        jae 0x10086c12 // likely
        goto loc_0x10086bc6;
    loc_0x10086c12:
        // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086bc4(x)
        cl = byte [var_1h]
        
    loc_0x10086c15:
        // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086c30(x)
        edi = pop ()
        return eax;
    loc_0x10086bc6: // orphan
         eax = dword [edx + 0x14]
         dword [arg_8h] = eax
         esp = esp                // ebp

    loc_0x10086bd0: // orphan
         // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086c10(x)
         v = eax & eax
         if (!v) 
         goto loc_0x10086bd4;
    loc_0x10086bd4: // orphan
         v = eax - dword [edi + 0x18]
         if (v) 
         goto loc_0x10086bd9;
    loc_0x10086bd9: // orphan
         // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086bd2(x)
         ebx = edx + 0x1c
         push (0x10374b40)        // '@K7\x10'
         push (ebx)
         fcn.1000cc00 ()          // fcn.1000cc00(0x0, 0x1c)
         esp += 8
         v = al & al
         if (v) 
         goto loc_0x10086bee;
    loc_0x10086bee: // orphan
         eax = edi + 0xc
         push (ebx)
         push (eax)
         fcn.1000cb90 ()          // fcn.1000cb90(0xc, 0x0)
         esp += 8
         v = al & al
         if (v) 
         goto loc_0x10086bff;
    loc_0x10086bff: // orphan
         eax = dword [arg_8h]
         ecx = dword [var_ch]
         edx = dword [var_8h]

    loc_0x10086c08: // orphan
         // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086bd7(x)
         edi = dword [esi + 4]
         esi += 4
         v = esi - ecx
         if (((unsigned) v) < 0) 
         goto loc_0x10086c12;
    loc_0x10086c21: // orphan
         eax = 0
         v = cl & cl
         al = v == 0
         esp = ebp
         ebp = pop ()
         return

    loc_0x10086c2e: // orphan
         // CODE XREFS from method.ConditionInterval.virtual_8 @ 0x10086bec(x), 0x10086bfd(x)
         cl = 1
         
         goto loc_0x10086c32;
    loc_0x10086c32: // orphan
         // CODE XREF from method.ConditionInterval.virtual_8 @ 0x10086bbd(x)
         cl = byte [var_1h]
         
         return eax;
}

