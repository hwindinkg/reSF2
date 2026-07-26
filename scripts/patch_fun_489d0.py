import struct
with open('E:/reSF2/reverse/binaries/libs3e_android.so','rb') as f:
    so = bytearray(f.read())

# Patch FUN_000489d0: str r3, [r0, #0x50] at 0x389E8  
off1 = 0x389E8
val1 = struct.unpack_from('<I', so, off1)[0]
print('0x%05X: 0x%08X' % (off1, val1))
# Expect: str r3, [r0, #0x50] = 0xE5803050
if val1 == 0xE5803050:
    struct.pack_into('<I', so, off1, 0xE1A00000)  # NOP
    print('Patched!')
else:
    print('UNEXPECTED!')

# Save
with open('E:/reSF2/scripts/libs3e_android_patched.so', 'wb') as f:
    f.write(so)
print('Saved to libs3e_android_patched.so')
