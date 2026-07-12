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
void fcn.1015da00 (int32_t arg1, int32_t arg2, int32_t arg_8h, int32_t arg_ch) {
        // CALL XREFS from fcn.1015efd0 @ 0x1015f4bc(x), 0x1015f4cf(x)
        // CALL XREF from fcn.10162ba0 @ 0x10162be4(x)
        push (ebp)
        ebp = esp
        push (ebx)    // arg2
        ebx = arg_ch  // arg3
        eax = 0x2aaaaaab
        edx = dword [ebx + 4]
        edx -= dword [ebx]
        push (esi)
        eax = eax * edx
        edx >>= 1
        eax = edx
        eax >>>= 0x1f
        push (edi)
        edi = dword [arg_8h]
        eax += edx
        v = edi - eax
        jae 0x1015dab4 // likely
        goto loc_0x1015da28;
    loc_0x1015dab4:
        // CODE XREF from fcn.1015da00 @ 0x1015da22(x)
        push (edi)
        push (str.big_frame__d) // 0x105b1ab8 // "big frame %d" // (pstr 0x105b1ab8) "big frame %d"
        fcn.101472f0 () // fcn.101472f0(0x0, 0x0)
        fldz
        esp += 8
        edi = pop ()  // ebp
        esi = pop ()
        ebx = pop ()
        ebp = pop ()
        return
        goto loc_0x1015da37;
        return eax;
    loc_0x1015da37:
        ecx = dword [ebx + 4]
        ecx -= dword [ebx]
        eax = 0x2aaaaaab
        eax = eax * ecx
        edx >>= 1
        eax = edx
        eax >>>= 0x1f
        eax += edx
        v = edi - eax
        if (((unsigned) v) < 0) goto 0x1015da5d // unlikely
        goto loc_0x1015da50;
    loc_0x1015da5d:
        // CODE XREF from fcn.1015da00 @ 0x1015da4e(x)
        ebx = dword [ebx]
        edi = edi + edi*2
        ecx = dword [ebx + edi*4 + 4]
        eax = dword [ebx + edi*4 + 8]
        eax -= ecx
        eax >>= 2
        v = esi - eax
        jae 0x1015da80 // likely
        return eax;
    loc_0x1015da80:
        // CODE XREF from fcn.1015da00 @ 0x1015da71(x)
        push (esi)
        push (str.Subcontainer_index_error__i) // 0x105aca0c // "Subcontainer index error %i" // (pstr 0x105aca0c) "Subcontainer index error %i"
        fcn.101471b0 () // fcn.101471b0(0x0, 0x0)
        eax = dword [ebx + edi*4 + 4]
        esp += 8
        fld dword [eax]
        edi = pop ()  // ebp
        esi = pop ()
        ebx = pop ()
        ebp = pop ()
        return
        return eax;
    loc_0x1015da50: // orphan
         push (0x103744c0)        // (pstr 0x103744c0) "vector"
         fcn.102e4020 ()          // fcn.102e4020(0x0)
         esp += 4                 // (pstr 0x103744c0) "vector"

    loc_0x1015da73: // orphan
         fld dword [ecx + esi*4]
         edi = pop ()
         eax = ecx + esi*4
         esi = pop ()
         ebx = pop ()
         ebp = pop ()
         return

    loc_0x1015da9b: // orphan
         // CODE XREF from fcn.1015da00 @ 0x1015da35(x)
         push (dword [arg_ch])
         push (str.node__s_not_found) // 0x105b1aa4 // "node %s not found" // (pstr 0x105b1aa4) "node %s not found"
         fcn.101471b0 ()          // fcn.101471b0(0x0, 0x0)
         fldz
         esp += 8
         edi = pop ()             // ebp
         esi = pop ()
         ebx = pop ()
         ebp = pop ()
         return

}

