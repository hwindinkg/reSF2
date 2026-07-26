# Add debug prints to scenes.cpp for transition tracking
with open('E:/reSF2/engine/scene/scenes.cpp', 'r') as f:
    content = f.read()

# Add before MainMenuScene::on_enter body
content = content.replace(
    'void MainMenuScene::on_enter(SceneContext& ctx) {',
    'void MainMenuScene::on_enter(SceneContext& ctx) {\n    std::printf("[scene] MainMenu::on_enter\\n");'
)

# Add before LoadingScene::on_update body
content = content.replace(
    'void LoadingScene::on_update(SceneContext& ctx) {',
    'void LoadingScene::on_update(SceneContext& ctx) {\n    std::printf("[scene] Loading::on_update dt=%u\\n", ctx.dt_ms);'
)

with open('E:/reSF2/engine/scene/scenes.cpp', 'w') as f:
    f.write(content)
print('Done')
