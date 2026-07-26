import re
with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    content = f.read()
keywords = ['findMatchingSlot', 'matchingSlot', 'slotNameLen', 'isEqual', 'nameRangeEqual', 'stringEqualWithRange']
for kw in keywords:
    idx = content.find(kw)
    if idx >= 0:
        print('Found "%s" at offset %d:' % (kw, idx))
        print(content[idx:idx+200])
        print()
    else:
        print('Not found: "%s"' % kw)
