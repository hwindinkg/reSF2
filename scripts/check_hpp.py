with open('E:/reSF2/engine/game/game_clean.hpp', 'r', encoding='utf-8', errors='replace') as f:
    content = f.read()
idx = content.find('load_loading_screen()')
if idx >= 0:
    end = content.find('\n}', idx) + 5
    print(content[idx:end])
