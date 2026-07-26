# Add debug prints to init_location in game.cpp
with open('E:/reSF2/engine/game/game.cpp', 'r') as f:
    content = f.read()

# Add flush to ensure output before crash
debug_fmt = '    std::fprintf(stderr, "%s\\n"); std::fflush(stderr);\n'

insertions = [
    ('void Game::init_location() {',
     'void Game::init_location() {\n    std::fprintf(stderr, "[DBG] init_location START\\n"); std::fflush(stderr);'),
    ('load_location(current_location_name_);',
     'std::fprintf(stderr, "[DBG] load_location...\\n"); std::fflush(stderr);\n            load_location(current_location_name_);'),
    ('load_skeleton();',
     'std::fprintf(stderr, "[DBG] load_skeleton...\\n"); std::fflush(stderr);\n            load_skeleton();'),
    ('load_body_model();',
     'std::fprintf(stderr, "[DBG] load_body_model...\\n"); std::fflush(stderr);\n            load_body_model();'),
    ('load_punching_bag_model();',
     'std::fprintf(stderr, "[DBG] load_punching_bag...\\n"); std::fflush(stderr);\n            load_punching_bag_model();'),
    ('load_animations();',
     'std::fprintf(stderr, "[DBG] load_animations...\\n"); std::fflush(stderr);\n            load_animations();'),
    ('load_moves();',
     'std::fprintf(stderr, "[DBG] load_moves...\\n"); std::fflush(stderr);\n            load_moves();'),
    ('load_hud_textures();',
     'std::fprintf(stderr, "[DBG] load_hud_textures...\\n"); std::fflush(stderr);\n            load_hud_textures();'),
    ('load_menu_textures();',
     'std::fprintf(stderr, "[DBG] load_menu_textures...\\n"); std::fflush(stderr);\n            load_menu_textures();'),
    ('load_sounds();',
     'std::fprintf(stderr, "[DBG] load_sounds...\\n"); std::fflush(stderr);\n            load_sounds();'),
]

for old, new in insertions:
    if old in content:
        content = content.replace(old, new)
    else:
        print('WARNING: not found:', old[:60])

with open('E:/reSF2/engine/game/game.cpp', 'w') as f:
    f.write(content)

print('Done')
