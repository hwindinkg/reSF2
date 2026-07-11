#!/usr/bin/env python3
"""Search s86 for key engine strings - fast regex approach."""
import re
import sys

def main():
    path = '/home/z/my-project/work/original_windows/Shadow%20Fight%202.s86'
    with open(path, 'rb') as f:
        data = f.read()
    print(f'File size: {len(data)} bytes', flush=True)
    # Use regex to find all ASCII strings >= 8 chars
    pattern = re.compile(rb'[\x20-\x7e]{8,}')
    strings = pattern.findall(data)
    print(f'Total strings: {len(strings)}', flush=True)
    keywords = ['ModelPhysics', 'ModelAnimation', 'MoveInside', 'IntervalAttack',
                'NPivot', 'AnimationPlayer', 'PlayerAnimation', 'EnemyAnimation',
                'RootMotion', 'root_motion', 'step_forward', 'step_back',
                'AnimationStart', 'AnimationEnd', 'MirrorNode', 'mirror',
                'facing', 'Facing', 's3eKeyboard', 's3eKey', 's3ePointer',
                'KeyTap', 'KeyHold', 'Pressed', 'Released']
    for kw in keywords:
        kwb = kw.lower().encode()
        matches = set()
        for s in strings:
            if kwb in s.lower():
                try:
                    matches.add(s.decode('ascii'))
                except:
                    pass
                if len(matches) >= 15:
                    break
        if matches:
            print(f'\n=== {kw} ({len(matches)} found) ===')
            for m in sorted(matches)[:10]:
                print(f'  {m}')

if __name__ == '__main__':
    main()
