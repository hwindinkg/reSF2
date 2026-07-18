#include "asset_manager.hpp"
#include "../reverse/dz_reader.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>

namespace resf2::core {

void AssetManager::init(std::string asset_root) {
    asset_root_ = std::move(asset_root);
}

AssetData AssetManager::load_raw(const std::string& path) {
    // Try filesystem first
    std::string full = asset_root_.empty() ? path : asset_root_ + "/" + path;
    std::ifstream f(full, std::ios::binary | std::ios::ate);
    if (f) {
        auto sz = (size_t)f.tellg();
        if (sz) {
            f.seekg(0);
            std::vector<uint8_t> data(sz);
            f.read((char*)data.data(), (std::streamsize)sz);
            return {std::move(data), path};
        }
    }

    // Try DZ archive
    auto& dz_reg = resf2::dz::DzRegistry::instance();
    auto basename = std::filesystem::path(path).filename().string();
    if (dz_reg.has_file(basename)) {
        auto d = dz_reg.read_file(basename);
        std::vector<uint8_t> bytes(d.size());
        for (size_t i = 0; i < d.size(); i++) bytes[i] = (uint8_t)d[i];
        return {std::move(bytes), path};
    }

    return {};
}

void AssetManager::load(uint32_t id, DecodeCallback cb) {
    auto it = cache_.find(id);
    if (it != cache_.end() && it->second.loaded) {
        if (cb) cb(id, it->second.data);
        return;
    }

    std::string path = resolve_path(id);
    auto data = load_raw(path);
    if (data.bytes.empty()) return;

    cache_[id] = {data, true};
    if (cb) cb(id, data);
}

void AssetManager::load_batch(const std::vector<uint32_t>& ids, std::function<void()> on_complete) {
    for (auto id : ids) {
        auto path = resolve_path(id);
        auto data = load_raw(path);
        if (!data.bytes.empty()) {
            cache_[id] = {std::move(data), true};
        }
    }
    if (on_complete) on_complete();
}

void AssetManager::cache(uint32_t id, AssetData data) {
    cache_[id] = {std::move(data), true};
}

AssetData* AssetManager::get(uint32_t id) {
    auto it = cache_.find(id);
    if (it != cache_.end() && it->second.loaded)
        return &it->second.data;
    return nullptr;
}

bool AssetManager::is_cached(uint32_t id) const {
    auto it = cache_.find(id);
    return it != cache_.end() && it->second.loaded;
}

std::string AssetManager::resolve_path(uint32_t id) const {
    auto it = id_to_path_.find(id);
    if (it == id_to_path_.end()) return {};
    return it->second;
}

uint32_t AssetManager::path_to_id(const std::string& path) {
    auto it = path_to_id_.find(path);
    if (it != path_to_id_.end()) return it->second;
    uint32_t id = next_id_++;
    path_to_id_[path] = id;
    id_to_path_[id] = path;
    return id;
}

} // namespace resf2::core
