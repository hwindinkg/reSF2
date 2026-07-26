# Add debug prints to init_location in game.cpp
with open('E:/reSF2/engine/game/game.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

steps = [
    ('void Game::init_location() {',
     'void Game::init_location() {\n    std::fprintf(stderr, "[I] START\\n"); std::fflush(stderr);'),
    ('load_location(current_location_name_);',
     'std::fprintf(stderr, "[I] load_location\\n"); std::fflush(stderr);\n            load_location(current_location_name_);'),
    ('load_skeleton();',
     'std::fprintf(stderr, "[I] skeleton\\n"); std::fflush(stderr);\n            load_skeleton();'),
    ('load_body_model();',
     'std::fprintf(stderr, "[I] body\\n"); std::fflush(stderr);\n            load_body_model();'),
    ('load_punching_bag_model();',
     'std::fprintf(stderr, "[I] bag\\n"); std::fflush(stderr);\n            load_punching_bag_model();'),
    ('load_animations();',
     'std::fprintf(stderr, "[I] anims\\n"); std::fflush(stderr);\n            load_animations();'),
    ('load_moves();',
     'std::fprintf(stderr, "[I] moves\\n"); std::fflush(stderr);\n            load_moves();'),
    ('load_hud_textures();',
     'std::fprintf(stderr, "[I] hud\\n"); std::fflush(stderr);\n            load_hud_textures();'),
    ('load_menu_textures();',
     'std::fprintf(stderr, "[I] menu\\n"); std::fflush(stderr);\n            load_menu_textures();'),
    ('load_sounds();',
     'std::fprintf(stderr, "[I] sounds\\n"); std::fflush(stderr);\n            load_sounds();'),
]

for old, new in steps:
    if old in content:
        content = content.replace(old, new)
    else:
        print('NOT FOUND:', old[:60])

with open('E:/reSF2/engine/game/game.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print('Done')
