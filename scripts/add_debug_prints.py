with open('E:/reSF2/engine/game/game.cpp', 'r') as f:
    content = f.read()

steps = [
    ('void Game::init_location() {', 
     'void Game::init_location() {\n    std::printf("[DEBUG] init_location START\\n");'),
    ('load_location(current_location_name_);', '[DEBUG] loading location...'),
    ('load_skeleton();', '[DEBUG] loading skeleton...'),
    ('load_body_model();', '[DEBUG] loading body...'),
    ('load_punching_bag_model();', '[DEBUG] loading bag...'),
    ('load_animations();', '[DEBUG] loading anims...'),
    ('load_moves();', '[DEBUG] loading moves...'),
    ('load_hud_textures();', '[DEBUG] loading hud...'),
    ('load_menu_textures();', '[DEBUG] loading menu...'),
    ('load_sounds();', '[DEBUG] loading sounds...'),
]

for old, msg in steps:
    if old in content:
        content = content.replace(
            old,
            'std::printf("' + msg + '\\n");\n            ' + old
        )

with open('E:/reSF2/engine/game/game.cpp', 'w') as f:
    f.write(content)
print('Done - %d replacements' % len(steps))
