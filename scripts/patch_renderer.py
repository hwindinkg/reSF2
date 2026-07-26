with open('E:/reSF2/engine/renderer/renderer.cpp', 'r') as f:
    content = f.read()

content = content.replace(
    'void Renderer::begin_frame() {',
    'void Renderer::begin_frame() {\n    static int fc = 0; ++fc; if (fc >= 70) { std::fprintf(stderr, "[DBG] RND begin_frame #%d\\n", fc); std::fflush(stderr); }')

content = content.replace(
    'void Renderer::end_frame() {',
    'void Renderer::end_frame() {\n    static int fc2 = 0; ++fc2; if (fc2 >= 70) { std::fprintf(stderr, "[DBG] RND end_frame #%d\\n", fc2); std::fflush(stderr); }')

with open('E:/reSF2/engine/renderer/renderer.cpp', 'w') as f:
    f.write(content)
print('Done')
