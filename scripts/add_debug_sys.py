with open('E:/reSF2/engine/scene/scene_system.cpp', 'r') as f:
    content = f.read()

if 'void SceneManager::render' in content:
    content = content.replace(
        'void SceneManager::render(SceneContext& ctx) {',
        'void SceneManager::render(SceneContext& ctx) {\n    std::fprintf(stderr, "[DBG] SceneManager::render\\n"); std::fflush(stderr);')

if 'void SceneManager::transition_to' in content:
    content = content.replace(
        'void SceneManager::transition_to(SceneId to) {',
        'void SceneManager::transition_to(SceneId to) {\n    std::fprintf(stderr, "[DBG] SceneManager::transition_to %d\\n", (int)to); std::fflush(stderr);')

with open('E:/reSF2/engine/scene/scene_system.cpp', 'w') as f:
    f.write(content)
print('Done')
