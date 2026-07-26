with open('E:/reSF2/engine/game/game_clean.hpp', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()
idx = content.find('load_loading_screen()')
if idx >= 0:
    end = content.find('\n}', idx) + 5
    with open('E:/reSF2/scripts/check_out.txt', 'w', encoding='utf-8') as f2:
        f2.write(content[idx:end])
        f2.write('\n---\n')
    
    # Also find load_skeleton
    idx2 = content.find('void load_skeleton()')
    if idx2 >= 0:
        end2 = content.find('\n}', idx2) + 5
        with open('E:/reSF2/scripts/check_out.txt', 'a', encoding='utf-8') as f2:
            f2.write(content[idx2:end2])
    print('Written to check_out.txt')
else:
    print('Not found')
