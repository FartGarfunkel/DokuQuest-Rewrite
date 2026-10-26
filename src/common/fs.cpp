#include "common/fs.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <miniz.h>
#if defined(_WIN32)
#include <windows.h>
#undef near   // (windows.h defines near and far as nothing)
#undef far
#else
#include <unistd.h>
#include <climits>
#endif

namespace dq {

std::optional<std::string> readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  std::ostringstream ss; ss << f.rdbuf();
  return ss.str();
}
std::optional<std::vector<u8>> readBinary(const fs::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  if (!f) return std::nullopt;
  auto n = f.tellg(); f.seekg(0);
  std::vector<u8> v((size_t)n);
  if (n > 0) f.read((char*)v.data(), n);
  return v;
}
bool writeFileAtomic(const fs::path& p, const void* data, size_t n) {
  std::error_code ec;
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  fs::path tmp = p; tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data, (std::streamsize)n);
    if (!f) return false;
  }
  fs::rename(tmp, p, ec);
  if (ec) { fs::remove(p, ec); fs::rename(tmp, p, ec); }
  return !ec;
}
std::optional<json> readJson(const fs::path& p) {
  auto s = readFile(p);
  if (!s) return std::nullopt;
  try { return json::parse(*s); } catch (std::exception& e) { DQ_WARN("bad json %s: %s", p.string().c_str(), e.what()); return std::nullopt; }
}
bool writeJson(const fs::path& p, const json& j, bool pretty) {
  return writeFileAtomic(p, pretty ? j.dump(2) : j.dump());
}

static fs::path g_userOverride;
void setUserDirOverride(const fs::path& p) { g_userOverride = p; }

fs::path installDir() {
  static fs::path cached;
  if (!cached.empty()) return cached;
#if defined(_WIN32)
  wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
  cached = fs::path(buf).parent_path();
#else
  char buf[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) { buf[n] = 0; cached = fs::path(buf).parent_path(); } else cached = fs::current_path();
#endif
  // development: run from build dir with data/ next to the binary, or fall back to the source tree
  if (!fs::exists(cached / "data") && fs::exists(fs::current_path() / "data")) cached = fs::current_path();
  return cached;
}
fs::path dataDir() { return installDir() / "data"; }
fs::path assetsDir() { return installDir() / "assets"; }
fs::path userDir() {
  if (!g_userOverride.empty()) { fs::create_directories(g_userOverride); return g_userOverride; }
  static fs::path cached;
  if (!cached.empty()) return cached;
#if defined(_WIN32)
  const char* a = getenv("APPDATA");
  cached = fs::path(a ? a : ".") / "DokuQuest";
#else
  const char* x = getenv("XDG_DATA_HOME");
  const char* h = getenv("HOME");
  cached = x && *x ? fs::path(x) / "DokuQuest" : fs::path(h ? h : ".") / ".local/share/DokuQuest";
#endif
  std::error_code ec; fs::create_directories(cached, ec);
  return cached;
}
fs::path userSub(const char* name) { fs::path p = userDir() / name; std::error_code ec; fs::create_directories(p, ec); return p; }

std::vector<u8> zCompress(const void* data, size_t n, int level) {
  mz_ulong bound = mz_compressBound((mz_ulong)n);
  std::vector<u8> out(bound);
  if (mz_compress2(out.data(), &bound, (const unsigned char*)data, (mz_ulong)n, level) != MZ_OK) return {};
  out.resize(bound);
  return out;
}
bool zDecompress(const void* data, size_t n, std::vector<u8>& out, size_t expected) {
  size_t cap = expected ? expected : std::max<size_t>(n * 4, 1024);
  for (int tries = 0; tries < 8; tries++) {
    out.resize(cap);
    mz_ulong len = (mz_ulong)cap;
    int r = mz_uncompress(out.data(), &len, (const unsigned char*)data, (mz_ulong)n);
    if (r == MZ_OK) { out.resize(len); return true; }
    if (r != MZ_BUF_ERROR) return false;
    cap *= 4;
  }
  return false;
}

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64Encode(const u8* d, size_t n) {
  std::string s; s.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    u32 v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    s += B64[(v >> 18) & 63]; s += B64[(v >> 12) & 63];
    s += i + 1 < n ? B64[(v >> 6) & 63] : '='; s += i + 2 < n ? B64[v & 63] : '=';
  }
  return s;
}
std::vector<u8> base64Decode(std::string_view s) {
  static int T[256]; static bool init = false;
  if (!init) { for (int i = 0; i < 256; i++) T[i] = -1; for (int i = 0; i < 64; i++) T[(u8)B64[i]] = i; init = true; }
  std::vector<u8> out; u32 v = 0; int bits = 0;
  for (char c : s) { int x = T[(u8)c]; if (x < 0) continue; v = v << 6 | x; bits += 6; if (bits >= 8) { bits -= 8; out.push_back((u8)(v >> bits)); } }
  return out;
}

} // namespace dq
