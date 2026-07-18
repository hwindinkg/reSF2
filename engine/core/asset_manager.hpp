#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resf2::core {

// Asset manager — matches JS `G` class.
// Loads assets by string path or integer ID, with cache and decode dispatch.

struct AssetData {
    std::vector<uint8_t> bytes;
    std::string path;
};

using DecodeCallback = std::function<void(uint32_t id, const AssetData& data)>;

class AssetManager {
public:
    // Init with base asset root path
    void init(std::string asset_root);

    // Load raw bytes by path (from filesystem or DZ archive)
    AssetData load_raw(const std::string& path);

    // Load and decode (triggers callback on completion)
    void load(uint32_t id, DecodeCallback cb);
    void load_batch(const std::vector<uint32_t>& ids, std::function<void()> on_complete);

    // Cache
    void cache(uint32_t id, AssetData data);
    AssetData* get(uint32_t id);
    bool is_cached(uint32_t id) const;

    // Asset path resolution (replaces {lang}, {image}, {scale}, {audio})
    std::string resolve_path(uint32_t id) const;
    uint32_t path_to_id(const std::string& path);

    // Register a file extension mapping
    void register_extension(const std::string& ext, DecodeCallback decoder);

    // Settings
    void set_language(const std::string& lang) { lang_ = lang; }
    void set_device_scale(float scale) { device_scale_ = scale; }

    static AssetManager& instance() {
        static AssetManager inst;
        return inst;
    }

private:
    std::string asset_root_;
    std::string lang_ = "en";
    float device_scale_ = 1.0f;
    uint32_t next_id_ = 2048;

    struct Entry {
        AssetData data;
        bool loaded = false;
    };
    std::unordered_map<uint32_t, Entry> cache_;
    std::unordered_map<std::string, uint32_t> path_to_id_;
    std::unordered_map<uint32_t, std::string> id_to_path_;
    std::unordered_map<std::string, DecodeCallback> decoders_;
};

} // namespace resf2::core
