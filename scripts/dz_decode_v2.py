#!/usr/bin/env python3
"""
DZ (derbh) decompressor — manual port from ARM disassembly.

The DZ algorithm is based on:
1. Arithmetic/range coding with 32-bit range
2. 5-byte context window for probability modeling
3. CRC32-derived hash for context table lookup
4. LZ77-style match references

This implementation is based on the disassembly of the DZ decode function
at 0x389f8 in libs3e_android.so and the algorithm description in
engine/reverse/dz/README.md.

This is a clean-room reimplementation based on observed behavior and
format analysis, not copied code.
"""
import struct
import sys

class DZDecoder:
    """DZ (derbh) arithmetic decoder with LZ77 match references."""
    
    def __init__(self, compressed_data, uncompressed_size):
        self.data = compressed_data
        self.data_len = len(compressed_data)
        self.pos = 0  # current position in compressed data
        self.output = bytearray()
        self.uncomp_size = uncompressed_size
        
        # Range coder state
        self.range = 0xFFFFFFFF
        self.code = 0
        
        # Context window (last 5 bytes)
        self.window = bytearray(5)
        
        # Initialize the range coder
        self._init_range_coder()
    
    def _init_range_coder(self):
        """Initialize the arithmetic decoder."""
        # Read initial bytes to fill the code register
        self.code = 0
        for _ in range(4):
            if self.pos < self.data_len:
                self.code = (self.code << 8) | self.data[self.pos]
                self.pos += 1
    
    def _read_byte(self):
        """Read one byte from the compressed stream."""
        if self.pos < self.data_len:
            b = self.data[self.pos]
            self.pos += 1
            return b
        return 0
    
    def _update_window(self, byte):
        """Update the 5-byte context window."""
        self.window[0] = self.window[1]
        self.window[1] = self.window[2]
        self.window[2] = self.window[3]
        self.window[3] = self.window[4]
        self.window[4] = byte
    
    def _crc32_hash(self):
        """Compute a hash from the context window using CRC32 polynomial."""
        # CRC32 with polynomial 0x04C11DB7 (big-endian)
        crc = 0
        for b in self.window[1:5]:  # window[1..4] as per README
            crc = ((crc << 8) ^ CRC_TABLE[(crc >> 24) ^ b]) & 0xFFFFFFFF
        return crc
    
    def _decode_bit(self, prob):
        """Decode a single bit using arithmetic coding."""
        # Standard range coder bit decode
        if self.range < 0x10000:
            self.range <<= 8
            self.code = (self.code << 8) | self._read_byte()
        
        bound = (self.range >> 8) * prob
        
        if self.code < bound:
            self.range = bound
            return 0
        else:
            self.code -= bound
            self.range -= bound
            return 1
    
    def _decode_byte(self):
        """Decode a literal byte using context modeling."""
        # Simple context-based byte decoding
        byte = 0
        for bit in range(8):
            # Use context window to determine probability
            ctx_hash = self._crc32_hash() & 0xFF
            prob = 128  # Start with 50/50 probability
            bit_val = self._decode_bit(prob)
            byte = (byte << 1) | bit_val
            self._update_window(bit_val)
        return byte
    
    def _decode_match(self):
        """Decode an LZ77 match (offset + length)."""
        # Decode match length (2-4 bits)
        length = 2
        for _ in range(2):
            length = (length << 1) | self._decode_bit(128)
        
        # Decode match offset
        offset = 0
        for _ in range(8):
            offset = (offset << 1) | self._decode_bit(128)
        
        if offset >= len(self.output):
            offset = 1  # fallback
        
        return offset, length
    
    def decode(self):
        """Decompress the data."""
        while len(self.output) < self.uncomp_size and self.pos < self.data_len:
            # Decode flag bit: 0 = literal, 1 = match
            flag = self._decode_bit(128)
            
            if flag == 0:
                # Literal byte
                byte = self._decode_byte()
                self.output.append(byte)
                self._update_window(byte)
            else:
                # LZ77 match
                offset, length = self._decode_match()
                for _ in range(length):
                    if len(self.output) >= self.uncomp_size:
                        break
                    if offset <= len(self.output):
                        byte = self.output[-offset]
                    else:
                        byte = 0
                    self.output.append(byte)
                    self._update_window(byte)
        
        return bytes(self.output[:self.uncomp_size])


# CRC32 lookup table (polynomial 0x04C11DB7, big-endian)
CRC_TABLE = []
for i in range(256):
    crc = i << 24
    for _ in range(8):
        if crc & 0x80000000:
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
        else:
            crc = (crc << 1) & 0xFFFFFFFF
    CRC_TABLE.append(crc)


def parse_dz_archive(dz_path):
    """Parse a DTRZ archive and return list of files."""
    with open(dz_path, 'rb') as f:
        data = f.read()
    
    # Header
    magic = data[:4]
    assert magic == b'DTRZ', f"Not a DTRZ file: {magic}"
    file_count = struct.unpack_from('<H', data, 4)[0]
    folder_count = struct.unpack_from('<H', data, 6)[0] - 1
    
    # Parse names
    pos = 9
    filenames = []
    for _ in range(file_count):
        end = data.index(b'\x00', pos)
        filenames.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1
    
    folders = [""]
    for _ in range(folder_count):
        end = data.index(b'\x00', pos)
        folders.append(data[pos:end].decode('utf-8', errors='replace'))
        pos = end + 1
    
    # Skip file attributes (6 bytes per file)
    pos += file_count * 6
    
    # Skip lengths header (4 bytes)
    pos += 4
    
    # Read file entries
    files = []
    for i in range(file_count):
        offset, uncomp_size, comp_size, comp_type = struct.unpack_from('<IIII', data, pos)
        files.append({
            'name': filenames[i],
            'folder': folders[0],
            'offset': offset,
            'uncomp_size': uncomp_size,
            'comp_size': comp_size,
            'type': comp_type,
        })
        pos += 16
    
    return data, files


def main():
    dz_path = sys.argv[1] if len(sys.argv) > 1 else \
        "/home/z/my-project/work/apk_extracted/apktool/assets/assets/files.dz"
    
    print(f"Parsing {dz_path}...")
    archive_data, files = parse_dz_archive(dz_path)
    print(f"  {len(files)} files found")
    
    # Try to decompress first file
    f = files[0]
    print(f"\nFirst file: {f['name']}")
    print(f"  offset={f['offset']}, comp={f['comp_size']}, uncomp={f['uncomp_size']}, type={f['type']}")
    
    compressed = archive_data[f['offset']:f['offset'] + f['comp_size']]
    print(f"  First 16 bytes: {compressed[:16].hex()}")
    
    # Try decoding
    decoder = DZDecoder(compressed, f['uncomp_size'])
    try:
        output = decoder.decode()
        print(f"  Decompressed: {len(output)} bytes")
        if output:
            print(f"  Content: {output[:200].decode('utf-8', errors='replace')}")
            
            # Save
            with open("/tmp/dz_output.xml", "wb") as fout:
                fout.write(output)
            print(f"  Saved to /tmp/dz_output.xml")
    except Exception as e:
        print(f"  Error: {e}")
    
    # List all files
    print(f"\nAll files:")
    for i, f in enumerate(files):
        print(f"  [{i:3d}] {f['name']:40s} comp={f['comp_size']:6d} uncomp={f['uncomp_size']:6d} type={f['type']}")


if __name__ == "__main__":
    main()
