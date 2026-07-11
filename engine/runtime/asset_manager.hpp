// engine/runtime/asset_manager.hpp
//
// AssetManager — central registry for loading game assets from disk.
//
// Stage 6 implementation. Provides:
// - Path resolution (searches multiple roots)
// - Per-format loader plugins (registered at init)
// - Async I/O queue (one worker thread per backing filesystem)
// - Hot-reload watcher (dev-only, opt-in)
// - In-memory cache keyed by canonical path
//
// The manager does NOT know about specific formats — it dispatches
// based on file extension to registered IAssetLoader instances.
//
// Dependencies: std::filesystem, std::thread, std::future.

#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace resf2::runtime::assets {

// Canonical path (normalized, case-insensitive on Windows, case-sensitive
// on Unix). Used as the cache key.
using AssetPath = std::string;

// A loaded asset. The `data` field holds the format-specific parsed
// representation (e.g. resf2::reverse::plist::ParsedAtlas). The
// `raw_bytes` field holds the original file bytes for formats that
// need random access (e.g. textures).
struct Asset {
    AssetPath          path;
    std::vector<std::byte> raw_bytes;
    std::any           parsed;  // format-specific parsed representation
    std::uint64_t      load_time_ms = 0;  // wall-clock load time
    std::uint64_t      file_mtime   = 0;  // source file modification time
};

// Error codes returned by load operations.
enum class LoadError {
    kOk = 0,
    kNotFound,           // path not in any search root
    kNoLoaderRegistered, // no loader for this extension
    kReadFailed,         // I/O error
    kParseFailed,        // loader returned an error
    kCancelled,          // async load was cancelled
    kCacheEvicted,       // asset was evicted before async get()
};

[[nodiscard]] const char* to_string(LoadError e) noexcept;

// Interface for per-format loaders. Implementations:
//   - PlistAtlasLoader   (.plist)
//   - AtfTacticsLoader   (.atf)
//   - BitmapFontLoader   (.fnt)
//   - PngImageLoader     (.png)   [Stage 7.2]
//   - WavAudioLoader     (.wav)   [Stage 7.5]
//   - Mp3AudioLoader     (.mp3)   [Stage 7.5]
//   - DzArchiveLoader    (.dz)    [Stage 5.x — blocked on DZ algorithm]
class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    // Returns the file extensions this loader handles (e.g. {".plist", ".atf"}).
    [[nodiscard]] virtual auto extensions() const -> std::span<const std::string_view> = 0;

    // Parse a raw byte buffer into a format-specific representation.
    // The returned std::any holds the parsed object.
    [[nodiscard]] virtual auto parse(std::span<const std::byte> data) const
        -> std::expected<std::any, LoadError> = 0;
};

// Configuration for the AssetManager.
struct Config {
    // Ordered list of search roots. The manager looks for a requested
    // path in each root until found.
    std::vector<std::filesystem::path> search_roots;

    // Maximum cache size in bytes (0 = unlimited). When the cache
    // exceeds this size, least-recently-used assets are evicted.
    std::size_t max_cache_bytes = 256 * 1024 * 1024;  // 256 MB default

    // Number of worker threads for async I/O. 0 = synchronous.
    std::uint32_t worker_threads = 2;

    // Enable hot-reload watcher (dev-only). When true, the manager
    // watches all search roots for file changes and invalidates
    // the cache + reloads on next access.
    bool hot_reload = false;

    // Load .dz archives as virtual filesystems. When true, paths
    // inside .dz files (e.g. "files.dz://assets/quests.xml") are
    // resolved transparently. Requires the DzArchiveLoader.
    bool mount_dz_archives = true;
};

// The AssetManager itself.
class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
    AssetManager(AssetManager&&) noexcept;
    AssetManager& operator=(AssetManager&&) noexcept;

    // Initialize with the given config. Returns false if search roots
    // don't exist or worker threads can't be created.
    [[nodiscard]] bool init(const Config& config) noexcept;

    // Shut down: cancel pending async loads, join worker threads,
    // clear the cache.
    void shutdown() noexcept;

    // Register a loader for one or more file extensions.
    // Multiple loaders can be registered for different extensions.
    void register_loader(std::unique_ptr<IAssetLoader> loader) noexcept;

    // ---- Synchronous API ----

    // Load an asset by path. The path is searched in all search roots.
    // If the asset is already cached, returns the cached version.
    // Otherwise, reads the file, parses it with the registered loader,
    // caches the result, and returns it.
    [[nodiscard]] auto load(const AssetPath& path) -> std::expected<std::shared_ptr<Asset>, LoadError>;

    // Convenience: load and cast to a specific parsed type.
    // Returns an error if the load fails OR if the parsed type doesn't
    // match T.
    template <typename T>
    [[nodiscard]] auto load_as(const AssetPath& path) -> std::expected<std::shared_ptr<T>, LoadError>;

    // ---- Async API ----

    // Schedule an async load. Returns a future that resolves when the
    // asset is loaded (or an error occurs).
    [[nodiscard]] auto load_async(const AssetPath& path) -> std::future<std::expected<std::shared_ptr<Asset>, LoadError>>;

    // ---- Cache management ----

    // Check if an asset is currently in the cache.
    [[nodiscard]] bool is_cached(const AssetPath& path) const noexcept;

    // Evict a specific asset from the cache. Returns true if evicted.
    bool evict(const AssetPath& path) noexcept;

    // Evict all assets from the cache.
    void evict_all() noexcept;

    // Current cache size in bytes.
    [[nodiscard]] std::size_t cache_bytes() const noexcept;

    // Number of assets currently in the cache.
    [[nodiscard]] std::size_t cache_count() const noexcept;

    // ---- Hot-reload (dev-only) ----

    // Force a reload of an asset on next access, regardless of cache.
    void invalidate(const AssetPath& path) noexcept;

    // Force a reload of all assets on next access.
    void invalidate_all() noexcept;

    // ---- Introspection ----

    // List all currently-cached asset paths.
    [[nodiscard]] std::vector<AssetPath> cached_paths() const noexcept;

    // Get the list of registered extensions.
    [[nodiscard]] std::vector<std::string_view> registered_extensions() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---- Standard loader implementations ----

// Cocos2d-x TexturePacker v2 plist atlas loader.
// Parses .plist files using resf2::reverse::plist::parse().
class PlistAtlasLoader final : public IAssetLoader {
public:
    [[nodiscard]] auto extensions() const -> std::span<const std::string_view> override;
    [[nodiscard]] auto parse(std::span<const std::byte> data) const
        -> std::expected<std::any, LoadError> override;
};

// .atf tactics blob loader.
// zlib-decompresses + parses using resf2::reverse::atf::parse().
class AtfTacticsLoader final : public IAssetLoader {
public:
    [[nodiscard]] auto extensions() const -> std::span<const std::string_view> override;
    [[nodiscard]] auto parse(std::span<const std::byte> data) const
        -> std::expected<std::any, LoadError> override;
};

// AngelCode BMFont bitmap font loader.
// Parses .fnt files using resf2::reverse::font::parse().
class BitmapFontLoader final : public IAssetLoader {
public:
    [[nodiscard]] auto extensions() const -> std::span<const std::string_view> override;
    [[nodiscard]] auto parse(std::span<const std::byte> data) const
        -> std::expected<std::any, LoadError> override;
};

// Generic raw-bytes loader for formats that don't need parsing
// (e.g. .png, .jpg, .wav, .mp3, .mp4, .ttf).
class RawBytesLoader final : public IAssetLoader {
public:
    explicit RawBytesLoader(std::vector<std::string> extensions);
    [[nodiscard]] auto extensions() const -> std::span<const std::string_view> override;
    [[nodiscard]] auto parse(std::span<const std::byte> data) const
        -> std::expected<std::any, LoadError> override;
private:
    std::vector<std::string> extensions_;
    mutable std::string_view ext_view_;  // hack for span lifetime
};

// ---- Template implementation ----

template <typename T>
auto AssetManager::load_as(const AssetPath& path) -> std::expected<std::shared_ptr<T>, LoadError> {
    auto result = load(path);
    if (!result) return std::unexpected(result.error());
    auto& asset = *result;
    if (!asset->parsed.has_value()) {
        return std::unexpected(LoadError::kParseFailed);
    }
    try {
        // std::any_cast to pointer type
        auto* ptr = std::any_cast<std::shared_ptr<T>>(&asset->parsed);
        if (!ptr) {
            return std::unexpected(LoadError::kParseFailed);
        }
        return *ptr;
    } catch (...) {
        return std::unexpected(LoadError::kParseFailed);
    }
}

}  // namespace resf2::runtime::assets
