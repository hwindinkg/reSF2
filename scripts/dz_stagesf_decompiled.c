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
void method.StageSF.virtual_328 (int32_t arg1) {
        push (esi)
        push (edi)
        edi = ecx     // arg3
        v = byte [edi + 0x11c] - 0
        if (!v) goto loc_0x10231b6e // unlikely
        goto loc_0x10231a51;
    loc_0x10231b6e:
        // CODE XREF from method.StageSF.virtual_328 @ 0x10231a4b(x)
        eax = dword [edi + 0x118]
        v = eax - 7   // 7
        if (v >= 0) goto loc_0x10231c24 // unlikely
        goto loc_0x10231b7d;
    loc_0x10231c24:
        // CODE XREF from method.StageSF.virtual_328 @ 0x10231b77(x)
        edi = pop ()
        esi = pop ()
        return
    switch (eax) { // jump table of 7 cases at 0x10231c28
        case 0: // 0x10231b8d
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            fcn.100d7d30 () // fcn.100d7d30(0x0)
            fcn.100d8ec0 ()
            fcn.100d8680 () // fcn.100d8680(0x0)
            fcn.100d5eb0 () // fcn.100d5eb0(0x0)
            dword [edi + 0x118]++
            edi = pop ()
            esi = pop ()
            return
        case 1: // 0x10231baa
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            fcn.10240970 ()
            dword [edi + 0x118]++
            edi = pop ()  // ebp
            esi = pop ()
            return
        case 2: // 0x10231bb8
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            fcn.100d8370 () // fcn.100d8370(0x0)
            dword [edi + 0x118]++
            edi = pop ()  // ebp
            esi = pop ()
            return
        case 3: // 0x10231bc6
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            push (str.assets_devices.xml) // 0x105cde54 // "assets/devices.xml" // (pstr 0x105cde54) "assets/devices.xml"
            fcn.10240cd0 () // fcn.10240cd0(0x0)
            esp += 4      // (pstr 0x105cde54) "assets/devices.xml"
            dword [edi + 0x118]++
            edi = pop ()  // ebp // "assets/devices.xml" str.assets_devices.xml
            esi = pop ()
            return
        case 4: // 0x10231bdc
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            esi = dword [edi]
            fcn.10268350 () // fcn.10268350(0x0)
            push (eax)
            ecx = edi
            dword [esi + 0xc4] () // 196 // 0xc4(0x0, 0x0, 0x0, 0x0)
            dword [edi + 0x118]++
            edi = pop ()
            esi = pop ()
            return        // ebp
        case 5: // 0x10231bf5
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            esi = dword [edi]
            fcn.10177380 () // fcn.10177380(0x0)
            push (eax)
            ecx = edi
            dword [esi + 0xc4] () // 196 // 0xc4(0x0, 0x0, 0x0, 0x0)
            dword [edi + 0x118]++
            edi = pop ()
            esi = pop ()
            return        // ebp
        case 6: // 0x10231c0e
            // CODE XREF from method.StageSF.virtual_328 @ 0x10231b86(x)
            push (1)      // 1
            push (0)
            push (0)
            push (0)
            fcn.101779c0 () // fcn.101779c0(0x0, 0x0, 0x0, 0x0, 0x0, 0x0)
            esp += 0x10
            break;
        default: // 0x10231c1e
            // CODE XREFS from method.StageSF.virtual_328 @ 0x10231b80(x), 0x10231b86(x)
            dword [edi + 0x118]++
            break;
    }
    loc_0x10231a7b: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx + 4] ()       // 4 // 0x4(0x0, 0x0, 0x0, -1)

    loc_0x10231a84: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231a79(x)
         fcn.1012c6a0 ()          // fcn.1012c6a0(0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10231a8d;
    loc_0x10231a8d: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231a95: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231a8b(x)
         fcn.10058f70 ()          // fcn.10058f70(0x0, 0x0)
         fcn.10114a90 ()          // fcn.10114a90(0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10231aa3;
    loc_0x10231aa3: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231aab: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231aa1(x)
         push (0)
         fcn.10069790 ()          // fcn.10069790(0x0)
         esp += 4
         v = eax & eax
         if (!v) 
         goto loc_0x10231ab9;
    loc_0x10231ab9: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231ac1: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231ab7(x)
         fcn.1020bff0 ()          // fcn.1020bff0(0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10231aca;
    loc_0x10231aca: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231ad2: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231ac8(x)
         fcn.1008b590 ()
         v = eax & eax
         if (!v) 
         goto loc_0x10231adb;
    loc_0x10231adb: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231ae3: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231ad9(x)
         fcn.1022edd0 ()          // fcn.1022edd0(0x0)
         v = eax & eax
         if (!v) 
         goto loc_0x10231aec;
    loc_0x10231aec: // orphan
         edx = dword [eax]
         push (1)                 // 1
         ecx = eax
         dword [edx] ()           // 0xffffff01(0x0, 0x0, 0x0, -1)

    loc_0x10231af4: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231aea(x)
         fcn.100c6df0 ()          // fcn.100c6df0(0x0)
         esi = eax
         v = esi & esi
         if (!v) 
         goto loc_0x10231aff;
    loc_0x10231aff: // orphan
         ecx = esi
         fcn.100c61e0 ()          // fcn.100c61e0(0x0, 0x0)
         push (esi)
         fcn.102e3350 ()
         esp += 4

    loc_0x10231b0f: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231afd(x)
         push (ebx)
         fcn.1018e310 ()
         fcn.100c7070 ()
         ebx = eax
         ecx = dword [ebx + 4]
         eax = dword [ebx]
         v = ecx - ecx
         if (!v) 
         goto loc_0x10231b25;
    loc_0x10231b25: // orphan
         esi = ecx
         esi -= ecx
         push (esi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += esi

    loc_0x10231b36: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231b23(x)
         dword [ebx + 4] = eax
         fcn.100c7070 ()
         ecx = dword [eax + 0x10]
         ebx = eax + 0xc
         eax = dword [ebx]
         v = ecx - ecx
         if (!v) 
         goto loc_0x10231b4a;
    loc_0x10231b4a: // orphan
         esi = ecx
         esi -= ecx
         push (esi)
         push (ecx)
         push (eax)
         sub.MSVCR110.dll_memmove ()
         esp += 0xc
         eax += esi

    loc_0x10231b5b: // orphan
         // CODE XREF from method.StageSF.virtual_328 @ 0x10231b48(x)
         dword [ebx + 4] = eax
         fcn.10052980 ()          // fcn.10052980(0x0, 0x0)
         dword [edi + 0x118] = 0xfffffffb // [0xfffffffb:4]=-1 // 4294967291
         ebx = pop ()             // ebp

}

