// engine/game/asset_manager.cpp
//
// AssetManager implementation — asset loading: textures, atlases,
// skeletons, body models, moves, fonts, sounds.

#include "asset_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "engine/renderer/stb_image.h"
#include "game.hpp"  // for read_text, read_file, tof, toi

namespace resf2::game {

namespace plist = resf2::reverse::plist;

// ---------- load_atlas ----------

void AssetManager::load_atlas(const std::string& name, const std::string& loc,
                              const std::string& asset_root) {
    auto root = std::filesystem::path(asset_root);
    for (const auto& dir : {root/"assets"/"1536"/"locations"/loc,
                             root/"assets"/"1536"/"textures",
                             root/"assets"/"1536",
                             root/"1536"/"locations"/loc,
                             root/"1536"/"textures",
                             root/"1536",
                             root/"assets",
                             root}) {
        auto pp = dir/(name+".plist"), pn = dir/(name+".png");
        if (std::filesystem::exists(pp) && std::filesystem::exists(pn)) {
            auto result = plist::parse(read_text(pp.string()));
            if (!result) continue;
            auto png_data = read_file(pn.string());
            int aw, ah, ach;
            auto* atlas_px = stbi_load_from_memory(
                (const stbi_uc*)png_data.data(), (int)png_data.size(),
                &aw, &ah, &ach, 4);
            auto tex = std::make_unique<ren::Texture2D>();
            if (!tex->init_from_png((const uint8_t*)png_data.data(),
                                     png_data.size())) {
                if (atlas_px) stbi_image_free(atlas_px);
                continue;
            }
            AtlasRef a;
            a.texture = std::move(tex);
            a.atlas = std::make_shared<plist::ParsedAtlas>(std::move(*result));
            if (atlas_px) {
                for (auto& [fname, idx] : a.atlas->name_index) {
                    auto& frame = a.atlas->frames[idx];
                    if (!frame.rotated) continue;
                    int fw = frame.atlas_h;
                    int fh = frame.atlas_w;
                    auto ctex = std::make_unique<ren::Texture2D>();
                    std::vector<std::uint8_t> px((size_t)fw * fh * 4);
                    for (int y = 0; y < fh; ++y) {
                        for (int x = 0; x < fw; ++x) {
                            int sx = frame.atlas_x + (fh - 1 - y);
                            int sy = frame.atlas_y + x;
                            if (sx < 0 || sy < 0 || sx >= aw || sy >= ah) continue;
                            int src_idx = (sy * aw + sx) * 4;
                            int dst_idx = (y * fw + x) * 4;
                            px[dst_idx+0] = atlas_px[src_idx+0];
                            px[dst_idx+1] = atlas_px[src_idx+1];
                            px[dst_idx+2] = atlas_px[src_idx+2];
                            px[dst_idx+3] = atlas_px[src_idx+3];
                        }
                    }
                    ctex->init_rgba(fw, fh, px.data());
                    std::string n = fname;
                    if (n.ends_with(".png")) n = n.substr(0, n.size() - 4);
                    a.cropped[n] = std::move(ctex);
                }
                stbi_image_free(atlas_px);
            }
            std::printf("  Atlas '%s': %zu frames, %zu pre-cropped\n",
                        name.c_str(), a.atlas->frames.size(), a.cropped.size());
            atlases_[name] = std::move(a);
            return;
        }
    }
    std::printf("  Atlas '%s' NOT FOUND\n", name.c_str());
}

} // namespace resf2::game
