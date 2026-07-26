with open('E:/reSF2/engine/game/asset_manager.cpp', 'r', encoding='utf-8', errors='ignore') as f:
    content = f.read()

# Find load_skeleton and add debug prints
idx = content.find('void AssetManager::load_skeleton')
if idx >= 0:
    block = content[idx:idx+600]
    # Replace with debug version
    new_block = '''void AssetManager::load_skeleton(const std::string& asset_root, const std::string& /*location*/) {
    std::fprintf(stderr, "[SK] START\n"); std::fflush(stderr);
    auto candidates = model_paths(asset_root, "skeleton.xml");
    std::fprintf(stderr, "[SK] paths=%zu\n", candidates.size()); std::fflush(stderr);
    std::string path;
    for (const auto& p : candidates) {
        std::fprintf(stderr, "[SK] check %s\n", p.string().c_str()); std::fflush(stderr);
        if (std::filesystem::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) { std::printf("  skeleton.xml NOT FOUND!\\n"); return; }
    std::fprintf(stderr, "[SK] path=%s\n", path.c_str()); std::fflush(stderr);
    auto xml = read_text(path);
    std::fprintf(stderr, "[SK] xml read %zu bytes\n", xml.size()); std::fflush(stderr);
    fmt::XmlDocument doc;
    std::fprintf(stderr, "[SK] about to parse\n"); std::fflush(stderr);
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[skeleton] xml_doc parse error: %s\\n", doc.error().c_str());
        return;
    }
    std::fprintf(stderr, "[SK] parsed ok\n"); std::fflush(stderr);
'''
    content = content.replace(block.split('\n')[0] + block[block.find('\n'):block.find('\n{')+2], new_block)

with open('E:/reSF2/engine/game/asset_manager.cpp', 'w', encoding='utf-8') as f:
    f.write(content)
print('Patched')
