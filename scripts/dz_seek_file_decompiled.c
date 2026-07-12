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
void fcn.102ca8a7 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        // CALL XREFS from fcn.102c9fbf @ 0x102ca0a1(x), 0x102ca169(x)
        push (ebp)
        ebp = esp
        push (ecx)    // arg3
        push (ebx)    // arg2
        push (esi)
        push (edi)
        esi = ecx     // arg3
        edi = 0
        v = dword [esi + 0x5c] - edi
        if (v <= 0) goto loc_0x102ca8e2 // likely
        goto loc_0x102ca8b7;
    loc_0x102ca8e2:
        // CODE XREF from fcn.102ca8a7 @ 0x102ca8b5(x)
        push (3)      // 3
        eax = pop ()  // ebp
        
    loc_0x102ca8e5:
        // CODE XREFS from fcn.102ca8a7 @ 0x102ca8fe(x), 0x102ca909(x)
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        leave         // ebp
        return
        return eax;
    loc_0x102ca8c3: // orphan
         // CODE XREF from fcn.102ca8a7 @ 0x102ca8e0(x)
         eax = dword [ebx]
         push (eax)
         dword [eax + 0xc] ()     // 12 // 0xc(-1, 0x0, 0x0, 0x0)
         edx = dword [var_4h]
         ecx = pop ()
         ecx = dword [esi + 8]
         ecx = word [ecx + edx + 0xc]
         v = ecx & eax
         if (v) 
         goto loc_0x102ca8d9;
    loc_0x102ca8d9: // orphan
         edi++
         ebx += 4
         v = edi - dword [esi + 0x5c]
         jl 0x102ca8c3            // unlikely

         goto loc_0x102ca8e2;
    loc_0x102ca8ec: // orphan
         // CODE XREF from fcn.102ca8a7 @ 0x102ca8d7(x)
         eax = dword [esi + edi*4 + 0x1c]
         push (dword [arg_8h])
         push (eax)
         dword [eax + 8] ()       // 8 // 0x8(-1, 0x0, 0x0, 0x0)
         ecx = pop ()
         ecx = pop ()
         dword [esi + 0x60] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102ca900;
    loc_0x102ca900: // orphan
         eax = dword [esi + edi*4 + 0x1c]
         dword [esi + 4] = eax
         eax = 0
         
}

