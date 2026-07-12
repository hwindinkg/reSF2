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
void fcn.10159310 (int32_t arg1, int32_t arg2) {
        // CALL XREF from fcn.10070b70 @ 0x10070bb0(x)
        // CALL XREF from method.ModelContainer.virtual_328 @ 0x1016784b(x)
        // CALL XREF from method.PerkConditionCurrentAnimation.virtual_8 @ 0x10196738(x)
        push (esi)
        esi = ecx     // arg3
        ecx = dword [esi + 0x590]
        fcn.10006ef0 () // method.cocos2d::CCAnimation.virtual_40 // fcn.10006ef0(0x0)
        v = al & al
        if (!v) goto loc_0x1015932e // likely
        goto loc_0x10159322;
    loc_0x1015932e:
        // CODE XREF from fcn.10159310 @ 0x10159320(x)
        ecx = dword [esi + 0x598]
        fcn.1002c4c0 () // method.TextureVideo.virtual_28 // fcn.1002c4c0(0x0)
        v = eax & eax
        if (!v) goto loc_0x10159349 // likely
        goto loc_0x1015933d;
    loc_0x10159349:
        // CODE XREF from fcn.10159310 @ 0x1015933b(x)
        eax |= 0xffffffff // -1
        esi = pop ()
        return
    loc_0x1015933d: // orphan
         ecx = dword [esi + 0x598]
         esi = pop ()
         
         return eax;
}

