#!/usr/bin/env python3
"""Decode forge.xml from files.dz using a pure Python binary arithmetic decoder.

Algorithm reverse-engineered from the ARM binary's DZ type-4 decoder.
"""
import struct, os

DZ_PATH = "assets/files.dz"
ASSETS_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets")

def parse_dz(path):
    with open(path, 'rb') as f:
        data = f.read()
    num_files = struct.unpack_from('<H', data, 4)[0]
    num_dirs = struct.unpack_from('<H', data, 6)[0]
    pos = 9
    filenames = []
    for _ in range(num_files):
        end = data.index(b'\x00', pos)
        filenames.append(data[pos:end].decode('utf-8','replace'))
        pos = end + 1
    for _ in range(max(0, num_dirs-1)):
        end = data.index(b'\x00', pos)
        pos = end + 1
    pos += num_files * 6 + 4
    entries = []
    for _ in range(num_files):
        f0, f1, f2, f3 = struct.unpack_from('<IIII', data, pos)
        offset_lo24 = f1 & 0xFFFFFF
        comp_lo24 = f2 & 0xFFFFFF
        ftype = (f2 >> 24) & 0xFF
        entries.append({
            'name': filenames.pop(0),
            'offset': offset_lo24,
            'comp_size': comp_lo24,
            'uncomp_size': f3,
            'type': ftype
        })
        pos += 16
    return data, entries, pos  # data_start = pos

class ArithmeticDecoder:
    """Binary arithmetic decoder matching the DZ type-4 algorithm.
    
    State: 32-bit range and value, 16-bit probability table.
    Uses two-context (CRC32-based) probability model.
    """
    
    MAX_RANGE = 0xFFFFFFFF  # initial full range (2^32 - 1)
    PROB_TABLE_SIZE = 10   # number of probability sub-tables
    PROB_SUBTABLE_SIZE = 64  # entries per sub-table
    PROB_SHIFT = 5  # adaptation rate: prob_new = prob_old +/- (prob_old >> 5)
    PROB_INIT = 0x400  # initial probability (2048 = 50% in 12-bit)
    CONTEXT_HASH_SIZE = 0x1E0  # size of context hash lookup
    
    def __init__(self, data):
        self.data = data
        self.pos = 0  # byte position in input
        self.range_val = self.MAX_RANGE
        self.value = 0
        
        # Probability table: 10 * 64 = 640 entries of uint16
        # Layout in result buffer: res[8]..res[17] = 10 sub-table pointers
        # Each sub-table is at an 0x80-byte interval within the prob buffer
        # Actual data is at 0x40 * 0x10 = 640 uint16 entries
        
        # We'll use a flat probability array
        self.prob = [[self.PROB_INIT] * 64 for _ in range(10)]
        
        # Context state - CRC32 hash of recent decoded bytes
        self.context_history = bytearray(128)  # ring buffer of recent bytes
        self.context_pos = 0
        
        # CRC32 table for context hashing  
        self.crc32_table = self._build_crc32_table()
        
        # Initialize the value from first bytes
        self._init_value()
    
    def _build_crc32_table(self):
        table = []
        for i in range(256):
            crc = i
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xEDB88320
                else:
                    crc >>= 1
            table.append(crc)
        return table
    
    def _crc32_byte(self, crc, byte_val):
        return self.crc32_table[(crc ^ byte_val) & 0xFF] ^ (crc >> 8)
    
    def _init_value(self):
        """Read initial 4 bytes as big-endian value."""
        if self.pos + 4 <= len(self.data):
            self.value = (self.data[self.pos] << 24 |
                          self.data[self.pos + 1] << 16 |
                          self.data[self.pos + 2] << 8 |
                          self.data[self.pos + 3])
            self.pos += 4
        else:
            self.value = 0
    
    def _renormalize(self):
        """Shift bits into value to keep it in range."""
        while self.range_val < 0x1000000:  # threshold for 24-bit range
            self.range_val <<= 8
            self.value <<= 8
            if self.pos < len(self.data):
                self.value |= self.data[self.pos]
                self.pos += 1
    
    def _get_context_index(self, context_id):
        """Compute context index from recent decoded bytes using CRC32.
        
        The DZ algorithm uses a 5-byte context window:
        - bytes at positions -1, -2, -3, -4, -5 relative to current position
        - These feed into a CRC32 hash to produce the context index
        """
        # Build 5-byte context from recent history
        ctx_bytes = bytearray(5)
        for i in range(5):
            idx = (self.context_pos - 1 - i) % len(self.context_history)
            ctx_bytes[i] = self.context_history[idx]
        
        # CRC32 hash of the context bytes
        crc = 0xFFFFFFFF
        for b in ctx_bytes:
            crc = self._crc32_byte(crc, b)
        crc ^= 0xFFFFFFFF
        
        # Combine with context_id to get probability table index
        # Each context_id selects a sub-table
        return (context_id + (crc & 0x3F)) % 64
    
    def decode_bit(self, context_id):
        """Decode one binary symbol using probability from context_id."""
        self._renormalize()
        
        # Get probability entry for this context
        subtable = self.prob[context_id % 10]
        ctx_idx = self._get_context_index(context_id)
        prob = subtable[ctx_idx]
        
        # Split the range
        mid = 1 + (self.range_val * prob) // 0x1000
        
        if self.value < mid:
            # Decoded 0 (more probable symbol)
            self.range_val = mid
            prob -= prob >> 5  # Increase probability for next time
            bit = 0
        else:
            # Decoded 1 (less probable symbol)
            self.range_val -= mid
            self.value -= mid
            prob += (0x1000 - prob) >> 5  # Decrease probability for next time
            bit = 1
        
        # Update probability table
        self.prob[context_id % 10][ctx_idx] = max(1, min(0xFFF, prob))
        
        return bit
    
    def decode_byte(self):
        """Decode 8 bits to form a byte."""
        val = 0
        for i in range(8):
            bit = self.decode_bit(9)  # use slot 9 for raw byte decoding
            val = (val << 1) | bit
        
        # Update context history
        self.context_history[self.context_pos % len(self.context_history)] = val
        self.context_pos += 1
        
        return val
    
    def decode(self, size):
        """Decode 'size' bytes."""
        output = bytearray()
        while len(output) < size:
            output.append(self.decode_byte())
        return bytes(output)


def main():
    print("=== DZ type-4 Decoder (Pure Python) ===")
    print()
    
    # Parse files.dz
    dz_data, entries, data_start = parse_dz(DZ_PATH)
    
    # Find forge.xml
    target = None
    for e in entries:
        if e['name'] == 'forge.xml':
            target = e
            break
    
    if not target:
        print("forge.xml not found in files.dz!")
        return
    
    print(f"Target: {target['name']}")
    print(f"  offset={target['offset']}, comp_size={target['comp_size']}, uncomp_size={target['uncomp_size']}")
    
    # Get compressed data
    comp_start = data_start + target['offset']
    comp_data = dz_data[comp_start:comp_start + target['comp_size']]
    print(f"  compressed data: {len(comp_data)} bytes at file offset 0x{comp_start:x}")
    print(f"  first 16 bytes: {comp_data[:16].hex()}")
    print()
    
    # Load ground truth if available
    gt_path = os.path.join(ASSETS_DIR, "forge.xml")
    if os.path.exists(gt_path):
        with open(gt_path, 'rb') as f:
            gt = f.read()
        print(f"Ground truth: {len(gt)} bytes from {gt_path}")
    else:
        gt = None
        print("No ground truth available")
    print()
    
    # Attempt decode
    print("Attempting decode...")
    decoder = ArithmeticDecoder(comp_data)
    
    try:
        output = decoder.decode(target['uncomp_size'])
        print(f"Decoded {len(output)} bytes")
        print(f"Output ({len(output)} bytes):")
        print(output[:200])
        print("...")
        
        if gt:
            if output == gt:
                print("\n*** PERFECT MATCH ***")
            else:
                match_len = 0
                for i in range(min(len(output), len(gt))):
                    if output[i] != gt[i]:
                        break
                    match_len += 1
                print(f"\nMismatch: {match_len}/{len(gt)} bytes match")
                if match_len > 0:
                    print(f"  output[{match_len-4:3d}:{match_len+4:3d}]: {output[max(0,match_len-4):match_len+4].hex()}")
                    print(f"  ground[{match_len-4:3d}:{match_len+4:3d}]:  {gt[max(0,match_len-4):match_len+4].hex()}")
        else:
            print(f"\nFull output ({len(output)} bytes):")
            print(output.decode('utf-8', errors='replace'))
    except Exception as e:
        import traceback
        print(f"Decode error: {e}")
        traceback.print_exc()


if __name__ == "__main__":
    main()
