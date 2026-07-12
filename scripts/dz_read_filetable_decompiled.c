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
void fcn.102ca66b (int32_t arg1, int32_t arg2, int32_t arg_8h, uint32_t arg_ch) {
        // CALL XREF from fcn.102c9778 @ 0x102c9a85(x)
        push (ebp)
        ebp = esp
        esp -= 0x10
        push (ebx)    // arg2
        ebx = dword [arg_8h]
        push (esi)
        esi = arg_ch  // arg3
        push (edi)
        dword [var_ch] = esi
        v = ebx & ebx
        if (!v) goto loc_0x102ca89d // likely
        goto loc_0x102ca684;
    loc_0x102ca89d:
        // CODE XREFS from fcn.102ca66b @ 0x102ca67e(x), 0x102ca689(x), 0x102ca6a3(x), 0x102ca6ea(x), 0x102ca79e(x)
        push (2)      // 2
        eax = pop ()  // ebp
        
    loc_0x102ca8a0:
        // CODE XREFS from fcn.102ca66b @ 0x102ca6bd(x), 0x102ca762(x), 0x102ca87a(x), 0x102ca89b(x)
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        leave         // ebp
        return
        goto loc_0x102ca68f;
        return eax;
    loc_0x102ca68f:
        push (ebx)
        push (1)      // 1
        push (4)      // 4
        push (esi)
        dword [esi + 0x18] = eax
        fcn.102fbfc0 () // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
        esp += 0x10
        v = eax - 1   // 1
        if (v) goto loc_0x102ca89d // likely
        goto loc_0x102ca6a9;
    loc_0x102ca6a9:
        ecx = word [esi + 2]
        eax = 0
        push (3)      // 3
        v = cx & cx
        edx = pop ()  // ebp
        if (!v) eax = edx
        dword [esi + 0x60] = eax
        v = eax & eax
        if (v) goto loc_0x102ca8a0 // unlikely
        goto loc_0x102ca6c3;
    loc_0x102ca6c3:
        eax = ecx
        eax <<<= 4
        push (eax)
        fcn.102c96db ()
        ecx = eax
        eax = word [esi + 2]
        push (ebx)
        push (eax)
        push (0x10)   // 16
        push (ecx)
        dword [esi + 8] = ecx
        fcn.102fbfc0 () // fcn.102fbfc0(0xffff, 0x0, 0x0, 0x0)
        ecx = word [esi + 2]
        esp += 0x14
        v = eax - ecx
        if (v) goto loc_0x102ca89d // unlikely
        goto loc_0x102ca6f0;
    loc_0x102ca6f0:
        eax = word [esi]
        eax <<<= 2
        push (eax)
        fcn.102c96db ()
        dword [esi + 0x10] = eax
        eax = word [esi]
        eax <<<= 2
        push (eax)
        fcn.102c96db ()
        ecx = pop ()
        dword [esi + 0x14] = eax
        eax = word [esi]
        ecx = pop ()
        push (0)
        edi = pop ()
        eax--
        if (!v) goto loc_0x102ca767 // unlikely
        goto loc_0x102ca719;
    loc_0x102ca767:
        // CODE XREFS from fcn.102ca66b @ 0x102ca717(x), 0x102ca731(x)
        dword [esi + 0xc] &= 0
        
    loc_0x102ca76b:
        // CODE XREF from fcn.102ca66b @ 0x102ca75d(x)
        eax = 0
        ecx = 0
        v = ax - word [esi]
        jae 0x102ca783 // unlikely
        goto loc_0x102ca774;
        return eax;
    loc_0x102ca719: // orphan
         esi = eax

    loc_0x102ca71b: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca72a(x)
         push (ebx)
         edi++
         fcn.102ff730 ()          // fcn.102ff730(0x0)
         ecx = pop ()
         v = eax & eax
         if (v) 
         goto loc_0x102ca727;
    loc_0x102ca727: // orphan
         esi--

    loc_0x102ca728: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca725(x)
         v = esi & esi
         if (v) 
         goto loc_0x102ca72c;
    loc_0x102ca72c: // orphan
         esi = dword [var_ch]
         v = edi & edi
         if (!v) 
         goto loc_0x102ca733;
    loc_0x102ca733: // orphan
         push (edi)
         fcn.102c96db ()
         v = dword [esi + 0x10] - 0
         ecx = pop ()
         dword [esi + 0xc] = eax
         if (!v) 
         goto loc_0x102ca743;
    loc_0x102ca743: // orphan
         v = eax & eax
         if (!v) 
         goto loc_0x102ca747;
    loc_0x102ca747: // orphan
         v = dword [esi + 0x14] - 0
         if (!v) 
         goto loc_0x102ca74d;
    loc_0x102ca74d: // orphan
         eax = edi
         push (1)
         eax ~= eax
         push (eax)
         push (ebx)
         fcn.102ff760 ()          // fcn.102ff760(0x0, 0x0, 0x0)
         esp += 0xc
         
         goto loc_0x102ca75f;
    loc_0x102ca75f: // orphan
         // CODE XREFS from fcn.102ca66b @ 0x102ca741(x), 0x102ca745(x), 0x102ca74b(x)
         eax = 0
         eax++
         
         goto loc_0x102ca767;
    loc_0x102ca774: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca781(x)
         eax = dword [esi + 0x10]
         dword [eax + ecx*4] &= 0
         eax = word [esi]
         ecx++
         v = ecx - eax
         jl 0x102ca774            // likely

         goto loc_0x102ca783;
    loc_0x102ca783: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca772(x)
         eax = dword [esi + 0x10]
         dword [eax] = ebx
         v = edi & edi
         if (v <= 0) 
         goto loc_0x102ca78c;
    loc_0x102ca78c: // orphan
         push (ebx)
         push (1)
         push (edi)
         push (dword [esi + 0xc])
         fcn.102fbfc0 ()          // fcn.102fbfc0(0x0, 0x0, 0x0, 0x0)
         esp += 0x10
         v = eax - 1
         if (v) 
         goto loc_0x102ca7a4;
    loc_0x102ca7a4: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca78a(x)
         dword [var_8h] &= 0
         eax = var_8h
         push (eax)
         push (str.MinBufSize)    // 0x105fb920 // "MinBufSize" // (pstr 0x105fb920) "MinBufSize"
         push (str.derbh)         // 0x105fb914 // "derbh" // (pstr 0x105fb914) "derbh"
         fcn.102fc640 ()          // fcn.102fc640(0x177ff8, 0x0, 0x0)
         ecx = word [esi + 2]
         esp += 0xc
         v = ecx & ecx
         if (v <= 0) 
         goto loc_0x102ca7c6;
    loc_0x102ca7c6: // orphan
         eax = dword [esi + 8]
         edx = dword [var_8h]     // "MinBufSize" str.MinBufSize
         eax += 8
         edi = ecx

    loc_0x102ca7d1: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca7e8(x)
         ebx = 0x400              // 1024
         v = word [eax + 4] & bx
         if (!v) 
         goto loc_0x102ca7dc;
    loc_0x102ca7dc: // orphan
         v = dword [eax] - edx
         cmova edx dword [eax]
         dword [var_8h] = edx

    loc_0x102ca7e4: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca7da(x)
         eax += 0x10              // 16
         edi--
         if (v) 
         goto loc_0x102ca7ea;
    loc_0x102ca7ea: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca7c4(x)
         edi = 0
         dword [arg_ch] = edi
         v = ecx & ecx
         if (v <= 0) 
         goto loc_0x102ca7f3;
    loc_0x102ca7f3: // orphan
         eax = dword [esi + 8]
         eax += 0xc
         dx = di

    loc_0x102ca7fc: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca803(x)
         dx |= word [eax]
         eax = eax + 0x10
         ecx--
         if (v) 
         goto loc_0x102ca805;
    loc_0x102ca805: // orphan
         word [arg_ch] = dx
         edi = dword [arg_ch]

    loc_0x102ca80c: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca7f1(x)
         edi &= 0xfbfe
         dword [arg_ch] = edi

    loc_0x102ca815: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca88d(x)
         eax = 0
         dword [var_10h] &= eax
         dword [var_4h] = 0x7fffffff
         dword [var_ch] = eax
         v = dword [esi + 0x5c] - eax
         if (v <= 0) 
         goto loc_0x102ca829;
    loc_0x102ca829: // orphan
         ebx = dword [var_10h]
         edi = esi + 0x1c

    loc_0x102ca82f: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca85a(x)
         eax = dword [edi]
         push (eax)
         dword [eax + 0xc] ()     // 0xc(-1, 0x0, 0x0, 0x0)
         ecx = pop ()
         v = eax - dword [var_4h]
         if (v >= 0) 
         goto loc_0x102ca83b;
    loc_0x102ca83b: // orphan
         ecx = word [arg_ch]
         v = eax & ecx
         if (!v) 
         goto loc_0x102ca843;
    loc_0x102ca843: // orphan
         ecx = ebx
         dword [var_4h] = eax
         dword [var_ch] = ecx
         
         goto loc_0x102ca84d;
    loc_0x102ca84d: // orphan
         // CODE XREFS from fcn.102ca66b @ 0x102ca839(x), 0x102ca841(x)
         ecx = dword [var_ch]
         eax = dword [var_4h]

    loc_0x102ca853: // orphan
         // CODE XREF from fcn.102ca66b @ 0x102ca84b(x)
         ebx++
         edi += 4
         v = ebx - dword [esi + 0x5c]
         jl 0x102ca82f            // unlikely

         goto loc_0x102ca85c;
    loc_0x102ca85c: // orphan
         ebx = dword [arg_8h]
         v = eax - 0x7fffffff
         if (!v) 
         goto loc_0x102ca866;
    loc_0x102ca866: // orphan
         push (dword [var_8h])
         eax = dword [esi + ecx*4 + 0x1c]
         push (ebx)
         push (esi)
         push (eax)
         dword [eax] ()           // 0xffffffff(-1, 0x0, 0x0, 0x0)
         esp += 0x10
         dword [esi + 0x60] = eax
         v = eax & eax
         if (v) 
         goto loc_0x102ca87c;
    loc_0x102ca87c: // orphan
         eax = dword [var_ch]
         eax = dword [esi + eax*4 + 0x1c]
         push (eax)
         dword [eax + 0xc] ()     // 0xc(-1, 0x0, 0x0, 0x0)
         eax = !eax
         dword [arg_ch] &= eax
         ecx = pop ()
         
         goto loc_0x102ca88f;
    loc_0x102ca88f: // orphan
         // CODE XREFS from fcn.102ca66b @ 0x102ca827(x), 0x102ca864(x)
         eax = 0
         v = word [arg_ch] - ax
         push (3)
         ecx = pop ()             // ebp
         if (!zf) eax = ecx
         
         goto loc_0x102ca89d;
}

