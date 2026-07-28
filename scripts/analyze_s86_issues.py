#!/usr/bin/env python3
"""
Analyze Shadow Fight 2 s86 binary for functions related to known issues.

Issues analyzed:
1. Tutorial text system (hardcoded vs original localization)
2. Character movement/control systems
3. Dialog and zone display
4. Shop functionality
5. Combat system

This script finds string references and cross-references in the PE32 binary
to locate the original functions responsible for these systems.
"""
import struct
import sys
import os

def parse_pe(data):
    """Parse PE32 headers and return sections + image base."""
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
        sections.append({
            'name': name, 'vaddr': vaddr, 'rawoff': rawoff, 'rawsize': rawsize,
            'va': image_base + vaddr
        })
    return image_base, sections

def find_strings(data, sections, keywords):
    """Find keyword strings in .rdata section."""
    rdata = None
    for s in sections:
        if s['name'] == '.rdata':
            rdata = s
            break
    if not rdata:
        print("ERROR: .rdata section not found")
        return {}
    
    results = {}
    rdata_slice = data[rdata['rawoff']:rdata['rawoff'] + rdata['rawsize']]
    
    for kw in keywords:
        kwb = kw.encode('ascii') if isinstance(kw, str) else kw
        idx = 0
        found = []
        while True:
            idx = rdata_slice.find(kwb, idx)
            if idx == -1:
                break
            va = rdata['va'] + idx
            # Get full string (null-terminated)
            end = rdata_slice.find(b'\x00', idx)
            if end == -1:
                end = idx + len(kwb) + 50
            full_str = rdata_slice[idx:min(end, idx+200)].decode('ascii', errors='replace')
            found.append((va, full_str))
            idx += 1
        if found:
            results[kw] = found
    return results

def find_xrefs(data, sections, string_addrs):
    """Find push imm32 xrefs to string addresses in .text section."""
    text = None
    for s in sections:
        if s['name'] == '.text':
            text = s
            break
    if not text:
        print("ERROR: .text section not found")
        return {}
    
    text_slice = data[text['rawoff']:text['rawoff'] + text['rawsize']]
    results = {}
    
    for kw, addrs in string_addrs.items():
        xrefs = []
        for str_va, full_str in addrs:
            target = struct.pack('<I', str_va)
            idx = 0
            while True:
                idx = text_slice.find(target, idx)
                if idx == -1:
                    break
                # Check for push (0x68) or mov (0xB8+r, 0xC7) patterns
                if idx > 0:
                    prev_byte = text_slice[idx-1]
                    if prev_byte == 0x68:  # push imm32
                        ref_va = text['va'] + idx - 1
                        xrefs.append((ref_va, 'push', str_va, full_str))
                    elif prev_byte >= 0xB8 and prev_byte <= 0xBF:  # mov reg, imm32
                        ref_va = text['va'] + idx - 1
                        xrefs.append((ref_va, 'mov', str_va, full_str))
                idx += 1
        if xrefs:
            results[kw] = xrefs
    return results

def find_function_start(data, sections, addr):
    """Try to find function start by looking for common prologues backwards."""
    text = None
    for s in sections:
        if s['name'] == '.text':
            text = s
            break
    if not text:
        return None
    
    # Convert VA to file offset
    offset = addr - text['va'] + text['rawoff']
    if offset < 0 or offset >= len(data):
        return None
    
    # Search backwards for function prologue (push ebp; mov ebp, esp = 55 8B EC)
    # or other common prologues
    search_start = max(0, offset - 0x2000)  # Search up to 8KB back
    search_data = data[search_start:offset]
    
    # Look for CC CC CC padding (function boundary) followed by push ebp
    best_start = None
    for i in range(len(search_data) - 3, 0, -1):
        # Check for int3 padding before function
        if search_data[i-1] == 0xCC and search_data[i] == 0x55:  # CC 55 (push ebp)
            best_start = search_start + i
            break
        # Check for ret (C3) followed by padding and then push ebp
        if search_data[i-1] == 0xC3 and search_data[i] == 0x55:
            best_start = search_start + i
            break
    
    if best_start:
        return best_start - text['rawoff'] + text['va']
    return None

def main():
    # Find the s86 binary
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(script_dir)
    
    candidates = [
        os.path.join(root_dir, 'reverse', 'binaries', 'ShadowFight2.s86'),
        os.path.join(root_dir, 'OriginalWindowsFiles', 'Shadow%20Fight%202.s86'),
    ]
    
    path = None
    for c in candidates:
        if os.path.exists(c):
            path = c
            break
    
    if not path:
        print("ERROR: s86 binary not found. Tried:")
        for c in candidates:
            print(f"  {c}")
        return 1
    
    print(f"Loading: {path}")
    with open(path, 'rb') as f:
        data = f.read()
    print(f"Size: {len(data)} bytes ({len(data)/1024/1024:.2f} MB)")
    
    image_base, sections = parse_pe(data)
    print(f"Image base: 0x{image_base:08x}")
    for s in sections:
        print(f"  {s['name']:8s} VA=0x{s['va']:08x} size=0x{s['rawsize']:08x}")
    
    # ================================================================
    # ISSUE 1: Tutorial text system
    # ================================================================
    print("\n" + "="*70)
    print("ISSUE 1: TUTORIAL TEXT SYSTEM")
    print("="*70)
    
    tutorial_keywords = [
        'Tutorial', 'tutorial', 'MOVE', 'BAG', 'FIRST_FIGHT',
        'Sensei', 'sensei', 'Dojo', 'dojo', 'training',
        'tutorial_move', 'tutorial_punchbag', 'tutorial_training',
        'Warrior', 'CurrentUser',
    ]
    
    tutorial_strings = find_strings(data, sections, tutorial_keywords)
    for kw, addrs in tutorial_strings.items():
        print(f"\n  '{kw}': {len(addrs)} occurrence(s)")
        for va, s in addrs[:3]:
            print(f"    0x{va:08x}: {s[:80]}")
    
    tutorial_xrefs = find_xrefs(data, sections, tutorial_strings)
    print("\n  Cross-references:")
    for kw, xrefs in tutorial_xrefs.items():
        print(f"    '{kw}': {len(xrefs)} xref(s)")
        for ref_va, ref_type, str_va, s in xrefs[:5]:
            func_start = find_function_start(data, sections, ref_va)
            func_info = f" (func ~0x{func_start:08x})" if func_start else ""
            print(f"      {ref_type} at 0x{ref_va:08x} -> 0x{str_va:08x}{func_info}")
    
    # ================================================================
    # ISSUE 2: Character movement/control
    # ================================================================
    print("\n" + "="*70)
    print("ISSUE 2: CHARACTER MOVEMENT/CONTROL")
    print("="*70)
    
    movement_keywords = [
        'StepForward', 'StepBack', 'step_forward', 'step_back',
        'DoubleStep', 'MoveInside', 'moveinside', 'NPivot',
        'ModelAnimation::mirrorNodes', 'ModelAnimation::getPlayerAnimation',
        'Model::setNearestEnemy', 'Model::step', 'CautiousMovement',
        'ForwardStep', 'BackStep', 'DoubleStepForward',
        's3eKeyboardGetState', 's3eKeyboardUpdate',
        'RootMotion', 'root_motion', 'facing', 'Facing',
    ]
    
    movement_strings = find_strings(data, sections, movement_keywords)
    for kw, addrs in movement_strings.items():
        print(f"\n  '{kw}': {len(addrs)} occurrence(s)")
        for va, s in addrs[:3]:
            print(f"    0x{va:08x}: {s[:80]}")
    
    movement_xrefs = find_xrefs(data, sections, movement_strings)
    print("\n  Cross-references:")
    for kw, xrefs in movement_xrefs.items():
        print(f"    '{kw}': {len(xrefs)} xref(s)")
        for ref_va, ref_type, str_va, s in xrefs[:5]:
            func_start = find_function_start(data, sections, ref_va)
            func_info = f" (func ~0x{func_start:08x})" if func_start else ""
            print(f"      {ref_type} at 0x{ref_va:08x} -> 0x{str_va:08x}{func_info}")
    
    # ================================================================
    # ISSUE 3: Dialog and zone display
    # ================================================================
    print("\n" + "="*70)
    print("ISSUE 3: DIALOG AND ZONE DISPLAY")
    print("="*70)
    
    dialog_keywords = [
        'Dialog', 'dialog', 'Dialogue', 'dialogue',
        'DisplayZone', 'Zone', 'ZONE_', 'zone_',
        'ActScreen', 'MapScreen', 'FightScreen',
        'ScreenModel', 'ScreenFight', 'LoadingScreen',
        'quests.xml', 'stages.xml', 'params.xml',
        'OpenZone', 'ShowBattle', 'HideBattle',
        'characterSensei', 'characterMay',
    ]
    
    dialog_strings = find_strings(data, sections, dialog_keywords)
    for kw, addrs in dialog_strings.items():
        print(f"\n  '{kw}': {len(addrs)} occurrence(s)")
        for va, s in addrs[:3]:
            print(f"    0x{va:08x}: {s[:80]}")
    
    dialog_xrefs = find_xrefs(data, sections, dialog_strings)
    print("\n  Cross-references:")
    for kw, xrefs in dialog_xrefs.items():
        print(f"    '{kw}': {len(xrefs)} xref(s)")
        for ref_va, ref_type, str_va, s in xrefs[:5]:
            func_start = find_function_start(data, sections, ref_va)
            func_info = f" (func ~0x{func_start:08x})" if func_start else ""
            print(f"      {ref_type} at 0x{ref_va:08x} -> 0x{str_va:08x}{func_info}")
    
    # ================================================================
    # ISSUE 4: Shop functionality
    # ================================================================
    print("\n" + "="*70)
    print("ISSUE 4: SHOP FUNCTIONALITY")
    print("="*70)
    
    shop_keywords = [
        'Shop', 'shop', 'ShopScreen', 'OpenShop',
        'Buy', 'Sell', 'Equip', 'Unequip',
        'list.xml', 'Item', 'Price', 'price',
        'Weapon', 'Armor', 'Helm', 'Ranged', 'Magic',
        'Inventory', 'inventory', 'Equipment',
        'shopBuy', 'shopSell', 'btnShopEquip',
    ]
    
    shop_strings = find_strings(data, sections, shop_keywords)
    for kw, addrs in shop_strings.items():
        print(f"\n  '{kw}': {len(addrs)} occurrence(s)")
        for va, s in addrs[:3]:
            print(f"    0x{va:08x}: {s[:80]}")
    
    shop_xrefs = find_xrefs(data, sections, shop_strings)
    print("\n  Cross-references:")
    for kw, xrefs in shop_xrefs.items():
        print(f"    '{kw}': {len(xrefs)} xref(s)")
        for ref_va, ref_type, str_va, s in xrefs[:5]:
            func_start = find_function_start(data, sections, ref_va)
            func_info = f" (func ~0x{func_start:08x})" if func_start else ""
            print(f"      {ref_type} at 0x{ref_va:08x} -> 0x{str_va:08x}{func_info}")
    
    # ================================================================
    # ISSUE 5: Combat system
    # ================================================================
    print("\n" + "="*70)
    print("ISSUE 5: COMBAT SYSTEM")
    print("="*70)
    
    combat_keywords = [
        'IntervalAttack', 'AttackMoves', 'Model::startAction',
        'Model::getTotalDamage', 'Fight::update', 'Fight::Fight',
        'Fight::resetLife', 'Fight::updateFightDataDamage',
        'Model::equipRulesItems', 'ConditionModelMirrored',
        'moves.xml', 'Movesxml', 'MovesParser', 'MovesMaps',
        'Punch', 'Kick', 'Block', 'block', 'Damage', 'damage',
        'Hit', 'hit', 'Combo', 'combo', 'Stun', 'stun',
        'Health', 'health', 'Life', 'life', 'Death', 'death',
        'Tactic', 'tactic', 'tacticSettings', 'AI', 'ai_',
    ]
    
    combat_strings = find_strings(data, sections, combat_keywords)
    for kw, addrs in combat_strings.items():
        print(f"\n  '{kw}': {len(addrs)} occurrence(s)")
        for va, s in addrs[:3]:
            print(f"    0x{va:08x}: {s[:80]}")
    
    combat_xrefs = find_xrefs(data, sections, combat_strings)
    print("\n  Cross-references:")
    for kw, xrefs in combat_xrefs.items():
        print(f"    '{kw}': {len(xrefs)} xref(s)")
        for ref_va, ref_type, str_va, s in xrefs[:5]:
            func_start = find_function_start(data, sections, ref_va)
            func_info = f" (func ~0x{func_start:08x})" if func_start else ""
            print(f"      {ref_type} at 0x{ref_va:08x} -> 0x{str_va:08x}{func_info}")
    
    # ================================================================
    # Summary of key function addresses
    # ================================================================
    print("\n" + "="*70)
    print("SUMMARY: KEY FUNCTION ADDRESSES")
    print("="*70)
    
    all_xrefs = {}
    all_xrefs.update(tutorial_xrefs)
    all_xrefs.update(movement_xrefs)
    all_xrefs.update(dialog_xrefs)
    all_xrefs.update(shop_xrefs)
    all_xrefs.update(combat_xrefs)
    
    # Collect unique function addresses
    func_addrs = {}
    for kw, xrefs in all_xrefs.items():
        for ref_va, ref_type, str_va, s in xrefs:
            func_start = find_function_start(data, sections, ref_va)
            if func_start:
                if func_start not in func_addrs:
                    func_addrs[func_start] = []
                func_addrs[func_start].append((kw, ref_va, s[:50]))
    
    print(f"\nFound {len(func_addrs)} unique function addresses:")
    for addr in sorted(func_addrs.keys()):
        refs = func_addrs[addr]
        print(f"\n  0x{addr:08x}:")
        for kw, ref_va, s in refs[:3]:
            print(f"    [{kw}] xref at 0x{ref_va:08x}: {s}")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
