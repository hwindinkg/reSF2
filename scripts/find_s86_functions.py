#!/usr/bin/env python3
"""Find string addresses and xrefs in s86 PE32 binary — optimized."""
import struct

def main():
    path = '/home/z/my-project/work/original_windows/Shadow%20Fight%202.s86'
    with open(path, 'rb') as f:
        data = f.read()
    
    e_lfanew = struct.unpack_from('<I', data, 0x3c)[0]
    num_sections = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    opt_hdr_size = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    opt_hdr_off = e_lfanew + 24
    image_base = struct.unpack_from('<I', data, opt_hdr_off + 28)[0]
    
    sec_off = opt_hdr_off + opt_hdr_size
    sections = []
    for i in range(num_sections):
        off = sec_off + i * 40
        name = data[off:off+8].rstrip(b'\x00').decode('ascii', errors='replace')
        vaddr = struct.unpack_from('<I', data, off + 12)[0]
        rawsize = struct.unpack_from('<I', data, off + 16)[0]
        rawoff = struct.unpack_from('<I', data, off + 20)[0]
        sections.append({'name': name, 'vaddr': vaddr, 'rawoff': rawoff, 'rawsize': rawsize})
    
    text = sections[0]
    rdata = sections[1]
    rdata_start = rdata['rawoff']
    rdata_end = rdata['rawoff'] + rdata['rawsize']
    rdata_va = image_base + rdata['vaddr']
    text_start = text['rawoff']
    text_end = text['rawoff'] + text['rawsize']
    text_va = image_base + text['vaddr']
    
    keywords = [
        b'ModelAnimation::mirrorNodes',
        b'ModelAnimation::getPlayerAnimation',
        b'Model::setNearestEnemy',
        b'IntervalAttack::getFactors',
        b'MoveInside',
        b'step_forward\x00',
        b'step_back\x00',
        b'NPivot\x00',
        b'Model::startAction',
    ]
    
    print(f"Image base: 0x{image_base:08x}")
    print(f".text VA: 0x{text_va:08x} (0x{text['rawsize']:x} bytes)")
    print(f".rdata VA: 0x{rdata_va:08x} (0x{rdata['rawsize']:x} bytes)")
    
    # Find strings in .rdata
    string_addrs = {}
    rdata_slice = data[rdata_start:rdata_end]
    for kw in keywords:
        idx = 0
        while True:
            idx = rdata_slice.find(kw, idx)
            if idx == -1:
                break
            va = rdata_va + idx
            kw_key = kw.rstrip(b'\x00').decode('ascii')
            if kw_key not in string_addrs:
                string_addrs[kw_key] = va
                print(f"  string 0x{va:08x}: {kw_key}")
            idx += 1
    
    # Find xrefs in .text — search for push imm32 (0x68 + 4-byte VA)
    text_slice = data[text_start:text_end]
    print(f"\n=== Cross-references ===")
    for kw, str_va in string_addrs.items():
        target = struct.pack('<I', str_va)
        refs = []
        idx = 0
        while True:
            idx = text_slice.find(target, idx)
            if idx == -1:
                break
            # Check if preceded by push (0x68)
            if idx > 0 and text_slice[idx-1] == 0x68:
                ref_va = text_va + idx - 1
                refs.append(ref_va)
            idx += 1
        if refs:
            print(f"  {kw} (0x{str_va:08x}): {len(refs)} push-xref(s)")
            for r in refs[:3]:
                print(f"    push at 0x{r:08x}")

if __name__ == '__main__':
    main()
