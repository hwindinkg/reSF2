import re
with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    content = f.read()

keywords = ['MoveInside', 'moveInside', 'Verlet', 'verlet', 'pivot', 'Align', 'ShiftY', 'shiftY', 'uninterrupt', 'Uninterrupt', 'CurrentAnimation', 'Velocity']
for kw in keywords:
    idx = content.find(kw)
    count = 0
    while idx >= 0 and count < 3:
        line_start = content.rfind('\n', 0, idx) + 1
        line_end = content.find('\n', idx)
        line = content[line_start:line_end].strip()[:150]
        print('"%s": %s' % (kw, line))
        count += 1
        idx = content.find(kw, idx + 1)
    if count == 0:
        print('Not found: "%s"' % kw)
    print()
