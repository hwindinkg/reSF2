import re

with open('E:/reSF2/engine/renderer/renderer.cpp', 'r') as f:
    content = f.read()

# Replace begin_frame with broken-down version
old = '''void Renderer::begin_frame() {
    static int fc = 0; ++fc; if (fc >= 70) { std::fprintf(stderr, "[DBG] RND begin_frame #%d\\n", fc); std::fflush(stderr); } glClear(GL_COLOR_BUFFER_BIT); camera_.update(16); }'''

new = '''void Renderer::begin_frame() {
    static int fc = 0; ++fc;
    if (fc >= 72) { std::fprintf(stderr, "[DBG] RND clear #%d\\n", fc); std::fflush(stderr); }
    glClear(GL_COLOR_BUFFER_BIT);
    if (fc >= 72) { std::fprintf(stderr, "[DBG] RND cam_upd #%d\\n", fc); std::fflush(stderr); }
    camera_.update(16);
    if (fc >= 72) { std::fprintf(stderr, "[DBG] RND beg_done #%d\\n", fc); std::fflush(stderr); }
}'''

if old in content:
    content = content.replace(old, new)
    with open('E:/reSF2/engine/renderer/renderer.cpp', 'w') as f:
        f.write(content)
    print('Patched')
else:
    # Try finding begin_frame more carefully
    idx = content.find('void Renderer::begin_frame() {')
    if idx >= 0:
        end = content.find('\n}', idx) + 2
        print('Current begin_frame:')
        print(content[idx:end])
