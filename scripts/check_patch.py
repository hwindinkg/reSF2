import struct
from unicorn import *
from unicorn.arm_const import *

with open('E:/reSF2/reverse/binaries/libs3e_android.so', 'rb') as f:
    so = f.read()

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)

# Load segments
for i in range(6):
    p = struct.unpack_from('<IIIIIIII', so, 0x34 + i * 32)
    if p[0] == 1:
        va, off, fs, ms = p[2], p[1], p[4], p[5]
        ps = va & ~0xFFF
        pe = ((va + ms - 1) & ~0xFFF) + 0x1000
        try: mu.mem_map(ps, pe - ps, UC_PROT_ALL)
        except: pass
        mu.mem_write(va, so[off:off + fs])
        if ms > fs:
            mu.mem_write(va + fs, b'\x00' * (ms - fs))

# Verify instruction at 0x51838
print(f'Before patch @0x51838: {bytes(mu.mem_read(0x51838, 4)).hex()}')
print(f'File at offset 0x51838: {so[0x51838:0x5183c].hex()}')

# Check context around 0x51838
print(f'Context 0x51830-0x51850:')
for a in range(0x51830, 0x51850, 4):
    print(f'  0x{a:05x}: {bytes(mu.mem_read(a, 4)).hex()}')

# Apply patch
mu.mem_write(0x51838, b'\x00\x00\x52\xe1')
print(f'After patch @0x51838: {bytes(mu.mem_read(0x51838, 4)).hex()}')

# Now let's run the actual init function and see what happens
# Set up minimal state
STACK = 0x90000000
mu.mem_map(STACK, 0x100000, UC_PROT_ALL)

GOT_BASE = 0xC1ECC
IN_BUF = 0x70000000
mu.mem_map(IN_BUF & ~0xFFF, 0x10000, UC_PROT_ALL)

# Write some test data
test_data = bytes(range(256))
mu.mem_write(IN_BUF, test_data)

# Set up GOT entries for callbacks (memset etc.)
# ... skip for now

print("\nJust checking if 0x51838 is the right address...")
print(f"cmp r2,#0x900000 encodes to: e3520609")
print(f"Bytes in file: {so[0x51838:0x5183c].hex()}")
print(f"So 0x51838 = {so[0x51838]:02x} {so[0x51839]:02x} {so[0x5183a]:02x} {so[0x5183b]:02x}")
