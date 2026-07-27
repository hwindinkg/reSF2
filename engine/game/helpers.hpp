#pragma once

#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstddef>
#include <filesystem>

namespace resf2::game {

extern bool g_debug_log_enabled;
extern FILE* g_debug_log;

void debug_log_init(const std::string& path);
void debug_log(const char* fmt, ...);
void debug_log_close();

std::vector<std::byte> read_file(const std::string& path, class resf2::dz::DzRegistry* dz = nullptr);
std::string read_text(const std::string& path, class resf2::dz::DzRegistry* dz = nullptr);
std::string xml_attr(const std::string& tag, const std::string& attr);
float tof(const std::string& s, float def = 0.0f);
int toi(const std::string& s, int def = 0);
std::filesystem::path get_exe_dir();
std::vector<std::filesystem::path> model_paths(const std::string& asset_root, const char* filename);

} // namespace resf2::game
