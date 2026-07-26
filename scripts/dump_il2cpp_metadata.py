#!/usr/bin/env python3
"""Analyze IL2CPP global-metadata.dat for Shadow Fight 2 Unity version"""
import struct, sys

metadata_path = r'E:\reSF2\sf2_unity_native\global-metadata.dat'
with open(metadata_path, 'rb') as f:
    data = f.read()

print(f'File size: {len(data)} bytes ({len(data)/1024/1024:.1f} MB)')
print()

# First 64 bytes hexdump
print('First 64 bytes (hex):')
for i in range(0, 64, 16):
    hex_str = ' '.join('{:02x}'.format(b) for b in data[i:i+16])
    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
    print('  {:04x}: {}  {}'.format(i, hex_str, ascii_str))
print()

# First 4 dwords
for off in [0, 4, 8, 12, 16]:
    val = struct.unpack('<I', data[off:off+4])[0]
    print('u32@0x{:04x} = 0x{:08x} ({})'.format(off, val, val))
print()

# Search for known Nekki/Shadow Fight strings
print('=== Scanning for known strings ===')
targets = [b'Nekki', b'Shadow', b'shadow', b'nekki', b'Fight', b'fight',
           b'Animation', b'anim', b'Player', b'player', b'Enemy', b'enemy',
           b'Move', b'move', b'Attack', b'attack', b'Combo', b'combo',
           b'Skeleton', b'skeleton', b'Body', b'body', b'Health', b'health',
           b'Damage', b'damage', b'Gold', b'gold', b'Crystal', b'crystal',
           b'Currency', b'currency', b'Scene', b'scene', b'Fight',
           b'Dojo', b'Location', b'Weapon', b'weapon',
           b'UnityEngine', b'mscorlib', b'System', b'System.', b'Unity']

for t in targets:
    idx = data.find(t)
    if idx >= 0:
        start = max(0, idx - 8)
        end = min(len(data), idx + len(t) + 60)
        ctx = data[start:end]
        # Read the full null-terminated string
        null_end = data.find(b'\x00', idx)
        if null_end > idx and null_end - idx < 200:
            s = data[idx:null_end].decode('ascii', errors='replace')
        else:
            s = data[idx:idx+60]
        print('  [0x{:06x}] {}'.format(idx, s))
