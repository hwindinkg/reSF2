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
void fcn.102c928c (int32_t arg1, int32_t arg_8h, int32_t arg_ch, int32_t arg_10h, int32_t arg_14h) {
        // CALL XREF from fcn.102c75dd @ 0x102c76df(x)
        // CALL XREF from fcn.102c7b52 @ 0x102c7c99(x)
        push (ebp)
        ebp = esp
        esp -= 0x10
        push (ebx)    // arg2
        push (esi)
        push (edi)
        edi = dword [arg_8h]
        v = edi & edi
        if (!v) goto loc_0x102c9437 // likely
        goto loc_0x102c92a0;
    loc_0x102c9437:
        // CODE XREFS from fcn.102c928c @ 0x102c929a(x), 0x102c92a5(x), 0x102c92b0(x), 0x102c9342(x)
        eax = 0
        
    loc_0x102c9439:
        // CODE XREF from fcn.102c928c @ 0x102c92f3(x)
        edi = pop ()
        esi = pop ()
        ebx = pop ()
        leave         // ebp
        return
        goto loc_0x102c92ab;
        return eax;
    loc_0x102c92ab:
        ecx = dword [arg_14h]
        v = ecx & ecx
        if (!v) goto loc_0x102c9437 // likely
        goto loc_0x102c92b6;
    loc_0x102c92b6:
        edx = dword [esi + 0x30]
        eax = dword [esi + 0x34]
        edx += ecx
        dword [var_ch] = eax
        v = edx - eax
        if (v <= 0) goto loc_0x102c933c // likely
        goto loc_0x102c92c5;
    loc_0x102c933c:
        // CODE XREF from fcn.102c928c @ 0x102c92c3(x)
        dword [var_ch] &= 0
        v = ecx & ecx
        if (v <= 0) goto loc_0x102c9437 // likely
        goto loc_0x102c9348;
    loc_0x102c9348:
        edx = dword [arg_10h]
        eax = dword [arg_14h]
        edx += 8
        dword [var_4h] = edx
        
    loc_0x102c9354:
        // CODE XREF from fcn.102c928c @ 0x102c9431(x)
        ebx = dword [esi + 0x30]
        ecx = dword [edx - 4]
        ebx <<<= 4
        ebx += dword [esi + 0x38]
        v = ecx & ecx
        if (!v) goto loc_0x102c9422 // unlikely
        goto loc_0x102c9368;
        goto loc_0x102c92cc;
        return eax;
    loc_0x102c92cc:
        eax = edx + 8
        dword [esi + 0x34] = eax
        eax <<<= 4
        push (eax)
        push (edi)
        fcn.102c4ed1 () // fcn.102c4ed1(0x80, 0x0)
        ecx = pop ()
        ecx = pop ()
        ecx = eax
        dword [esi + 0x38] = ecx
        v = ecx & ecx
        if (v) goto loc_0x102c92f8 // likely
        goto loc_0x102c92e7;
    loc_0x102c92f8:
        // CODE XREF from fcn.102c928c @ 0x102c92e5(x)
        eax = dword [var_ch]
        eax <<<= 4
        push (eax)
        push (ebx)
        push (ecx)
        fcn.102f7830 ()
        push (ebx)
        push (edi)
        fcn.102c4e1f () // fcn.102c4e1f(0x2c92dc0, 0x0)
        esp += 0x14
        goto loc_0x102c9339
        
    loc_0x102c9339:
        // CODE XREF from fcn.102c928c @ 0x102c9310(x)
        ecx = dword [arg_14h]
        return eax;
    loc_0x102c92c5: // orphan
         ebx = dword [esi + 0x38]
         v = ebx & ebx
         if (!v) 
    loc_0x102c92e7: // orphan
         push (ebx)
         push (edi)
         fcn.102c4e1f ()          // fcn.102c4e1f(0x0, 0x0)
         ecx = pop ()
         ecx = pop ()

    loc_0x102c92f0: // orphan
         // CODE XREFS from fcn.102c928c @ 0x102c932d(x), 0x102c93c1(x)
         eax = 0
         eax++
         
         goto loc_0x102c92f8;
    loc_0x102c9312: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c92ca(x)
         dword [esi + 0x30] &= 0
         eax = ecx + 8
         dword [esi + 0x34] = eax
         eax <<<= 4
         push (eax)
         push (edi)
         fcn.102c4ed1 ()          // fcn.102c4ed1(0x80, 0x0)
         ecx = pop ()
         ecx = pop ()
         dword [esi + 0x38] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x102c932f;
    loc_0x102c932f: // orphan
         dword [esi + 0xb8] |= 0x4000 // [0x4000:4]=-1

    loc_0x102c9368: // orphan
         eax = ecx + 1
         dword [var_8h] = eax

    loc_0x102c936e: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c9373(x)
         al = byte [ecx]
         ecx++
         v = al & al
         if (v) 
         goto loc_0x102c9375;
    loc_0x102c9375: // orphan
         ecx -= dword [var_8h]
         eax = dword [edx - 8]
         dword [var_8h] = ecx
         dword [var_10h] = eax
         v = eax & eax
         if (v > 0) 
         goto loc_0x102c9389;
    loc_0x102c9389: // orphan
         edi = dword [edx]
         v = edi & edi
         if (!v) 
         goto loc_0x102c938f;
    loc_0x102c938f: // orphan
         v = byte [edi] - 0
         if (!v) 
         goto loc_0x102c9394;
    loc_0x102c9394: // orphan
         edx = edi + 1

    loc_0x102c9397: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c939c(x)
         al = byte [edi]
         edi++
         v = al & al
         if (v) 
         goto loc_0x102c939e;
    loc_0x102c939e: // orphan
         eax = dword [var_10h]
         edi -= edx
         dword [ebx] = eax
         
         goto loc_0x102c93a7;
    loc_0x102c93a7: // orphan
         // CODE XREFS from fcn.102c928c @ 0x102c938d(x), 0x102c9392(x)
         edi = 0
         dword [ebx] |= 0xffffffff // [0xffffffff:4]=-1 // -1

    loc_0x102c93ac: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c93a5(x)
         eax = ecx + 4
         eax += edi
         push (eax)
         push (dword [arg_8h])
         fcn.102c4ed1 ()          // fcn.102c4ed1(0x4, 0x0)
         ecx = pop ()
         ecx = pop ()
         dword [ebx + 4] = eax
         v = eax & eax
         if (!v) 
         goto loc_0x102c93c7;
    loc_0x102c93c7: // orphan
         push (dword [var_8h])
         ecx = dword [var_4h]
         push (dword [ecx - 4])
         push (eax)
         fcn.102f7830 ()
         ecx = dword [var_8h]
         eax = dword [ebx + 4]
         esp += 0xc
         byte [ecx + eax] = 0
         ecx++
         ecx += dword [ebx + 4]
         dword [ebx + 8] = ecx
         v = edi & edi
         if (!v) 
         goto loc_0x102c93ee;
    loc_0x102c93ee: // orphan
         eax = dword [var_4h]
         push (edi)
         push (dword [eax])
         push (ecx)
         fcn.102f7830 ()
         esp += 0xc

    loc_0x102c93fd: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c93ec(x)
         eax = dword [ebx + 8]
         byte [edi + eax] = 0
         dword [ebx + 0xc] = edi
         dword [esi + 0x30]++
         edi = dword [arg_8h]
         
         goto loc_0x102c940f;
    loc_0x102c940f: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c9383(x)
         push (str.iTXt_chunk_not_supported.) // 0x105fb7ec // "iTXt chunk not supported." // (pstr 0x105fb7ec) "iTXt chunk not supported."
         push (edi)
         fcn.102c5282 ()          // fcn.102c5282(0x0, 0x0)
         ecx = pop ()
         ecx = pop ()             // (pstr 0x105fb7ec) "iTXt chunk not supported."

    loc_0x102c941c: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c940d(x)
         eax = dword [arg_14h]
         edx = dword [var_4h]     // "iTXt chunk not supported." str.iTXt_chunk_not_supported.

    loc_0x102c9422: // orphan
         // CODE XREF from fcn.102c928c @ 0x102c9362(x)
         ecx = dword [var_ch]
         ecx++
         edx += 0x10              // 16
         dword [var_ch] = ecx
         dword [var_4h] = edx
         v = ecx - eax
         jl 0x102c9354            // unlikely

         goto loc_0x102c9437;
}

