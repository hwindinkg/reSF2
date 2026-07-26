with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find class np (CurrentAnimation)
found = False
for i, line in enumerate(lines):
    if 'class np' in line or 'class np ' in line:
        print('=== Line %d ===' % (i+1))
        for j in range(i, min(i+40, len(lines))):
            print('%5d: %s' % (j+1, lines[j].rstrip()[:200]))
        found = True
        break
if not found:
    for i, line in enumerate(lines):
        if 'Xe' in line and 'Ye' in line and 'this.ih' in line:
            print('Line %d: %s' % (i+1, line.rstrip()[:200]))
