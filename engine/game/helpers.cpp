#include "game.hpp"
#include "engine/reverse/dz_reader.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

// ============================================================
// Helper functions and globals — extracted from monolithic main.cpp
// These are at file scope so inline Game methods in game.hpp can call them.
// ============================================================

FILE* g_debug_log = nullptr;
bool g_debug_log_enabled = true;

void debug_log_init(const std::string& path) {
    if (!g_debug_log_enabled) return;
    g_debug_log = std::fopen(path.c_str(), "w");
    if (g_debug_log) std::fprintf(g_debug_log, "=== reSF2 debug log ===\n");
}

void debug_log(const char* fmt, ...) {
    if (!g_debug_log) return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_debug_log, fmt, args);
    va_end(args);
    std::fflush(g_debug_log);
}

void debug_log_close() {
    if (g_debug_log) { std::fclose(g_debug_log); g_debug_log = nullptr; }
}

std::vector<std::byte> read_file(const std::string& path) {
    // [ORIGINAL] FUN_140308130 — DZ path resolution
    // Filesystem first
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        auto sz = (size_t)f.tellg(); if (!sz) return {};
        f.seekg(0); std::vector<std::byte> d(sz);
        f.read((char*)d.data(), (std::streamsize)sz); return d;
    }

    debug_log("[DZ-LOOKUP] Path: %s\n", path.c_str());

    // Try DZ archive
    auto& dz = resf2::dz::DzRegistry::instance();

    // [ORIGINAL] Original binary: normalize path, extract bare filename after last '\\',
    // and look up by bare name in the archive's file table.
    std::string bare_name;
    auto sep = path.find_last_of("/\\");
    if (sep != std::string::npos)
        bare_name = path.substr(sep + 1);
    else
        bare_name = path;

    debug_log("[DZ-LOOKUP] Bare name: %s\n", bare_name.c_str());

    // Try bare filename first (matches original binary behavior)
    if (bare_name != path && dz.has_file(bare_name)) {
        debug_log("[DZ-LOOKUP] Found by bare name: %s\n", bare_name.c_str());
        return dz.read_file(bare_name);
    }

    // Fall back to relative path logic
    namespace fs = std::filesystem;
    fs::path p(path);
    std::string rel;
    for (auto it = p.begin(); it != p.end(); ++it) {
        if (it->string() == "assets") {
            fs::path sub;
            for (auto it2 = it; it2 != p.end(); ++it2)
                sub /= *it2;
            rel = sub.string();
            break;
        }
    }
    if (rel.empty())
        rel = path;

    debug_log("[DZ-LOOKUP] Relative path: %s\n", rel.c_str());

    if (dz.has_file(rel)) {
        debug_log("[DZ-LOOKUP] Found by relative path: %s\n", rel.c_str());
        return dz.read_file(rel);
    }
    // Try with "assets/" prefix
    if (rel.substr(0, 7) != "assets/") {
        rel = "assets/" + rel;
        debug_log("[DZ-LOOKUP] Trying with assets prefix: %s\n", rel.c_str());
        if (dz.has_file(rel)) {
            debug_log("[DZ-LOOKUP] Found with assets prefix: %s\n", rel.c_str());
            return dz.read_file(rel);
        }
    }

    debug_log("[DZ-LOOKUP] NOT FOUND: %s\n", path.c_str());
    return {};
}

std::string read_text(const std::string& path) {
    auto data = read_file(path);
    if (data.empty()) return {};
    return std::string((const char*)data.data(), data.size());
}

float tof(const std::string& s, float def) {
    if (s.empty()) return def;
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    return (end && end != s.c_str()) ? v : def;
}

int toi(const std::string& s, int def) {
    if (s.empty()) return def;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return (end && end != s.c_str()) ? (int)v : def;
}

std::filesystem::path get_exe_dir() {
    std::filesystem::path p;
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    p = std::filesystem::path(buf).parent_path();
#else
    p = std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
    return p;
}

std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename) {
    namespace fs = std::filesystem;
    fs::path root(asset_root);
    std::vector<fs::path> paths;
    // Try known model locations from the original game.
    // The original game uses assets/models/ directly.
    for (const auto& dir : {root/"assets"/"models"/filename,
                            root/"models"/filename}) {
        if (fs::exists(dir)) paths.push_back(dir);
    }
    return paths;
}
