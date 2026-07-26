with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find root motion / NPivot displacement / step movement
for i, line in enumerate(lines):
    ls = line.strip()
    if 'cI' in ls and 'dI' in ls and 'Fk' in ls:
        # This is the MoveInside align consumption function
        print('Align consumption at %d:' % (i+1))
        for j in range(max(0,i-5), min(i+15, len(lines))):
            print('  %d: %s' % (j+1, lines[j].rstrip()[:200]))
        print()
        break
