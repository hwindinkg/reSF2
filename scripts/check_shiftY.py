with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find ShiftY handler
for i, line in enumerate(lines):
    if 'ShiftY' in line and ('attributes' in line or 'eja' in line):
        print('=== Line %d ===' % (i+1))
        for j in range(max(0,i-5), min(i+20, len(lines))):
            print('%5d: %s' % (j+1, lines[j].rstrip()[:200]))
        print()
        break

# Find Velocity handler
for i, line in enumerate(lines):
    if 'Velocity' in line and 'x = u.H' in line:
        print('=== Velocity at %d ===' % (i+1))
        for j in range(max(0,i-2), min(i+10, len(lines))):
            print('%5d: %s' % (j+1, lines[j].rstrip()[:200]))
        print()
        break
