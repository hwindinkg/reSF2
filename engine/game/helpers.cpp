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
    fopen_s(&g_debug_log, path.c_str(), "w");
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
    // Try filesystem first
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f) {
        auto sz = (size_t)f.tellg(); if (!sz) return {};
        f.seekg(0); std::vector<std::byte> d(sz);
        f.read((char*)d.data(), (std::streamsize)sz); return d;
    }
    // Try DZ archive
    auto& dz = resf2::dz::DzRegistry::instance();
    std::string rel = path;
    // Strip asset_root if present
    auto pos = rel.find("assets/");
    if (pos != std::string::npos) rel = rel.substr(pos);
    if (dz.has_file(rel)) return dz.read_file(rel);
    // Try with "assets/" prefix
    if (rel.substr(0, 7) != "assets/") {
        rel = "assets/" + rel;
        if (dz.has_file(rel)) return dz.read_file(rel);
    }
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
    // Try all known asset locations from the original game
    for (const auto& dir : {root/"assets"/"1536"/"models"/"player",
                            root/"assets"/"models"/"player",
                            root/"assets"/"1536"/"models",
                            root/"assets"/"models",
                            root/"models"}) {
        auto p = dir / filename;
        if (fs::exists(p)) paths.push_back(p);
    }
    return paths;
}
