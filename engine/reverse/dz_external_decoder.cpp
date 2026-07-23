#include "dz_external_decoder.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace resf2::dz {

DzExternalDecoder::DzExternalDecoder() {
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    std::string exe_dir = std::filesystem::path(exe_path).parent_path().string();

    std::vector<std::string> candidates = {
        exe_dir + "/dzip.exe",
        exe_dir + "/../../download/dzip.exe",
        exe_dir + "/../../../download/dzip.exe",
        exe_dir + "/../../../../download/dzip.exe",
        "E:/reSF2/download/dzip.exe",
        "E:/reSF2/upload/dzip.exe",
    };

    for (const auto& path : candidates) {
        if (std::filesystem::exists(path)) {
            dzip_path_ = path;
            std::printf("[DZ] External decoder: %s\n", dzip_path_.c_str());
            return;
        }
    }
    // Debug: show what was tried
    std::fprintf(stderr, "[DZ] dzip.exe not found! Searched:\n");
    for (const auto& path : candidates) {
        std::fprintf(stderr, "  - %s (%s)\n", path.c_str(),
                     std::filesystem::exists(path) ? "exists" : "not found");
    }
    std::fprintf(stderr, "[DZ] WARNING: dzip.exe not found!\n");
}

bool DzExternalDecoder::extract_archive(const std::string& dz_path) {
    if (cache_.find(dz_path) != cache_.end()) return true;
    if (dzip_path_.empty()) return false;

    std::string abs_dz_path = std::filesystem::absolute(dz_path).string();
    std::string archive_dir = std::filesystem::path(abs_dz_path).parent_path().string();
    std::string archive_stem = std::filesystem::path(abs_dz_path).stem().string();
    std::string extract_dir = archive_dir + "/" + archive_stem;

    std::string cmd = "\"" + dzip_path_ + "\" --decompress \"" + abs_dz_path + "\"";
    std::printf("[DZ] Extracting: %s\n", cmd.c_str());

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);

    if (!CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        std::fprintf(stderr, "[DZ] CreateProcess failed (error %lu)\n", GetLastError());
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        std::fprintf(stderr, "[DZ] dzip.exe returned %lu\n", exit_code);
        return false;
    }

    if (!std::filesystem::exists(extract_dir)) {
        std::fprintf(stderr, "[DZ] Extract dir not found: %s\n", extract_dir.c_str());
        return false;
    }

    std::unordered_map<std::string, std::vector<std::byte>> archive_cache;
    try {
        for (auto& entry : std::filesystem::recursive_directory_iterator(extract_dir)) {
            if (!entry.is_regular_file()) continue;
            auto fname = entry.path().filename().string();
            auto rel_path = std::filesystem::relative(entry.path(), extract_dir).string();
            for (auto& c : rel_path) if (c == '\\') c = '/';

            std::ifstream f(entry.path(), std::ios::binary | std::ios::ate);
            if (!f) continue;
            auto sz = static_cast<size_t>(f.tellg());
            f.seekg(0);
            std::vector<std::byte> data(sz);
            f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
            f.close();

            archive_cache[fname] = data;
            archive_cache[rel_path] = data;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[DZ] Error walking extraction: %s\n", e.what());
        return false;
    }

    if (archive_cache.empty()) {
        std::fprintf(stderr, "[DZ] No files found in extracted archive\n");
        return false;
    }

    std::printf("[DZ] Cached %zu files from %s\n", archive_cache.size(), dz_path.c_str());
    cache_[dz_path] = std::move(archive_cache);
    return true;
}

std::vector<std::byte> DzExternalDecoder::decompress(
    const std::string& dz_path, const std::string& file_name)
{
    if (!extract_archive(dz_path)) return {};

    auto archive_it = cache_.find(dz_path);
    if (archive_it == cache_.end()) return {};

    auto& file_cache = archive_it->second;
    auto file_it = file_cache.find(file_name);
    if (file_it != file_cache.end()) {
        return file_it->second;
    }

    for (auto& [key, data] : file_cache) {
        if (key.size() >= file_name.size() &&
            key.substr(key.size() - file_name.size()) == file_name)
        {
            return data;
        }
    }

    std::fprintf(stderr, "[DZ] File '%s' not found in cache (%zu files)\n",
                 file_name.c_str(), file_cache.size());
    return {};
}

}  // namespace resf2::dz
