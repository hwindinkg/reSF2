with open('E:/reSF2/main.cpp', 'r') as f:
    content = f.read()

# Add frame counter and crash-point debug
content = content.replace(
    'while (true) {',
    'while (true) {\n        static int fcount = 0;\n        ++fcount;')

content = content.replace(
    'if (!platform->poll_events()) break;',
    'if (fcount <= 3 || fcount >= 72) { std::fprintf(stderr, "[DBG] POLL #%d dt=%u\n", fcount, dt); std::fflush(stderr); }\n        if (!platform->poll_events()) break;')

content = content.replace(
    'game.on_update(*platform, dt);',
    'if (fcount >= 72) { std::fprintf(stderr, "[DBG] UPDATE #%d\\n", fcount); std::fflush(stderr); }\n        game.on_update(*platform, dt);')

content = content.replace(
    'game.on_render(*platform);',
    'if (fcount >= 72) { std::fprintf(stderr, "[DBG] RENDER #%d\\n", fcount); std::fflush(stderr); }\n        game.on_render(*platform);')

content = content.replace(
    'platform->swap_buffers();',
    'if (fcount >= 72) { std::fprintf(stderr, "[DBG] SWAP #%d\\n", fcount); std::fflush(stderr); }\n        platform->swap_buffers();')

with open('E:/reSF2/main.cpp', 'w') as f:
    f.write(content)
print('Done')
