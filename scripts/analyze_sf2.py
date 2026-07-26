with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find Uninterrupt handling
targets = ['Uninterrupt']
for target in targets:
    for i, line in enumerate(lines):
        if target in line and ('name ==' in line or 'finish' in line or 'start' in line):
            # Print context
            print('=== Line %d ===' % (i+1))
            for j in range(max(0,i-3), min(len(lines), i+10)):
                print('  %s' % lines[j].rstrip()[:200])
            print()
