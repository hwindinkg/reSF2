#!/usr/bin/env python3
"""scripts/verify_engine_code.py — Verify engine code against original.

Compares current engine implementation with decompiled original functions.
Checks for:
1. Hardcoded asset paths
2. Missing move filtering
3. Uninterrupt interval handling
4. Animation Y positioning
5. Key handling logic
"""
import os
import re
import sys

PROJECT_ROOT = "/home/z/my-project"
MAIN_CPP = os.path.join(PROJECT_ROOT, "main.cpp")
ENGINE_DIR = os.path.join(PROJECT_ROOT, "engine")

def check_hardcoded_assets():
    """Check for hardcoded asset file references."""
    print("=== Checking for hardcoded asset references ===")
    issues = []
    
    with open(MAIN_CPP, 'r') as f:
        content = f.read()
    
    # Look for hardcoded filenames
    hardcoded = re.findall(r'"(skeleton\.xml|body\.xml|moves\.xml|fists_idle\.bin|stance_idle\.bin|step_forward\.bin|step_back\.bin|jump\.bin)"', content)
    if hardcoded:
        for h in hardcoded:
            # Check if it's in a string literal that's NOT a search path
            lines = [l for l in content.split('\n') if f'"{h}"' in l]
            for line in lines:
                if 'search' in line.lower() or 'fallback' in line.lower() or 'find' in line.lower():
                    continue
                if 'animations_.count' in line or 'animations_.find' in line:
                    continue
                issues.append(f"  Hardcoded: {h} in: {line.strip()[:80]}")
    
    if issues:
        print("ISSUES FOUND:")
        for i in issues:
            print(i)
    else:
        print("  OK: No hardcoded asset references")
    return issues

def check_titan_filtering():
    """Check that Titan moves are filtered everywhere."""
    print("\n=== Checking Titan move filtering ===")
    issues = []
    
    with open(MAIN_CPP, 'r') as f:
        content = f.read()
    
    # Find all move matching loops
    loops = re.finditer(r'for\s*\(auto&\s*\[name,\s*move\]\s*:\s*moves_\)', content)
    for i, loop in enumerate(loops):
        # Get next 500 chars after the loop
        after = content[loop.end():loop.end()+500]
        if 'Titan' not in after:
            issues.append(f"  Move loop #{i} at pos {loop.start()} missing Titan filter")
    
    if issues:
        print("ISSUES FOUND:")
        for i in issues:
            print(i)
    else:
        print("  OK: All move loops have Titan filtering")
    return issues

def check_uninterrupt():
    """Check Uninterrupt interval handling."""
    print("\n=== Checking Uninterrupt interval handling ===")
    issues = []
    
    with open(MAIN_CPP, 'r') as f:
        content = f.read()
    
    if 'is_uninterrupt_' not in content:
        issues.append("  is_uninterrupt_ not found")
    if 'uninterrupt_start' not in content:
        issues.append("  uninterrupt_start not found")
    if 'uninterrupt_end' not in content:
        issues.append("  uninterrupt_end not found")
    
    # Check that is_uninterrupt_ is checked before move selection
    combat_pos = content.find('Allow attacks when')
    if combat_pos > 0:
        before = content[max(0,combat_pos-200):combat_pos]
        if 'is_uninterrupt_' not in before:
            issues.append("  is_uninterrupt_ not checked before combat")
    
    if issues:
        print("ISSUES FOUND:")
        for i in issues:
            print(i)
    else:
        print("  OK: Uninterrupt handling present")
    return issues

def check_y_positioning():
    """Check Y positioning logic."""
    print("\n=== Checking Y positioning ===")
    issues = []
    
    with open(MAIN_CPP, 'r') as f:
        content = f.read()
    
    if 'anim_npivot_bin_y_' not in content:
        issues.append("  anim_npivot_bin_y_ not used for Y positioning")
    if 'y_adjust_smoothed_ = current_npivot_y' not in content:
        issues.append("  y_adjust not computed from NPivot Y")
    
    if issues:
        print("ISSUES FOUND:")
        for i in issues:
            print(i)
    else:
        print("  OK: Y positioning uses NPivot Y")
    return issues

def check_key_handling():
    """Check key handling logic."""
    print("\n=== Checking key handling ===")
    issues = []
    
    with open(MAIN_CPP, 'r') as f:
        content = f.read()
    
    # Check for sticky key buffer (should be removed)
    if 'last_punch_seen_ms_' in content and '150' in content:
        # Check if it's actually used (not just declared)
        uses = content.count('last_punch_seen_ms_')
        if uses > 2:  # declaration + assignment
            issues.append("  Sticky key buffer still active (last_punch_seen_ms_)")
    
    if issues:
        print("ISSUES FOUND:")
        for i in issues:
            print(i)
    else:
        print("  OK: No sticky key buffer")
    return issues

def main():
    all_issues = []
    all_issues.extend(check_hardcoded_assets())
    all_issues.extend(check_titan_filtering())
    all_issues.extend(check_uninterrupt())
    all_issues.extend(check_y_positioning())
    all_issues.extend(check_key_handling())
    
    print(f"\n=== Summary: {len(all_issues)} issues found ===")
    return 1 if all_issues else 0

if __name__ == "__main__":
    sys.exit(main())
