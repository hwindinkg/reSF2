with open('E:/reSF2/engine/renderer/renderer.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

start = content.find('void Renderer::begin_frame() {')
end = content.find('\n}', start) + 2
old = content[start:end]

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
    with open('E:/reSF2/engine/renderer/renderer.cpp', 'w', encoding='utf-8') as f:
        f.write(content)
    print('OK')
else:
    print('Not found, first 100 chars of old:')
    print(repr(old[:100]))
