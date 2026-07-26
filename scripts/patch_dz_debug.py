with open('E:/reSF2/engine/game/game.cpp','r',encoding='utf-8',errors='ignore') as f:
    content = f.read()

# Find and replace the DZ section
idx = content.find('// Open DZ archives')
if idx >= 0:
    end_of_block = content.find('// Load stage data', idx)
    old_block = content[idx:end_of_block]
    
    new_block = '''// Open DZ archives
            std::fprintf(stderr, "[I] DZ init\n"); std::fflush(stderr);
            auto root = std::filesystem::path(asset_root_);
            std::fprintf(stderr, "[I] DZ root ok\n"); std::fflush(stderr);
            auto& dz = resf2::dz::DzRegistry::instance();
            std::fprintf(stderr, "[I] DZ instance ok\n"); std::fflush(stderr);
            for (const auto& base : {root, root/"assets", root/"assets"/"assets"}) {
                for (const auto& dz_name : {"files.dz", "animations.dz"}) {
                    auto dz_path = base / dz_name;
                    if (std::filesystem::exists(dz_path)) {
                        std::fprintf(stderr, "[I] DZ opening %s\n", dz_path.string().c_str()); std::fflush(stderr);
                        dz.open_archive(dz_path.string());
                        std::fprintf(stderr, "[I] DZ opened ok\n"); std::fflush(stderr);
                    }
                }
            }
            std::fprintf(stderr, "[I] DZ done\n"); std::fflush(stderr);'''
    
    content = content[:idx] + new_block + content[end_of_block:]
    with open('E:/reSF2/engine/game/game.cpp','w',encoding='utf-8') as f:
        f.write(content)
    print('Patched')
else:
    print('NOT FOUND')
