with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    lines = f.readlines()

# Find CurrentAnimation handler and surrounding function
found = False
for i, line in enumerate(lines):
    if 'case "CurrentAnimation":' in line:
        print('=== Line %d ===' % (i+1))
        for j in range(i, min(i+80, len(lines))):
            print('%5d: %s' % (j+1, lines[j].rstrip()[:200]))
        found = True
        break
if not found:
    # Find by other pattern
    for i, line in enumerate(lines):
        if 'd.name == "CurrentAnimation"' in line:
            print('=== Line %d ===' % (i+1))
            for j in range(max(0,i-5), min(i+30, len(lines))):
                print('%5d: %s' % (j+1, lines[j].rstrip()[:200]))
            break
