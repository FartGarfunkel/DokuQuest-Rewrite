// common/fs.h — files, folders, JSON and the game's data folders.
#pragma once
#include "common/common.h"
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>

namespace dq {
namespace fs = std::filesystem;
using json = nlohmann::json;

std::optional<std::string> readFile(const fs::path& p);
std::optional<std::vector<u8>> readBinary(const fs::path& p);
bool writeFileAtomic(const fs::path& p, const void* data, size_t n);   // write tmp + rename
inline bool writeFileAtomic(const fs::path& p, const std::string& s) { return writeFileAtomic(p, s.data(), s.size()); }
std::optional<json> readJson(const fs::path& p);
bool writeJson(const fs::path& p, const json& j, bool pretty = true);

// Folders:
//  installDir()  — where the executables are; holds data/ (generated game data) and assets/ (shaders, fonts)
//  userDir()     — per-user folder: ~/.local/share/DokuQuest (Linux), %APPDATA%\DokuQuest (Windows)
//                  saves/, resourcepacks/, mods/, shaderpacks/, skins/, screenshots/, servers/, settings.json
fs::path installDir();
fs::path dataDir();      // installDir()/data
fs::path assetsDir();    // installDir()/assets
fs::path userDir();
fs::path userSub(const char* name);   // creates it
void setUserDirOverride(const fs::path& p); // --userdir (tests, portable installs)

// zlib (miniz) helpers
std::vector<u8> zCompress(const void* data, size_t n, int level = 6);
bool zDecompress(const void* data, size_t n, std::vector<u8>& out, size_t expected = 0);
std::string base64Encode(const u8* d, size_t n);
std::vector<u8> base64Decode(std::string_view s);

} // namespace dq
