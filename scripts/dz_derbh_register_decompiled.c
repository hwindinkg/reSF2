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
void fcn.102ca2cf (int32_t arg1, int32_t arg_8h) {
        // CALL XREF from fcn.100a8870 @ 0x100a889c(x)
        // CALL XREF from fcn.100d5eb0 @ 0x100d61ce(x)
        // CALL XREF from fcn.100d8680 @ 0x100d8e67(x)
        // CALL XREF from fcn.100f4520 @ 0x100f459e(x)
        // CALL XREF from fcn.102ca260 @ 0x102ca2b8(x)
        push (4)      // 4
        eax = 0x10330c8b
        fcn.10300831 () // fcn.10300831(0x10330c8b, 0x0, 0x0)
        v = dword [0x10667318] - 0x20 // [0x10667318:4]=0
        jl 0x102ca2ec // likely
        goto loc_0x102ca2e4;
    loc_0x102ca2ec:
        // CODE XREF from fcn.102ca2cf @ 0x102ca2e2(x)
        v = byte [0x10667291] - 0 // [0x10667291:1]=0
        if (v) goto loc_0x102ca2fa // unlikely
        goto loc_0x102ca2f5;
    loc_0x102ca2fa:
        // CODE XREF from fcn.102ca2cf @ 0x102ca2f3(x)
        push (0x47c)  // 1148
        fcn.102c96db ()
        ecx = pop ()
        dword [var_10h] = eax
        ebx = 0
        dword [var_4h] = ebx
        v = eax & eax
        if (!v) goto loc_0x102ca31c // likely
        goto loc_0x102ca311;
    loc_0x102ca31c:
        // CODE XREF from fcn.102ca2cf @ 0x102ca30f(x)
        ecx = ebx
        
    loc_0x102ca31e:
        // CODE XREF from fcn.102ca2cf @ 0x102ca31a(x)
        eax = dword [0x10667318] // [0x10667318:4]=0 // int32_t arg1
        dword [var_4h] |= 0xffffffff // [0xffffffff:4]=-1 // -1
        push (ebx)
        push (ebx)
        push (dword [arg_8h])
        dword [eax*4 + 0x10667298] = ecx // [0x10667298:4]=0
        fcn.102c9bfc () // fcn.102c9bfc(0x0, 0x0, 0x0, 0x0, 0x0)
        edi = eax
        v = edi & edi
        if (!v) goto loc_0x102ca36c // likely
        goto loc_0x102ca33e;
    loc_0x102ca2f5: // orphan
         fcn.102c9af0 ()

    loc_0x102ca311: // orphan
         ecx = eax
         fcn.102c958d ()          // fcn.102c958d(0x0)
         ecx = eax
         
         goto loc_0x102ca31c;
    loc_0x102ca33e: // orphan
         ecx = dword [0x10667318] // [0x10667318:4]=0
         esi = dword [ecx*4 + 0x10667298]
         v = esi & esi
         if (!v) 
         goto loc_0x102ca34f;
    loc_0x102ca34f: // orphan
         ecx = esi
         fcn.102c9618 ()          // fcn.102c9618(0x0)
         push (esi)
         fcn.102c96f0 ()
         ecx = pop ()
         ecx = dword [0x10667318] // [0x10667318:4]=0

    loc_0x102ca363: // orphan
         // CODE XREF from fcn.102ca2cf @ 0x102ca34d(x)
         dword [ecx*4 + 0x10667298] = ebx // [0x10667298:4]=0
         
         goto loc_0x102ca36c;
    loc_0x102ca36c: // orphan
         // CODE XREF from fcn.102ca2cf @ 0x102ca33c(x)
         dword [0x10667318]++     // [0x10667318:4]=0

    loc_0x102ca372: // orphan
         // CODE XREF from fcn.102ca2cf @ 0x102ca36a(x)
         eax = edi

    loc_0x102ca374: // orphan
         // CODE XREF from fcn.102ca2cf @ 0x102ca2e7(x)
         fcn.1030080e ()
         return                   // ebp

}

