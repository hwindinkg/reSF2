with open('E:/reSF2/main.cpp', 'r') as f:
    content = f.read()

# Fix dt reference - poll_events is before dt calculation
content = content.replace(
    'if (fcount <= 3 || fcount >= 72) { std::fprintf(stderr, "[DBG] POLL #%d dt=%u\\n", fcount, dt); std::fflush(stderr); }',
    'if (fcount <= 3 || fcount >= 72) { std::fprintf(stderr, "[DBG] POLL #%d\\n", fcount); std::fflush(stderr); }')

with open('E:/reSF2/main.cpp', 'w') as f:
    f.write(content)
print('Fixed')
