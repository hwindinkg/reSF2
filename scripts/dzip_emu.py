#!/usr/bin/env python3
"""
Unicorn-based emulator for dzip.exe to extract .dz archives.
"""
import struct, sys, os
from unicorn import *
from unicorn.x86_const import *
import pefile

with open("/home/z/my-project/work/dzip.exe", "rb") as f:
    pe_data = f.read()

pe = pefile.PE(data=pe_data)
image_base = pe.OPTIONAL_HEADER.ImageBase

mu = Uc(UC_ARCH_X86, UC_MODE_32)
mu.mem_map(image_base, 0x100000)
for section in pe.sections:
    va = image_base + section.VirtualAddress
    data = section.get_data()
    full = data + b'\x00' * (section.Misc_VirtualSize - len(data))
    mu.mem_write(va, full[:section.Misc_VirtualSize])

# Stack
STACK_BASE = 0x10000000
STACK_SIZE = 0x100000
mu.mem_map(STACK_BASE, STACK_SIZE)
mu.reg_write(UC_X86_REG_ESP, STACK_BASE + STACK_SIZE - 0x1000)
mu.reg_write(UC_X86_REG_EBP, STACK_BASE + STACK_SIZE - 0x1000)

# Heap
HEAP_BASE = 0x20000000
HEAP_SIZE = 0x4000000  # 64 MB
mu.mem_map(HEAP_BASE, HEAP_SIZE)
heap_ptr = HEAP_BASE

def heap_alloc(size):
    global heap_ptr
    ptr = heap_ptr
    heap_ptr += (size + 0xF) & ~0xF
    if heap_ptr > HEAP_BASE + HEAP_SIZE:
        # Extend
        try:
            mu.mem_map(heap_ptr, 0x1000000)
        except:
            pass
    return ptr

# File I/O
open_files = {}
next_handle = 0x1000

# Critical sections (just empty data)
cs_data = {}
next_cs = 0x5000

# Memory for various allocations
SCRATCH_BASE = 0x60000000
mu.mem_map(SCRATCH_BASE, 0x100000)
scratch_ptr = SCRATCH_BASE

# Read a null-terminated string from memory
def read_cstring(addr):
    s = b""
    while True:
        b = mu.mem_read(addr, 1)
        if b == b'\x00': break
        s += b
        addr += 1
    return s.decode('ascii', errors='replace')

def read_wstring(addr):
    s = b""
    while True:
        b = mu.mem_read(addr, 2)
        if b == b'\x00\x00': break
        s += b
        addr += 2
    return s.decode('utf-16-le', errors='replace')

# Stub implementations - each takes a list of args
def h_CreateFileA(args):
    name = read_cstring(args[0])
    access = args[1]
    print(f"  CreateFileA({name!r}, access=0x{access:x})")
    try:
        if access & 0x40000000:  # GENERIC_WRITE
            os.makedirs(os.path.dirname(name) or '.', exist_ok=True)
            f = open(name, 'wb')
        else:
            f = open(name, 'rb')
        global next_handle
        next_handle += 1
        open_files[next_handle] = f
        return next_handle
    except Exception as e:
        print(f"    FAILED: {e}")
        return 0xFFFFFFFF

def h_CreateFileW(args):
    name = read_wstring(args[0])
    access = args[1]
    print(f"  CreateFileW({name!r}, access=0x{access:x})")
    try:
        if access & 0x40000000:
            os.makedirs(os.path.dirname(name) or '.', exist_ok=True)
            f = open(name, 'wb')
        else:
            f = open(name, 'rb')
        global next_handle
        next_handle += 1
        open_files[next_handle] = f
        return next_handle
    except Exception as e:
        print(f"    FAILED: {e}")
        return 0xFFFFFFFF

def h_ReadFile(args):
    handle, buf, size, bytes_read = args[0], args[1], args[2], args[3]
    if handle not in open_files: return 0
    f = open_files[handle]
    data = f.read(size)
    mu.mem_write(buf, data)
    mu.mem_write(bytes_read, struct.pack("<I", len(data)))
    return 1

def h_WriteFile(args):
    handle, buf, size, written = args[0], args[1], args[2], args[3]
    data = bytes(mu.mem_read(buf, size))
    if handle == 0xDEAD0003:  # stdout
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
        mu.mem_write(written, struct.pack("<I", size))
        return 1
    if handle == 0xDEAD0004:  # stderr
        sys.stderr.buffer.write(data)
        sys.stderr.flush()
        mu.mem_write(written, struct.pack("<I", size))
        return 1
    if handle not in open_files: return 0
    open_files[handle].write(data)
    mu.mem_write(written, struct.pack("<I", size))
    return 1

def h_SetFilePointer(args):
    handle, dist, hi, method = args[0], args[1], args[2], args[3]
    if handle not in open_files: return 0xFFFFFFFF
    f = open_files[handle]
    if method == 0: f.seek(dist)
    elif method == 1: f.seek(dist, 1)
    elif method == 2: f.seek(dist, 2)
    return f.tell() & 0xFFFFFFFF

def h_CloseHandle(args):
    handle = args[0]
    if handle in open_files:
        open_files[handle].close()
        del open_files[handle]
    return 1

def h_GetFileSize(args):
    handle = args[0]
    if handle not in open_files: return 0xFFFFFFFF
    f = open_files[handle]
    pos = f.tell()
    f.seek(0, 2)
    size = f.tell()
    f.seek(pos)
    return size

def h_HeapAlloc(args):
    return heap_alloc(args[2])

def h_HeapReAlloc(args):
    # Just alloc new
    new_ptr = heap_alloc(args[2])
    if args[1] != 0:
        try:
            old_data = mu.mem_read(args[1], 0x1000)
            mu.mem_write(new_ptr, old_data)
        except: pass
    return new_ptr

def h_HeapFree(args): return 1
def h_HeapCreate(args): return 0xDEAD0001
def h_HeapSize(args): return 0x10000
def h_GetProcessHeap(args): return 0xDEAD0001
def h_HeapSetInformation(args): return 1

def h_GetCommandLineA(args):
    return CMDLINE_BASE
def h_GetCommandLineW(args):
    return CMDLINE_BASE_W

def h_GetStdHandle(args):
    if args[0] == 0xFFFFFFF5:  # STD_INPUT_HANDLE
        return 0xDEAD0005
    if args[0] == 0xFFFFFFF6:  # STD_OUTPUT_HANDLE
        return 0xDEAD0003
    if args[0] == 0xFFFFFFF7:  # STD_ERROR_HANDLE
        return 0xDEAD0004
    return 0xDEAD0003

def h_ExitProcess(args):
    print(f"  ExitProcess({args[0]})")
    mu.emu_stop()
    return 0

def h_GetLastError(args): return 0
def h_SetLastError(args): return 0
def h_SetEndOfFile(args): return 1

def h_CreateDirectoryA(args):
    name = read_cstring(args[0])
    print(f"  CreateDirectoryA({name!r})")
    try:
        os.makedirs(name, exist_ok=True)
        return 1
    except: return 0

def h_GetCurrentDirectoryA(args):
    buf_size = args[0]
    buf = args[1]
    cwd = os.getcwd() + '\x00'
    mu.mem_write(buf, cwd.encode('ascii'))
    return len(cwd)

def h_SetCurrentDirectoryA(args):
    name = read_cstring(args[0])
    try:
        os.chdir(name)
        return 1
    except: return 0

def h_GetFileAttributesA(args):
    name = read_cstring(args[0])
    if os.path.exists(name): return 0x80  # FILE_ATTRIBUTE_NORMAL
    return 0xFFFFFFFF

def h_GetModuleFileNameA(args):
    name = b'C:\\dzip.exe\x00'
    mu.mem_write(args[1], name)
    return len(name)

def h_WideCharToMultiByte(args):
    # Just copy bytes
    src = args[3]
    if src == 0: return 0
    size = args[4]
    if size == 0: size = 0x100
    try:
        data = mu.mem_read(src, size*2)
        # Convert UTF-16 to ASCII (simple)
        s = data.decode('utf-16-le', errors='replace')[:args[5]]
        out = s.encode('ascii', errors='replace')
        mu.mem_write(args[6], out)
        return len(out)
    except: return 0

def h_MultiByteToWideChar(args):
    return 0

def h_GetProcAddress(args):
    return 0

def h_TlsAlloc(args): return 0
def h_TlsGetValue(args): return 0
def h_TlsSetValue(args): return 1
def h_TlsFree(args): return 1

def h_InterlockedIncrement(args):
    addr = args[0]
    v = struct.unpack("<I", mu.mem_read(addr, 4))[0] + 1
    mu.mem_write(addr, struct.pack("<I", v))
    return v

def h_InterlockedDecrement(args):
    addr = args[0]
    v = struct.unpack("<I", mu.mem_read(addr, 4))[0] - 1
    mu.mem_write(addr, struct.pack("<I", v))
    return v

def h_InitializeCriticalSectionAndSpinCount(args):
    global next_cs
    mu.mem_write(args[0], struct.pack("<I", next_cs))
    next_cs += 1
    return 1

def h_DeleteCriticalSection(args): return 1
def h_EnterCriticalSection(args): return 0
def h_LeaveCriticalSection(args): return 0

def h_GetModuleHandleW(args): return image_base
def h_DecodePointer(args): return args[0]
def h_EncodePointer(args): return args[0]
def h_RaiseException(args): 
    print(f"  RaiseException(0x{args[0]:x})")
    mu.emu_stop()
    return 0
def h_RtlUnwind(args): return 0

def h_IsProcessorFeaturePresent(args): return 0
def h_IsDebuggerPresent(args): return 0
def h_GetCurrentProcessId(args): return 1234
def h_GetCurrentThreadId(args): return 5678
def h_GetTickCount(args): return 1000
def h_QueryPerformanceCounter(args):
    mu.mem_write(args[0], struct.pack("<Q", 1000))
    return 1
def h_GetSystemTimeAsFileTime(args):
    mu.mem_write(args[0], struct.pack("<Q", 0))
    return 0
def h_TerminateProcess(args):
    mu.emu_stop()
    return 0
def h_GetCurrentProcess(args): return 0xDEAD0006
def h_UnhandledExceptionFilter(args): return 1
def h_SetUnhandledExceptionFilter(args): return 0

def h_GetCPInfo(args): return 1
def h_GetACP(args): return 1252
def h_GetOEMCP(args): return 437
def h_IsValidCodePage(args): return 1
def h_SetHandleCount(args): return args[0]
def h_GetFileType(args): return 2  # FILE_TYPE_CHAR
def h_GetStartupInfoW(args):
    # Write a minimal STARTUPINFOW (size 0x44)
    mu.mem_write(args[0], b'\x00' * 0x44)
    mu.mem_write(args[0], struct.pack("<I", 0x44))
    return 0
def h_Sleep(args): return 0
def h_GetConsoleCP(args): return 437
def h_GetConsoleMode(args): return 1
def h_FlushFileBuffers(args): return 1
def h_LoadLibraryW(args): return image_base
def h_LCMapStringW(args): return 0
def h_FreeEnvironmentStringsW(args): return 1
def h_GetEnvironmentStringsW(args):
    # Empty env block: just \x00\x00
    global scratch_ptr
    ptr = scratch_ptr
    mu.mem_write(ptr, b'\x00\x00')
    scratch_ptr += 16
    return ptr
def h_GetStringTypeW(args): return 0
def h_CompareStringW(args): return 2
def h_SetStdHandle(args): return 1
def h_WriteConsoleW(args):
    data = mu.mem_read(args[1], args[2])
    try:
        sys.stdout.buffer.write(data.decode('utf-16-le').encode('utf-8'))
        sys.stdout.flush()
    except: pass
    mu.mem_write(args[3], struct.pack("<I", args[2]))
    return 1
def h_SetEnvironmentVariableA(args): return 1
def h_DeleteFileA(args):
    name = read_cstring(args[0])
    try:
        os.unlink(name)
        return 1
    except: return 0

# Map all imports
hooks = {
    'CreateFileA': h_CreateFileA,
    'CreateFileW': h_CreateFileW,
    'ReadFile': h_ReadFile,
    'WriteFile': h_WriteFile,
    'SetFilePointer': h_SetFilePointer,
    'CloseHandle': h_CloseHandle,
    'GetFileSize': h_GetFileSize,
    'HeapAlloc': h_HeapAlloc,
    'HeapReAlloc': h_HeapReAlloc,
    'HeapFree': h_HeapFree,
    'HeapCreate': h_HeapCreate,
    'HeapSize': h_HeapSize,
    'GetProcessHeap': h_GetProcessHeap,
    'HeapSetInformation': h_HeapSetInformation,
    'GetCommandLineA': h_GetCommandLineA,
    'GetCommandLineW': h_GetCommandLineW,
    'GetStdHandle': h_GetStdHandle,
    'ExitProcess': h_ExitProcess,
    'GetLastError': h_GetLastError,
    'SetLastError': h_SetLastError,
    'SetEndOfFile': h_SetEndOfFile,
    'CreateDirectoryA': h_CreateDirectoryA,
    'GetCurrentDirectoryA': h_GetCurrentDirectoryA,
    'SetCurrentDirectoryA': h_SetCurrentDirectoryA,
    'GetFileAttributesA': h_GetFileAttributesA,
    'GetModuleFileNameA': h_GetModuleFileNameA,
    'WideCharToMultiByte': h_WideCharToMultiByte,
    'MultiByteToWideChar': h_MultiByteToWideChar,
    'GetProcAddress': h_GetProcAddress,
    'TlsAlloc': h_TlsAlloc,
    'TlsGetValue': h_TlsGetValue,
    'TlsSetValue': h_TlsSetValue,
    'TlsFree': h_TlsFree,
    'InterlockedIncrement': h_InterlockedIncrement,
    'InterlockedDecrement': h_InterlockedDecrement,
    'InitializeCriticalSectionAndSpinCount': h_InitializeCriticalSectionAndSpinCount,
    'DeleteCriticalSection': h_DeleteCriticalSection,
    'EnterCriticalSection': h_EnterCriticalSection,
    'LeaveCriticalSection': h_LeaveCriticalSection,
    'GetModuleHandleW': h_GetModuleHandleW,
    'DecodePointer': h_DecodePointer,
    'EncodePointer': h_EncodePointer,
    'RaiseException': h_RaiseException,
    'RtlUnwind': h_RtlUnwind,
    'IsProcessorFeaturePresent': h_IsProcessorFeaturePresent,
    'IsDebuggerPresent': h_IsDebuggerPresent,
    'GetCurrentProcessId': h_GetCurrentProcessId,
    'GetCurrentThreadId': h_GetCurrentThreadId,
    'GetTickCount': h_GetTickCount,
    'QueryPerformanceCounter': h_QueryPerformanceCounter,
    'GetSystemTimeAsFileTime': h_GetSystemTimeAsFileTime,
    'TerminateProcess': h_TerminateProcess,
    'GetCurrentProcess': h_GetCurrentProcess,
    'UnhandledExceptionFilter': h_UnhandledExceptionFilter,
    'SetUnhandledExceptionFilter': h_SetUnhandledExceptionFilter,
    'GetCPInfo': h_GetCPInfo,
    'GetACP': h_GetACP,
    'GetOEMCP': h_GetOEMCP,
    'IsValidCodePage': h_IsValidCodePage,
    'SetHandleCount': h_SetHandleCount,
    'GetFileType': h_GetFileType,
    'GetStartupInfoW': h_GetStartupInfoW,
    'Sleep': h_Sleep,
    'GetConsoleCP': h_GetConsoleCP,
    'GetConsoleMode': h_GetConsoleMode,
    'FlushFileBuffers': h_FlushFileBuffers,
    'LoadLibraryW': h_LoadLibraryW,
    'LCMapStringW': h_LCMapStringW,
    'FreeEnvironmentStringsW': h_FreeEnvironmentStringsW,
    'GetEnvironmentStringsW': h_GetEnvironmentStringsW,
    'GetStringTypeW': h_GetStringTypeW,
    'CompareStringW': h_CompareStringW,
    'SetStdHandle': h_SetStdHandle,
    'WriteConsoleW': h_WriteConsoleW,
    'SetEnvironmentVariableA': h_SetEnvironmentVariableA,
    'DeleteFileA': h_DeleteFileA,
}

# Build IAT stubs
STUB_BASE = 0x77000000
mu.mem_map(STUB_BASE, 0x10000)
stub_ptr = STUB_BASE
stub_to_name = {}

imports_by_name = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        if imp.name:
            imports_by_name[imp.name.decode()] = imp.address

for name, iat_addr in imports_by_name.items():
    # Determine stack cleanup: stdcall cleans up args
    # We'll just write `int 0x30; ret` and handle cleanup manually based on name
    if name in ('HeapFree', 'CloseHandle', 'DeleteFileA', 'DeleteCriticalSection',
                'EnterCriticalSection', 'LeaveCriticalSection', 'RtlUnwind',
                'Sleep', 'FlushFileBuffers', 'FreeEnvironmentStringsW',
                'SetEnvironmentVariableA', 'SetEndOfFile', 'SetLastError',
                'TlsFree', 'TlsSetValue', 'ExitProcess', 'SetStdHandle',
                'SetUnhandledExceptionFilter', 'SetCurrentDirectoryA'):
        # 1 arg
        stub_code = b'\xcd\x30\xc2\x04\x00'  # int 0x30; ret 4
    elif name in ('HeapAlloc', 'HeapReAlloc', 'HeapSize', 'HeapSetInformation',
                  'GetStdHandle', 'GetModuleHandleW', 'DecodePointer', 'EncodePointer',
                  'GetProcAddress', 'TlsAlloc', 'TlsGetValue', 'IsProcessorFeaturePresent',
                  'IsDebuggerPresent', 'GetCurrentProcessId', 'GetCurrentThreadId',
                  'GetTickCount', 'GetCurrentProcess', 'GetConsoleCP', 'GetConsoleMode',
                  'GetACP', 'GetOEMCP', 'IsValidCodePage', 'SetHandleCount', 'GetFileType',
                  'LoadLibraryW', 'CreateDirectoryA', 'GetFileAttributesA',
                  'GetCurrentDirectoryA', 'GetModuleFileNameA', 'GetProcessHeap',
                  'HeapCreate', 'GetCommandLineA', 'GetCommandLineW', 'GetLastError',
                  'InterlockedIncrement', 'InterlockedDecrement',
                  'InitializeCriticalSectionAndSpinCount', 'UnimplementedExceptionFilter',
                  'UnhandledExceptionFilter', 'DeleteFileA'):
        stub_code = b'\xcd\x30\xc2\x04\x00'  # 1 arg
    elif name in ('ReadFile', 'WriteFile', 'SetFilePointer', 'GetFileSize',
                  'CreateFileA', 'CreateFileW', 'WideCharToMultiByte',
                  'MultiByteToWideChar', 'GetStartupInfoW', 'LCMapStringW',
                  'GetStringTypeW', 'CompareStringW', 'InitializeCriticalSectionAndSpinCount'):
        # Multiple args - hard to know exact count without docs
        # Use ret 0x10 (4 args) as default, but some take more
        if name in ('CreateFileA', 'CreateFileW'):
            stub_code = b'\xcd\x30\xc2\x1c\x00'  # 7 args = 28 bytes
        elif name in ('ReadFile', 'WriteFile'):
            stub_code = b'\xcd\x30\xc2\x14\x00'  # 5 args
        elif name == 'SetFilePointer':
            stub_code = b'\xcd\x30\xc2\x10\x00'  # 4 args
        elif name == 'GetFileSize':
            stub_code = b'\xcd\x30\xc2\x08\x00'  # 2 args
        elif name == 'WideCharToMultiByte':
            stub_code = b'\xcd\x30\xc2\x20\x00'  # 8 args
        elif name == 'MultiByteToWideChar':
            stub_code = b'\xcd\x30\xc2\x18\x00'  # 6 args
        elif name == 'GetStartupInfoW':
            stub_code = b'\xcd\x30\xc2\x04\x00'  # 1 arg
        elif name == 'LCMapStringW':
            stub_code = b'\xcd\x30\xc2\x18\x00'  # 6 args
        elif name == 'GetStringTypeW':
            stub_code = b'\xcd\x30\xc2\x10\x00'  # 4 args
        elif name == 'CompareStringW':
            stub_code = b'\xcd\x30\xc2\x18\x00'  # 6 args
        elif name == 'InitializeCriticalSectionAndSpinCount':
            stub_code = b'\xcd\x30\xc2\x08\x00'  # 2 args
        else:
            stub_code = b'\xcd\x30\xc2\x10\x00'  # default 4 args
    else:
        stub_code = b'\xcd\x30\xc2\x04\x00'  # default 1 arg
    mu.mem_write(stub_ptr, stub_code)
    mu.mem_write(iat_addr, struct.pack("<I", stub_ptr))
    stub_to_name[stub_ptr] = name
    stub_ptr += 16

# Command line
CMDLINE_BASE = 0x30000000
mu.mem_map(CMDLINE_BASE, 0x1000)
# Build: "dzip.exe -d files.dz"
import shlex
cmd_args = ['dzip.exe', '-d', 'files.dz']
cmdline_str = ' '.join(cmd_args) + '\x00'
mu.mem_write(CMDLINE_BASE, cmdline_str.encode('ascii'))

# Wide version
CMDLINE_BASE_W = CMDLINE_BASE + 0x200
mu.mem_write(CMDLINE_BASE_W, cmdline_str.encode('utf-16-le'))

# Hook int 0x30
def hook_intr(uc, intno, user_data):
    if intno != 0x30: return
    eip = uc.reg_read(UC_X86_REG_EIP)
    stub_addr = eip - 2
    name = stub_to_name.get(stub_addr)
    if not name: return
    esp = uc.reg_read(UC_X86_REG_ESP)
    args_addr = esp + 4  # skip return address
    args = []
    for i in range(8):
        try:
            arg = struct.unpack("<I", uc.mem_read(args_addr + i*4, 4))[0]
            args.append(arg)
        except: args.append(0)
    
    if name in hooks:
        try:
            result = hooks[name](args)
            uc.reg_write(UC_X86_REG_EAX, result & 0xFFFFFFFF if isinstance(result, int) else 0)
        except Exception as e:
            print(f"  ERROR in {name}: {e}")
            uc.reg_write(UC_X86_REG_EAX, 0)
    else:
        print(f"  Unimplemented: {name}")
        uc.reg_write(UC_X86_REG_EAX, 0)

mu.hook_add(UC_HOOK_INTR, hook_intr)

# Set up TIB
TIB_BASE = 0x7FF70000
mu.mem_map(TIB_BASE, 0x1000)
mu.mem_write(TIB_BASE, struct.pack("<I", TIB_BASE))  # self
PEB_BASE = 0x7FFD0000
mu.mem_map(PEB_BASE, 0x1000)
mu.mem_write(TIB_BASE + 0x30, struct.pack("<I", PEB_BASE))
mu.reg_write(UC_X86_REG_MSR, (0xC0000100, TIB_BASE))

# Run
entry_va = image_base + pe.OPTIONAL_HEADER.AddressOfEntryPoint
EXIT_ADDR = 0xDEADBEEF
mu.mem_map(0xDEAD0000, 0x10000)
mu.mem_write(0xDEADBEEF, b'\xcc')

esp = mu.reg_read(UC_X86_REG_ESP)
esp -= 4
mu.mem_write(esp, struct.pack("<I", EXIT_ADDR))
mu.reg_write(UC_X86_REG_ESP, esp)

print(f"\n=== Starting emulation from 0x{entry_va:x} ===")
try:
    mu.emu_start(entry_va, EXIT_ADDR, timeout=120*1000000)
    print("\n=== Emulation completed ===")
except UcError as e:
    print(f"\n=== Emulation error: {e} ===")
    eip = mu.reg_read(UC_X86_REG_EIP)
    print(f"  EIP: 0x{eip:x}, ESP: 0x{mu.reg_read(UC_X86_REG_ESP):x}")
