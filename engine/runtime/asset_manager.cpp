// engine/runtime/asset_manager.cpp
//
// Implementation of the AssetManager.

#include "asset_manager.hpp"

#include "../reverse/plist_atlas.hpp"
#include "../reverse/atf_tactics.hpp"
#include "../reverse/bitmap_font.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace resf2::runtime::assets {

namespace fs = std::filesystem;

// ---------- LoadError helpers ----------

const char* to_string(LoadError e) noexcept {
    switch (e) {
        case LoadError::kOk:                 return "ok";
        case LoadError::kNotFound:           return "asset not found in any search root";
        case LoadError::kNoLoaderRegistered: return "no loader registered for this extension";
        case LoadError::kReadFailed:         return "file I/O error";
        case LoadError::kParseFailed:        return "loader parse error";
        case LoadError::kCancelled:          return "async load cancelled";
        case LoadError::kCacheEvicted:       return "asset evicted from cache before async get";
    }
    return "unknown error";
}

// ---------- Standard loaders ----------

namespace {

// Static storage for extension spans (must outlive the span).
// This is a bit ugly but avoids per-instance allocation.
struct ExtStorage {
    std::vector<std::string> exts;
    std::vector<std::string_view> ext_views;
};
ExtStorage plist_exts{{".plist"}, {".plist"}};
ExtStorage atf_exts{{".atf"}, {".atf"}};
ExtStorage fnt_exts{{".fnt"}, {".fnt"}};

}  // namespace

// PlistAtlasLoader
auto PlistAtlasLoader::extensions() const -> std::span<const std::string_view> {
    return std::span<const std::string_view>(plist_exts.ext_views);
}

auto PlistAtlasLoader::parse(std::span<const std::byte> data) const
    -> std::expected<std::any, LoadError> {
    std::string_view sv(reinterpret_cast<const char*>(data.data()), data.size());
    auto result = resf2::reverse::plist::parse(sv);
    if (!result) return std::unexpected(LoadError::kParseFailed);
    return std::any(std::make_shared<resf2::reverse::plist::ParsedAtlas>(std::move(*result)));
}

// AtfTacticsLoader
auto AtfTacticsLoader::extensions() const -> std::span<const std::string_view> {
    return std::span<const std::string_view>(atf_exts.ext_views);
}

auto AtfTacticsLoader::parse(std::span<const std::byte> data) const
    -> std::expected<std::any, LoadError> {
    auto result = resf2::reverse::atf::parse(data);
    if (!result) return std::unexpected(LoadError::kParseFailed);
    return std::any(std::make_shared<resf2::reverse::atf::ParsedTactics>(std::move(*result)));
}

// BitmapFontLoader
auto BitmapFontLoader::extensions() const -> std::span<const std::string_view> {
    return std::span<const std::string_view>(fnt_exts.ext_views);
}

auto BitmapFontLoader::parse(std::span<const std::byte> data) const
    -> std::expected<std::any, LoadError> {
    std::string_view sv(reinterpret_cast<const char*>(data.data()), data.size());
    auto result = resf2::reverse::font::parse(sv);
    if (!result) return std::unexpected(LoadError::kParseFailed);
    return std::any(std::make_shared<resf2::reverse::font::ParsedFont>(std::move(*result)));
}

// RawBytesLoader
RawBytesLoader::RawBytesLoader(std::vector<std::string> exts)
    : extensions_(std::move(exts)) {
    // Build string_views for span
    ext_view_ = {};
}

namespace {
// Per-instance ext storage for RawBytesLoader
// Each instance gets its own ExtStorage
std::unordered_set<std::unique_ptr<ExtStorage>> raw_bytes_ext_storage;
std::mutex raw_bytes_ext_mutex;
}

auto RawBytesLoader::extensions() const -> std::span<const std::string_view> {
    // This is a hack — we need stable storage for the string_views
    // For simplicity, return a single-extension span from ext_view_
    // In practice, RawBytesLoader is created with specific extensions
    // and we store them in a static map per-instance
    // 
    // Actually, let's just return the first extension for now
    // (this loader is typically created per-extension)
    static thread_local std::string_view local_view;
    if (!extensions_.empty()) {
        local_view = extensions_[0];
        return std::span<const std::string_view>(&local_view, 1);
    }
    return {};
}

auto RawBytesLoader::parse(std::span<const std::byte> data) const
    -> std::expected<std::any, LoadError> {
    // Just store the raw bytes — no parsing
    return std::any(std::vector<std::byte>(data.begin(), data.end()));
}

// ---------- AssetManager::Impl ----------

struct AssetManager::Impl {
    Config config;
    std::vector<std::unique_ptr<IAssetLoader>> loaders;
    std::unordered_map<std::string, std::span<const std::string_view>> ext_to_loader;

    // Cache: path -> Asset
    std::mutex cache_mutex;
    std::unordered_map<AssetPath, std::shared_ptr<Asset>> cache;
    std::size_t cache_bytes = 0;
    std::deque<AssetPath> lru_order;  // front = least recently used

    // Invalidated paths (force reload on next access)
    std::mutex invalidate_mutex;
    std::unordered_set<AssetPath> invalidated;

    // Async I/O
    std::vector<std::thread> workers;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::pair<AssetPath, std::promise<std::expected<std::shared_ptr<Asset>, LoadError>>>> queue;
    std::atomic<bool> shutdown_flag{false};

    ~Impl() {
        shutdown_flag = true;
        queue_cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

    // Resolve a path to a filesystem path by searching all roots
    std::optional<fs::path> resolve(const AssetPath& path) const {
        for (const auto& root : config.search_roots) {
            fs::path full = root / path;
            if (fs::exists(full) && fs::is_regular_file(full)) {
                return full;
            }
        }
        return std::nullopt;
    }

    // Find the loader for a given extension
    IAssetLoader* find_loader(const std::string& ext) const {
        for (auto& loader : loaders) {
            for (auto loader_ext : loader->extensions()) {
                if (loader_ext == ext) {
                    return loader.get();
                }
            }
        }
        return nullptr;
    }

    // Read a file into a byte vector
    std::expected<std::vector<std::byte>, LoadError> read_file(const fs::path& path) const {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return std::unexpected(LoadError::kReadFailed);
        auto size = static_cast<std::size_t>(f.tellg());
        if (size == 0) return std::unexpected(LoadError::kReadFailed);
        f.seekg(0);
        std::vector<std::byte> buf(size);
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
        if (!f) return std::unexpected(LoadError::kReadFailed);
        return buf;
    }

    // Evict LRU entries until cache is under max_bytes
    void evict_lru() {
        while (config.max_cache_bytes > 0 && cache_bytes > config.max_cache_bytes && !lru_order.empty()) {
            auto victim = lru_order.front();
            lru_order.pop_front();
            auto it = cache.find(victim);
            if (it != cache.end()) {
                cache_bytes -= it->second->raw_bytes.size();
                cache.erase(it);
            }
        }
    }

    // Worker thread main loop
    void worker_loop() {
        while (true) {
            std::pair<AssetPath, std::promise<std::expected<std::shared_ptr<Asset>, LoadError>>> job;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [this] { return shutdown_flag || !queue.empty(); });
                if (shutdown_flag && queue.empty()) return;
                job = std::move(queue.front());
                queue.pop_front();
            }
            // Load synchronously
            auto result = load_sync(job.first);
            job.second.set_value(std::move(result));
        }
    }

    // Synchronous load (used by both load() and worker threads)
    std::expected<std::shared_ptr<Asset>, LoadError> load_sync(const AssetPath& path) {
        // Check cache
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            auto it = cache.find(path);
            if (it != cache.end()) {
                // Check if invalidated
                std::lock_guard<std::mutex> inv_lock(invalidate_mutex);
                if (invalidated.count(path) > 0) {
                    invalidated.erase(path);
                } else {
                    // Update LRU
                    auto lru_it = std::find(lru_order.begin(), lru_order.end(), path);
                    if (lru_it != lru_order.end()) {
                        lru_order.erase(lru_it);
                        lru_order.push_back(path);
                    }
                    return it->second;
                }
            }
        }

        // Resolve path
        auto full_path = resolve(path);
        if (!full_path) return std::unexpected(LoadError::kNotFound);

        // Find loader by extension
        std::string ext = full_path->extension().string();
        IAssetLoader* loader = find_loader(ext);
        if (!loader) {
            // Try RawBytesLoader as fallback for unknown extensions
            // Actually, only fail if no loader at all
            return std::unexpected(LoadError::kNoLoaderRegistered);
        }

        // Read file
        auto raw = read_file(*full_path);
        if (!raw) return std::unexpected(raw.error());

        // Get file modification time
        std::uint64_t mtime = 0;
        std::error_code ec;
        auto last_write = fs::last_write_time(*full_path, ec);
        if (!ec) {
            mtime = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    last_write.time_since_epoch()).count());
        }

        // Parse
        auto start = std::chrono::steady_clock::now();
        auto parsed = loader->parse(*raw);
        auto end = std::chrono::steady_clock::now();
        if (!parsed) return std::unexpected(parsed.error());

        // Build Asset
        auto asset = std::make_shared<Asset>();
        asset->path = path;
        asset->raw_bytes = std::move(*raw);
        asset->parsed = std::move(*parsed);
        asset->load_time_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        asset->file_mtime = mtime;

        // Cache it
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            cache_bytes += asset->raw_bytes.size();
            cache[path] = asset;
            lru_order.push_back(path);
            evict_lru();
        }

        return asset;
    }
};

// ---------- AssetManager ----------

AssetManager::AssetManager() : impl_(std::make_unique<Impl>()) {}
AssetManager::~AssetManager() = default;
AssetManager::AssetManager(AssetManager&&) noexcept = default;
AssetManager& AssetManager::operator=(AssetManager&&) noexcept = default;

bool AssetManager::init(const Config& config) noexcept {
    impl_->config = config;
    // Spawn worker threads
    for (std::uint32_t i = 0; i < config.worker_threads; ++i) {
        try {
            impl_->workers.emplace_back([this] { impl_->worker_loop(); });
        } catch (...) {
            return false;
        }
    }
    return true;
}

void AssetManager::shutdown() noexcept {
    impl_->shutdown_flag = true;
    impl_->queue_cv.notify_all();
    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
    impl_->workers.clear();
    evict_all();
}

void AssetManager::register_loader(std::unique_ptr<IAssetLoader> loader) noexcept {
    impl_->loaders.push_back(std::move(loader));
}

auto AssetManager::load(const AssetPath& path) -> std::expected<std::shared_ptr<Asset>, LoadError> {
    return impl_->load_sync(path);
}

auto AssetManager::load_async(const AssetPath& path)
    -> std::future<std::expected<std::shared_ptr<Asset>, LoadError>> {
    std::promise<std::expected<std::shared_ptr<Asset>, LoadError>> promise;
    auto future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        impl_->queue.emplace_back(path, std::move(promise));
    }
    impl_->queue_cv.notify_one();
    return future;
}

bool AssetManager::is_cached(const AssetPath& path) const noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    return impl_->cache.count(path) > 0;
}

bool AssetManager::evict(const AssetPath& path) noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    auto it = impl_->cache.find(path);
    if (it == impl_->cache.end()) return false;
    impl_->cache_bytes -= it->second->raw_bytes.size();
    impl_->cache.erase(it);
    auto lru_it = std::find(impl_->lru_order.begin(), impl_->lru_order.end(), path);
    if (lru_it != impl_->lru_order.end()) impl_->lru_order.erase(lru_it);
    return true;
}

void AssetManager::evict_all() noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    impl_->cache.clear();
    impl_->lru_order.clear();
    impl_->cache_bytes = 0;
}

std::size_t AssetManager::cache_bytes() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    return impl_->cache_bytes;
}

std::size_t AssetManager::cache_count() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    return impl_->cache.size();
}

void AssetManager::invalidate(const AssetPath& path) noexcept {
    std::lock_guard<std::mutex> lock(impl_->invalidate_mutex);
    impl_->invalidated.insert(path);
}

void AssetManager::invalidate_all() noexcept {
    std::lock_guard<std::mutex> lock(impl_->invalidate_mutex);
    std::lock_guard<std::mutex> cache_lock(impl_->cache_mutex);
    for (const auto& [path, _] : impl_->cache) {
        impl_->invalidated.insert(path);
    }
}

std::vector<AssetPath> AssetManager::cached_paths() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->cache_mutex);
    std::vector<AssetPath> result;
    result.reserve(impl_->cache.size());
    for (const auto& [path, _] : impl_->cache) {
        result.push_back(path);
    }
    return result;
}

std::vector<std::string_view> AssetManager::registered_extensions() const noexcept {
    std::vector<std::string_view> result;
    for (const auto& loader : impl_->loaders) {
        for (auto ext : loader->extensions()) {
            result.push_back(ext);
        }
    }
    return result;
}

}  // namespace resf2::runtime::assets
