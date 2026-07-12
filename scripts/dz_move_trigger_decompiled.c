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
void fcn.1015acd0 (int32_t arg1, int32_t arg2) {
        // CALL XREF from fcn.100b9ff0 @ 0x100ba380(x)
        push (esi)
        esi = ecx     // arg3
        ecx = dword [esi + 0x1a0]
        v = byte [ecx + 0x62] - 0
        if (v) goto loc_0x1015ace8 // likely
        goto loc_0x1015acdf;
    loc_0x1015ace8:
        // CODE XREF from fcn.1015acd0 @ 0x1015acdd(x)
        v = dword [esi + 0x1bc] - 2
        if (v) goto loc_0x1015ad0f // likely
        goto loc_0x1015acf1;
    loc_0x1015ad0f:
        // CODE XREFS from fcn.1015acd0 @ 0x1015ace6(x), 0x1015acef(x)
        esi = pop ()
        return
        goto loc_0x1015ad05;
        goto loc_0x1015ace8;
        return eax;
    loc_0x1015acf1: // orphan
         push (edi)
         fcn.10171d40 ()          // method.CCLabelBMFontExtended.virtual_444 // fcn.10171d40(0x0)
         edi = eax
         ecx = edi
         fcn.10028f80 ()          // method.Trigger.virtual_24 // fcn.10028f80(0x0)
         v = dword [eax] - 1
         if (v) 
         goto loc_0x1015ad05;
    loc_0x1015ad05: // orphan
         push (1)                 // 1
         ecx = esi
         fcn.1015a370 ()          // fcn.1015a370(0x0, 0x0)

    loc_0x1015ad0e: // orphan
         // CODE XREF from fcn.1015acd0 @ 0x1015ad1b(x)
         edi = pop ()

    loc_0x1015ad11: // orphan
         // CODE XREF from fcn.1015acd0 @ 0x1015ad03(x)
         ecx = edi
         fcn.10028f80 ()          // method.Trigger.virtual_24 // fcn.10028f80(0x0)
         v = dword [eax] - 2
         if (v) 
         goto loc_0x1015ad1d;
    loc_0x1015ad1d: // orphan
         edi = pop ()
         ecx = esi
         esi = pop ()
         
}

