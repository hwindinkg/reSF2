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
void fcn.102c9fbf (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch, signed int arg_10h, int32_t arg_14h) {
        // CALL XREF from fcn.102ca3bc @ +0x153(x)
        push (ebp)
        ebp = esp
        esp -= 0x10
        push (ebx)    // arg2
        push (edi)
        edi = dword [arg_ch]
        edi = edi * dword [arg_10h]
        ebx = edi
        dword [ebp - 4] = arg_ch // arg3
        dword [var_8h] = edi
        v = ebx & ebx
        if (v) goto loc_0x102c9fe1 // unlikely
        goto loc_0x102c9fda;
    loc_0x102c9fe1:
        // CODE XREF from fcn.102c9fbf @ 0x102c9fd8(x)
        push (esi)
        esi = dword [arg_14h]
        eax = dword [esi]
        v = eax & eax
        jns 0x102c9ff2 // unlikely
        goto loc_0x102c9feb;
    loc_0x102c9ff2:
        // CODE XREF from fcn.102c9fbf @ 0x102c9fe9(x)
        edx = dword [esi + 0xc]
        ebx = 0xffff
        ecx = word [edx]
        dword [var_10h] = ebx
        v = cx - bx
        dword [arg_14h] = edx
        ebx = edi
        if (!v) goto loc_0x102ca040 // likely
        goto loc_0x102ca00a;
    loc_0x102ca040:
        // CODE XREFS from fcn.102c9fbf @ 0x102ca008(x), 0x102ca020(x)
        ecx = dword [esi + 4]
        v = eax - ecx
        if (v >= 0) goto loc_0x102ca06c // likely
        goto loc_0x102ca047;
    loc_0x102ca06c:
        // CODE XREF from fcn.102c9fbf @ 0x102ca045(x)
        v = dword [esi + 0x10] - 0
        if (!v) goto loc_0x102ca094 // unlikely
        goto loc_0x102ca072;
    loc_0x102ca094:
        // CODE XREF from fcn.102c9fbf @ 0x102ca070(x)
        eax = dword [var_4h]
        
    loc_0x102ca097:
        // CODE XREFS from fcn.102c9fbf @ 0x102ca078(x), 0x102ca07d(x), 0x102ca087(x)
        ecx = eax + 4
        eax = word [edx]
        push (eax)
        dword [var_10h] = ecx
        fcn.102ca8a7 () // fcn.102ca8a7(0xffff, 0x0, 0x4)
        v = eax & eax
        if (v) goto loc_0x102ca18a // likely
        goto loc_0x102ca0ae;
        goto loc_0x102ca07a;
        goto loc_0x102ca022;
    loc_0x102c9feb: // orphan
         eax = 0
         
         goto loc_0x102c9ff2;
    loc_0x102ca00a: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca03e(x)
         eax = dword [var_4h]
         ecx = cx
         eax = dword [eax + 0xc]
         ecx += ecx
         ecx = dword [eax + ecx*8 + 8]
         ecx += dword [esi + 4]
         eax = dword [esi]
         v = eax - ecx
         jl 0x102ca040            // unlikely

         goto loc_0x102ca022;
    loc_0x102ca022: // orphan
         edx += 2
         edi = 0xffff
         v = word [edx] - di
         dword [arg_14h] = edx
         edi = ebx
         if (!v) 
         goto loc_0x102ca034;
    loc_0x102ca034: // orphan
         dword [esi + 4] = ecx
         ecx = word [edx]
         v = cx - word [var_10h]
         if (v) 
         goto loc_0x102ca040;
    loc_0x102ca047: // orphan
         edi = dword [var_4h]
         ebx = ecx

    loc_0x102ca04c: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca062(x)
         eax = dword [edi + 0xc]
         edx -= 2
         ecx = word [edx]
         ecx += ecx
         ebx -= dword [eax + ecx*8 + 8]
         eax = dword [esi]
         dword [esi + 4] = ebx
         v = eax - ebx
         jl 0x102ca04c            // likely

         goto loc_0x102ca064;
    loc_0x102ca064: // orphan
         edi = dword [var_8h]
         dword [arg_14h] = edx
         ebx = edi

    loc_0x102ca072: // orphan
         v = eax - dword [esi + 0x14]
         eax = dword [var_4h]
         jl 0x102ca097            // unlikely

         goto loc_0x102ca07a;
    loc_0x102ca07a: // orphan
         v = edx - dword [esi + 0xc]
         if (v) 
         goto loc_0x102ca07f;
    loc_0x102ca07f: // orphan
         v = dword [eax + 0x90] - esi
         if (!v) 
         goto loc_0x102ca087;
    loc_0x102ca087: // orphan
         
         goto loc_0x102ca089;
    loc_0x102ca089: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca032(x)
         byte [esi + 0x1c] = 1
         eax = 0
         
         goto loc_0x102ca094;
    loc_0x102ca0ae: // orphan
         ecx = dword [var_10h]
         eax = esi + 0x18
         push (eax)
         eax = esi + 0x10
         push (eax)
         fcn.102ca5c9 ()          // fcn.102ca5c9(0x10, 0x0, 0x4)
         v = eax & eax
         if (v) 
         goto loc_0x102ca0c6;
    loc_0x102ca0c6: // orphan
         eax = dword [var_4h]
         dword [eax + 0x90] = esi
         eax = dword [esi + 4]
         dword [esi + 0x14] = eax
         eax = dword [arg_14h]
         dword [esi + 0xc] = eax

    loc_0x102ca0db: // orphan
         // CODE XREFS from fcn.102c9fbf @ 0x102ca085(x), 0x102ca184(x)
         ecx = esi + 0x18

    loc_0x102ca0de: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca13b(x)
         edx = dword [esi]
         edx -= dword [esi + 0x14]
         eax = dword [ecx]
         eax -= edx
         v = eax & eax
         if (v <= 0) 
         goto loc_0x102ca0eb;
    loc_0x102ca0eb: // orphan
         v = eax - ebx
         cmovg eax ebx
         push (eax)
         dword [arg_14h] = eax
         eax = dword [esi + 0x10]
         eax += edx
         push (eax)
         push (dword [arg_8h])
         fcn.102f7830 ()
         eax = dword [arg_14h]
         dword [arg_8h] += eax
         dword [esi] += eax
         esp += 0xc
         ebx -= eax
         if (!v) 
         goto loc_0x102ca115;
    loc_0x102ca115: // orphan
         ecx = esi + 0x18

    loc_0x102ca118: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca0e9(x)
         eax = dword [ecx]
         dword [esi + 0x14] += eax
         edx = dword [var_4h]
         edx += 4
         push (ecx)
         eax = esi + 0x10
         push (eax)
         ecx = edx
         dword [arg_14h] = edx
         fcn.102ca5c9 ()          // fcn.102ca5c9(0x10, 0x0, 0x4)
         v = eax & eax
         if (v) 
         goto loc_0x102ca136;
    loc_0x102ca136: // orphan
         ecx = esi + 0x18
         v = dword [ecx] - eax
         if (v) 
         goto loc_0x102ca13d;
    loc_0x102ca13d: // orphan
         eax = dword [esi + 0xc]
         ecx = 0xffff
         edx = eax + 2
         v = word [edx] - cx
         if (!v) 
         goto loc_0x102ca14d;
    loc_0x102ca14d: // orphan
         ecx = word [eax]
         eax = dword [var_4h]
         dword [esi + 0xc] = edx
         eax = dword [eax + 0xc]
         ecx += ecx
         eax = dword [eax + ecx*8 + 8]
         dword [esi + 4] += eax
         eax = word [edx]
         ecx = dword [arg_14h]
         push (eax)
         fcn.102ca8a7 ()          // fcn.102ca8a7(0xffff, 0x0, 0x4)
         v = eax & eax
         if (v) 
         goto loc_0x102ca172;
    loc_0x102ca172: // orphan
         ecx = dword [arg_14h]
         eax = esi + 0x18
         push (eax)
         eax = esi + 0x10
         push (eax)
         fcn.102ca5c9 ()          // fcn.102ca5c9(0x10, 0x0, 0x4)
         v = eax & eax
         if (!v) 
         goto loc_0x102ca18a;
    loc_0x102ca18a: // orphan
         // CODE XREFS from fcn.102c9fbf @ 0x102ca0a8(x), 0x102ca0c0(x), 0x102ca134(x), 0x102ca170(x)
         edi -= ebx
         eax = edi
         edx = 0
         dword [arg_ch] /=

    loc_0x102ca193: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102c9fed(x)
         byte [esi + 0x1c] = 2

    loc_0x102ca197: // orphan
         // CODE XREFS from fcn.102c9fbf @ 0x102ca08f(x), 0x102ca1a1(x), 0x102ca1b0(x)
         esi = pop ()

    loc_0x102ca198: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102c9fdc(x)
         edi = pop ()
         ebx = pop ()
         leave                    // ebp
         return

    loc_0x102ca19e: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca10f(x)
         eax = dword [arg_10h]
         
         goto loc_0x102ca1a3;
    loc_0x102ca1a3: // orphan
         // CODE XREF from fcn.102c9fbf @ 0x102ca14b(x)
         edi -= ebx
         eax = edi
         edx = 0
         dword [arg_ch] /=
         byte [esi + 0x1c] = 1
         
}

