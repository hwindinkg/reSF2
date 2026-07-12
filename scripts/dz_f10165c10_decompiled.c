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
void fcn.10165c10 (int32_t arg1) {
        // CALL XREF from fcn.10164fa0 @ 0x10165115(x)
        push (esi)
        esi = ecx     // arg3
        push (edi)
        eax = dword [esi + 0x20]
        eax = dword [eax + 0x94]
        edi = dword [eax + 0x70]
        v = edi - 0xffffffff
        if (v <= 0) goto loc_0x10165c3e // likely
        return eax;
    loc_0x10165c3e:
        // CODE XREF from fcn.10165c10 @ 0x10165c23(x)
        push (str._animationInfo__moveInside__align.pivotID___1) // 0x105b21f8 // "_animationInfo->moveInside->align.pivotID == -1" // (pstr 0x105b21f8) "_animationInfo->moveInside->align.pivotID == -1"
        dword [esi + 0x5c] = 0
        fcn.101472f0 () // fcn.101472f0(0x0, 0x0)
        esp += 4      // (pstr 0x105b21f8) "_animationInfo->moveInside->align.pivotID == -1"
        edi = pop ()  // ebp // "_animationInfo->moveInside->align.pivotID == -1" str._animationInfo__moveInside__align.pivotID___1
        esi = pop ()
        return
}

