// common/fs.h — files, folders, JSON and the game's data folders.
#pragma once
#include "common/common.h"
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>

namespace dq {

// a flag in saved data, however it was written: true, a number other than 0, a non-empty text (saves and older
// versions write some flags as 1 and others as true; reading one as the other threw and crashed the Travel Journal)
inline bool jflag(const nlohmann::json& j, const char* k) {
  if (!j.is_object()) return false;
  auto it = j.find(k);
  if (it == j.end() || it->is_null()) return false;
  if (it->is_boolean()) return it->get<bool>();
  if (it->is_number()) return it->get<double>() != 0;
  if (it->is_string()) return !it->get_ref<const std::string&>().empty();
  return !it->empty();
}
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
fs::path dataDir();      // installDir()/data (a release keeps its files in installDir()/data.dqa: readFile/readBinary/readJson look there too)
bool inDataArchive(const fs::path& p);   // is this data/ file in data.dqa?
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
