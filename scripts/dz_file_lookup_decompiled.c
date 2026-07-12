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
void fcn.102c9e41 (int32_t arg1, int32_t arg_8h, int32_t arg_ch) {
        // CALL XREF from fcn.102ca3bc @ +0x12f(x)
        push (ebp)
        ebp = esp
        esp -= 0x10c
        eax = dword [0x10653c5c] // [0x10653c5c:4]=0xbb40e64e
        eax ^= ebp
        dword [var_4h] = eax
        push (esi)
        esi = arg_ch  // arg3
        v = byte [esi + 0x74] - 0
        if (v) goto loc_0x102c9e64 // likely
        goto loc_0x102c9e5d;
    loc_0x102c9e64:
        // CODE XREF from fcn.102c9e41 @ 0x102c9e5b(x)
        push (edi)
        edi = dword [arg_8h]
        v = byte [edi] - 0x5c // '\\'
        if (v) goto loc_0x102c9e6e // likely
        goto loc_0x102c9e6d;
    loc_0x102c9e6e:
        // CODE XREF from fcn.102c9e41 @ 0x102c9e6b(x)
        ecx = dword [arg_ch]
        v = byte [ecx] - 0x72 // 'r'
        if (v) goto loc_0x102c9e7e // likely
        goto loc_0x102c9e76;
    loc_0x102c9e7e:
        // CODE XREF from fcn.102c9e41 @ 0x102c9e74(x)
        push (3)      // 3
        push (0x10375008) // "rb" // (pstr 0x10375008) "rb"
        push (ecx)
        sub.MSVCR110.dll_memcmp ()
        esp += 0xc
        v = eax & eax
        if (!v) goto loc_0x102c9e99 // likely
        goto loc_0x102c9e92;
    loc_0x102c9e99:
        // CODE XREFS from fcn.102c9e41 @ 0x102c9e7c(x), 0x102c9e90(x)
        al = byte [edi]
        ecx = var_104h
        byte [var_104h] = al
        
    while (al & al) {
        // CODE XREF from fcn.102c9e41 @ 0x102c9eb9(x)
        edi++
        v = byte [ecx] - 0x2f // '/'
        if (v) goto loc_0x102c9eb2 // likely
        byte [ecx] = 0x5c // '\\' // [0x5c:1]=255 // 92
        // CODE XREF from fcn.102c9e41 @ 0x102c9ead(x)
        al = byte [edi]
        ecx++
        byte [ecx] = al
    }
    loc_0x102c9ebb:
        push (ebx)
        eax = var_104h
        push (0x5c)   // '\\' // 92 // "\"
        push (eax)
        sub.MSVCR110.dll_strrchr ()
        ecx = pop ()
        edx = var_104h
        ebx = 0
        ecx = pop ()  // "\"
        dword [var_108h] = edx
        v = eax & eax
        if (!v) goto loc_0x102c9eef // unlikely
        goto loc_0x102c9ede;
    loc_0x102c9eef:
        // CODE XREF from fcn.102c9e41 @ 0x102c9edc(x)
        v = dword [esi + 0x80] - 0
        if (v) goto loc_0x102c9f03 // likely
        goto loc_0x102c9ef8;
    loc_0x102c9f03:
        // CODE XREFS from fcn.102c9e41 @ 0x102c9ef6(x), 0x102c9efa(x)
        ecx = dword [esi + 0x78]
        edi = 0
        
    loc_0x102c9f08:
        // CODE XREF from fcn.102c9e41 @ 0x102c9f44(x)
        push (edx)
        push (ecx)
        dword [var_10ch] = ecx
        fcn.102f7930 ()
        ecx = pop ()
        ecx = pop ()
        v = eax & eax
        if (v) goto loc_0x102c9f28 // unlikely
        goto loc_0x102c9f1b;
        goto loc_0x102c9efc;
    loc_0x102c9e92:
        eax = 0
        goto loc_0x102c9faf
        goto loc_0x102c9e7e;
    loc_0x102c9e6d: // orphan
         edi++

    loc_0x102c9e76: // orphan
         eax = byte [ecx + 1]
         v = al & al
         if (!v) 
         goto loc_0x102c9e7e;
    loc_0x102c9ede: // orphan
         edx = eax + 1
         byte [eax] = bl
         dword [var_108h] = edx
         ebx = var_104h

    loc_0x102c9ef8: // orphan
         v = ebx & ebx
         if (!v) 
         goto loc_0x102c9efc;
    loc_0x102c9efc: // orphan
         eax = 0
         v = byte [ebx] - al
         if (!v) ebx = eax

    loc_0x102c9f1b: // orphan
         push (ebx)
         push (edi)
         ecx = esi
         fcn.102c9705 ()          // fcn.102c9705(0x0, 0x0, 0x0, 0x0)
         v = al & al
         if (v) 
         goto loc_0x102c9f28;
    loc_0x102c9f28: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f19(x)
         eax = word [esi + 0x6c]
         edi++
         v = edi - eax
         if (!v) 
         goto loc_0x102c9f31;
    loc_0x102c9f31: // orphan
         ecx = dword [var_10ch]

    loc_0x102c9f37: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f3c(x)
         al = byte [ecx]
         ecx++
         v = al & al
         if (v) 
         goto loc_0x102c9f3e;
    loc_0x102c9f3e: // orphan
         edx = dword [var_108h]
         
         goto loc_0x102c9f46;
    loc_0x102c9f46: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f26(x)
         ebx = dword [esi + 0x8c]
         
         goto loc_0x102c9f4e;
    loc_0x102c9f4e: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f61(x)
         edi--

    loc_0x102c9f4f: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f5d(x)
         ax = word [ebx]
         ecx = 0xffff
         ebx += 2
         v = ax - cx
         if (v) 
         goto loc_0x102c9f5f;
    loc_0x102c9f5f: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f4c(x)
         v = edi & edi
         if (v) 
         goto loc_0x102c9f63;
    loc_0x102c9f63: // orphan
         push (0x24)              // '$' // 36 // "$"
         fcn.102c96db ()
         edx = eax
         eax = 0
         ecx = pop ()             // "$"
         ecx = ebx + 2
         dword [edx + 0x20] = esi
         dword [edx] = eax
         dword [edx + 0xc] = ecx
         dword [edx + 4] = eax
         dword [edx + 8] = eax
         byte [edx + 0x1c] = al
         dword [edx + 0x10] = eax
         dword [edx + 0x14] = eax
         v = dword [esi + 0x98] - eax
         if (v) 
         goto loc_0x102c9f91;
    loc_0x102c9f91: // orphan
         dword [esi + 0x98] = edx
         
         goto loc_0x102c9f99;
    loc_0x102c9f99: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f8f(x)
         eax = dword [esi + 0x94]
         dword [eax + 8] = edx

    loc_0x102c9fa2: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f97(x)
         dword [esi + 0x94] = edx
         eax = edx
         
         goto loc_0x102c9fac;
    loc_0x102c9fac: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9f2f(x)
         eax = 0

    loc_0x102c9fae: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9faa(x)
         ebx = pop ()

    loc_0x102c9faf: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9e94(x)
         edi = pop ()

    loc_0x102c9fb0: // orphan
         // CODE XREF from fcn.102c9e41 @ 0x102c9e5f(x)
         ecx = dword [var_4h]
         ecx ^= ebp
         esi = pop ()
         fcn.102fff32 ()          // ebp // fcn.102fff32(0x0)
         leave                    // ebp
         return

}

