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
void method.Model.virtual_12 (int32_t arg1, int32_t arg2, int32_t arg_8h) {
        push (ebp)
        ebp = esp
        push (esi)
        push (edi)
        edi = dword [arg_8h]
        esi = ecx     // arg3
        dword [esi + 0x588] = edi // [0x588:4]=-1 // 1416
        v = edi & edi
        if (!v) goto loc_0x10158b2c // likely
        goto loc_0x10158b24;
    loc_0x10158b2c:
        // CODE XREF from method.Model.virtual_12 @ 0x10158b22(x)
        eax = 0
        
    loc_0x10158b2e:
        // CODE XREF from method.Model.virtual_12 @ 0x10158b2a(x)
        push (0x10374b40) // '@K7\x10'
        push (0x10374b40) // '@K7\x10'
        ecx = esi + 0x1f4
        dword [esi + 0x190] = eax
        dword [esi + 0x58c] = 0 // [0x58c:4]=-1
        dword [esi + 0x590] = 0 // [0x590:4]=-1
        dword [esi + 0x598] = 0 // [0x598:4]=-1
        dword [esi + 0x594] = 0 // [0x594:4]=-1
        dword [esi + 0x59c] = 0 // [0x59c:4]=-1
        word [esi + 0x1e5] = 0
        dword [esi + 0x1ec] = 0
        dword [esi + 0x1f0] = 0
        fcn.100065e0 () // fcn.100065e0(0x0, 0x0, 0x1f4, 0x0, 0x0)
        dword [esi + 0x200] = 0
        dword [esi + 0x204] = 0
        dword [esi + 0x5bc] = 0xffffffff // [0x5bc:4]=-1 // 1468
        word [esi + 0x1b8] = 0
        dword [esi + 0x5d0] = 0 // [0x5d0:4]=-1
        dword [esi + 0x1e0] = 0
        dword [esi + 0x584] = 0 // [0x584:4]=-1
        byte [esi + 0x1a8] = 0
        byte [esi + 0x1c0] = 1
        byte [esi + 0x1dc] = 0
        byte [esi + 0x1e4] = 1
        ecx = esi
        dword [esi + 0x658] = 0 // [0x658:4]=-1
        dword [esi + 0x65c] = 0 // [0x65c:4]=-1
        dword [esi + 0x660] = 0 // [0x660:4]=-1
        byte [esi + 0x664] = 0 // [0x664:1]=255
        dword [esi + 0x668] = 0xffffffff // [0x668:4]=-1 // 1640
        fcn.10159b80 () // fcn.10159b80(0x0, 0x0)
        dword [esi + 0x5c8] = 0 // [0x5c8:4]=-1
        dword [esi + 0x580] = 1 // [0x580:4]=-1
        dword [esi + 0x194] = 1
        v = edi & edi
        if (!v) goto loc_0x10158c5d // likely
        goto loc_0x10158c51;
    loc_0x10158c51: // orphan
         eax = dword [edi + 0x1bc]
         dword [esi + 0x1bc] = eax

    loc_0x10158c5d: // orphan
         // CODE XREF from method.Model.virtual_12 @ 0x10158c4f(x)
         edi = pop ()
         byte [esi + 0x5d4] = 0   // [0x5d4:1]=255
         dword [esi + 0x5cc] = 0  // [0x5cc:4]=-1
         esi = pop ()
         ebp = pop ()
         return

}

