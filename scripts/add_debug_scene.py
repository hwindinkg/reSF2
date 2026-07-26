with open('E:/reSF2/engine/scene/scenes.cpp', 'r') as f:
    content = f.read()

insertions = [
    ('void LoadingScene::on_update(SceneContext& ctx) {',
     'void LoadingScene::on_update(SceneContext& ctx) {\n    std::fprintf(stderr, "[DBG] Loading::on_update dt=%u\\n", ctx.dt_ms); std::fflush(stderr);'),
    ('void MainMenuScene::on_enter(SceneContext& ctx) {',
     'void MainMenuScene::on_enter(SceneContext& ctx) {\n    std::fprintf(stderr, "[DBG] MainMenu::on_enter\\n"); std::fflush(stderr);'),
    ('ctx.host.host_load_battle_location("dojo");',
     'std::fprintf(stderr, "[DBG] MainMenu::host_load_battle_location\\n"); std::fflush(stderr);\n    ctx.host.host_load_battle_location("dojo");'),
    ('void MainMenuScene::on_update(SceneContext& ctx) {',
     'void MainMenuScene::on_update(SceneContext& ctx) {\n    std::fprintf(stderr, "[DBG] MainMenu::on_update dt=%u\\n", ctx.dt_ms); std::fflush(stderr);'),
]

for old, new in insertions:
    if old in content:
        content = content.replace(old, new)
    else:
        print('WARNING: not found:', old[:60])

with open('E:/reSF2/engine/scene/scenes.cpp', 'w') as f:
    f.write(content)
print('Done')
