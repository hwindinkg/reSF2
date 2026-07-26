"""Sweep all code-init/byte-skip combinations to find which gives 0x3C."""
import struct
import sys

with open('assets/files.dz', 'rb') as f:
    dz = f.read()

comp = dz[0x1937:0x1937 + 883]

def bit_decode(prob, code, range_, ip, ie):
    """Range decode one bit, return (bit, code, range, ip)"""
    def norm(c, r, ip, ie):
        while r < 0x1000000 and len(ip) < len(ie) + 10:
            r <<= 8
            if ip < ie:
                c = (c << 8) | ip[0]
                ip = ip[1:]
            else:
                c <<= 8
        return c, r, ip
    
    c, r, ip = norm(code, range_, ip, ie)
    bound = (r >> 11) * prob
    if c < bound:
        r = bound
        prob += (0x800 - prob) >> 5
        return 0, c, r, ip
    else:
        c -= bound
        r -= bound
        prob -= prob >> 5
        return 1, c, r, ip

def try_decode(comp_data, skip, nread, discard_top=0):
    """Try decoding first byte with given init parameters."""
    prob = [0x400] * 8192
    range_ = 0xFFFFFFFF
    code = 0
    
    ip = comp_data[skip:]
    ie = comp_data[skip + nread:] if skip + nread < len(comp_data) else b''
    
    # Read code bytes
    for i in range(nread):
        if ip:
            code = (code << 8) | ip[0]
            ip = ip[1:]
    
    # Optionally discard top byte (LZMA properties byte)
    if discard_top and nread >= 1:
        code = 0
        ip = comp_data[skip + 1:]
        for i in range(nread - 1):
            if ip:
                code = (code << 8) | ip[0]
                ip = ip[1:]
    else:
        ip = comp_data[skip + nread:]
    
    # Full 10K-byte buffer for renormalization
    ie = comp_data[skip:nread + skip + 10000]
    virt_ip = ip  # This might go past end
    
    c, r = code, range_
    
    # Decode is_match
    bit, c, r, ip = bit_decode(prob[0], c, r, ip, ie)
    
    # Decode literal (8 bits)
    sym = 1
    for i in range(8):
        bit, c, r, ip = bit_decode(prob[0x736 + sym], c, r, ip, ie)
        sym = (sym << 1) | bit
    
    byte = (sym - 256) & 0xFF
    return byte, code, c, r

print("Sweeping all code-init positions for first byte = 0x3C...")
print()

# Approach 1: skip + nread combinations
for skip in range(0, min(30, len(comp))):
    for nread in range(3, 6):
        if skip + nread > len(comp):
            continue
        
        # Standard LZMA: first byte is properties, rest is code
        byte, init_code, _, _ = try_decode(comp, skip, nread, discard_top=0)
        if byte == 0x3C:
            print(f"  MATCH! skip={skip} nread={nread} discard=0 init_code=0x{init_code:08X}")

# Approach 2: discard top byte
for skip in range(0, min(30, len(comp))):
    for nread in range(4, 6):
        if skip + nread > len(comp):
            continue
        byte, init_code, _, _ = try_decode(comp, skip, nread, discard_top=1)
        if byte == 0x3C:
            print(f"  MATCH! skip={skip} nread={nread} discard=top init_code=0x{init_code:08X}")

# Approach 3: Adjust code byte order (LE vs BE)
for skip in range(0, min(20, len(comp))):
    for nread in range(4, 5):
        if skip + nread > len(comp):
            continue
        # LE: bytes read as b0 | b1<<8 | b2<<16 | b3<<24
        raw = comp[skip:skip+4]
        init_code_le = struct.unpack('<I', raw)[0]
        
        prob = [0x400] * 8192
        r, c = 0xFFFFFFFF, init_code_le
        
        # virtual infinite stream
        ie = comp[skip + 4:] + b'\0' * 10000
        ip = ie[:]
        
        bit, c, r, ip = bit_decode(prob[0], c, r, ip, ie)
        sym = 1
        for i in range(8):
            bit, c, r, ip = bit_decode(prob[0x736 + sym], c, r, ip, ie)
            sym = (sym << 1) | bit
        byte = (sym - 256) & 0xFF
        if byte == 0x3C:
            print(f"  MATCH! skip={skip} nread={nread} LE init_code=0x{init_code_le:08X}")

# Approach 4: range init with different values
for skip in range(0, min(10, len(comp))):
    for range_init in [0xFFFFFFFF, 0x7FFFFFFF, 0x3FFFFFFF, 0xFFFFFF, 0x7FFFFF]:
        nread = 4
        if skip + nread > len(comp):
            continue
        
        init_code = struct.unpack('>I', comp[skip:skip+4])[0]
        prob = [0x400] * 8192
        r, c = range_init, init_code
        
        ie = comp[skip + 4:] + b'\0' * 10000
        ip = ie[:]
        
        bit, c, r, ip = bit_decode(prob[0], c, r, ip, ie)
        sym = 1
        for i in range(8):
            bit, c, r, ip = bit_decode(prob[0x736 + sym], c, r, ip, ie)
            sym = (sym << 1) | bit
        byte = (sym - 256) & 0xFF
        if byte == 0x3C:
            print(f"  MATCH! skip={skip} nread={nread} range_init=0x{range_init:08X}")

print()
print("No 0x3C found? Let me print all results:")
for skip in range(0, 10):
    for nread in [4]:
        if skip + nread > len(comp):
            continue
        byte, init_code, c_final, r_final = try_decode(comp, skip, nread, discard_top=0)
        byte_le, _, _, _ = try_decode(comp, skip, nread, discard_top=-1)
        raw = comp[skip:skip+4]
        init_le = struct.unpack('<I', raw)[0]
        print(f"  skip={skip:2d}  init=0x{init_code:08X}  BE_byte=0x{byte:02X}  LE_byte=0x{init_le:08X}")
