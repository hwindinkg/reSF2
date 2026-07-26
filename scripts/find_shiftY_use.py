with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find where ShiftY is consumed (not just parsed)
for i, line in enumerate(lines):
    if 'eja' in line and ('align' in line.lower() or 'position' in line.lower() or 'y' in line.lower() or 'move' in line.lower()):
        print('Line %d: %s' % (i+1, line.rstrip()[:200]))

print()

# Find where Velocity X/Y/Z is used (j8 accumulation)
for i, line in enumerate(lines):
    ls = line.strip()
    if ('x =' in ls or '.x +=' in ls) and ('velocity' in ls.lower() or 'j8' in ls or 'acceleration' in ls.lower()):
        print('Velocity use %d: %s' % (i+1, ls[:200]))
