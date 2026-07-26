with open('E:/reSF2/sf2_pc/www/sf2_beautified.js', 'r', errors='replace') as f:
    content = f.read()

# Find CurrentAnimation handler
idx = 0
count = 0
while count < 5:
    idx = content.find('CurrentAnimation', idx)
    if idx < 0: break
    # Find enclosing block
    block_start = content.rfind('\n', 0, idx) + 1
    block_end = content.find('\n', idx + 50)
    if block_end < 0: block_end = idx + 200
    print('=== Occurrence %d at %d ===' % (count+1, idx))
    print(content[block_start:block_end+1][:300])
    print()
    idx += 1
    count += 1
