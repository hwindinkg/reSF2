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
void fcn.102ca3bc (int32_t arg1, int32_t arg_8h) {
        // CALL XREF from fcn.100a7de0 @ 0x100a804b(x)
        // CALL XREF from fcn.100a8870 @ 0x100a89d8(x)
        // CALL XREF from fcn.100d8680 @ 0x100d8e82(x)
        // CALL XREF from fcn.1012b960 @ 0x1012bb54(x)
        push (ebp)
        ebp = esp
        esp -= 0x104
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_4h] = eax
        edx = dword [arg_8h]
        ecx = var_104h
        al = byte [edx]
        byte [var_104h] = al
        
    while (al & al) {
        // CODE XREF from fcn.102ca3bc @ 0x102ca3f2(x)
        edx++
        v = byte [ecx] - 0x2f // '/'
        if (v) goto loc_0x102ca3eb // likely
        byte [ecx] = 0x5c // '\\' // [0x5c:1]=255 // 92
        // CODE XREF from fcn.102ca3bc @ 0x102ca3e6(x)
        al = byte [edx]
        ecx++
        byte [ecx] = al
    }
    loc_0x102ca3f4:
        edx = dword [0x10667318] // [0x10667318:4]=0
        push (esi)
        esi = 0
        v = edx & edx
        if (v <= 0) goto loc_0x102ca495 // likely
        goto loc_0x102ca405;
    loc_0x102ca495:
        // CODE XREF from fcn.102ca3bc @ 0x102ca3ff(x)
        ecx = dword [var_4h]
        ecx ^= ebp
        esi = pop ()
        fcn.102fff32 () // ebp // fcn.102fff32(0x0)
        leave         // ebp
        return
    loc_0x102ca406: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca449(x)
         eax = dword [esi*4 + 0x10667298]
         eax = dword [eax + 0x84]
         v = eax & eax
         if (!v) 
         goto loc_0x102ca417;
    loc_0x102ca417: // orphan
         ecx = var_104h

    loc_0x102ca41d: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca437(x)
         bl = byte [eax]
         v = bl - byte [ecx]
         if (v) 
         goto loc_0x102ca423;
    loc_0x102ca423: // orphan
         v = bl & bl
         if (!v) 
         goto loc_0x102ca427;
    loc_0x102ca427: // orphan
         bl = byte [eax + 1]
         v = bl - byte [ecx + 1]
         if (v) 
         goto loc_0x102ca42f;
    loc_0x102ca42f: // orphan
         eax += 2
         ecx += 2
         v = bl & bl
         if (v) 
         goto loc_0x102ca439;
    loc_0x102ca439: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca425(x)
         eax = 0
         
         goto loc_0x102ca43d;
    loc_0x102ca43d: // orphan
         // CODE XREFS from fcn.102ca3bc @ 0x102ca421(x), 0x102ca42d(x)
         eax = eax - eax
         eax |= 1

    loc_0x102ca442: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca43b(x)
         v = eax & eax
         if (!v) 
         goto loc_0x102ca446;
    loc_0x102ca446: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca415(x)
         esi++
         v = esi - edx
         jl 0x102ca406            // unlikely

         goto loc_0x102ca44b;
    loc_0x102ca44b: // orphan
         
         goto loc_0x102ca44d;
    loc_0x102ca44d: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca444(x)
         push (edi)
         edi = dword [esi*4 + 0x10667298]
         v = edi & edi
         if (!v) 
         goto loc_0x102ca459;
    loc_0x102ca459: // orphan
         ecx = edi
         fcn.102c9618 ()          // fcn.102c9618(0x0)
         push (edi)
         fcn.102c96f0 ()
         edx = dword [0x10667318] // [0x10667318:4]=0
         ecx = pop ()

    loc_0x102ca46d: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca457(x)
         eax = esi + 1
         v = eax - edx
         if (v >= 0) 
         goto loc_0x102ca474;
    loc_0x102ca474: // orphan
         esi = eax*4 + 0x10667298
         ecx = edx
         edi = esi - 4
         ecx -= eax
         rep movsd dword es:[edi] dword [esi]

    loc_0x102ca484: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca472(x)
         edx--
         dword [0x10667318] = edx // [0x10667318:4]=0
         dword [edx*4 + 0x10667298] &= 0 // [0x10667298:4]=0
         edi = pop ()

    loc_0x102ca494: // orphan
         // CODE XREF from fcn.102ca3bc @ 0x102ca44b(x)
         ebx = pop ()

}

