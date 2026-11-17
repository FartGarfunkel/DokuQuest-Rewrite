// assets/font_files.cpp — see font_files.h.
#include "assets/font_files.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <cstring>
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace dq {

namespace {
bool isFont(const fs::path& p) {
  std::string e = p.extension().string(); for (auto& c : e) c = (char)std::tolower((unsigned char)c);
  return e == ".ttf" || e == ".otf";
}
void scan(const fs::path& dir, std::vector<std::string>& out) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  std::vector<std::string> here;
  for (auto& f : fs::directory_iterator(dir, ec)) if (f.is_regular_file(ec) && isFont(f.path())) here.push_back(f.path().filename().string());
  std::sort(here.begin(), here.end());
  for (auto& n : here) if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
}
}

std::vector<std::string> fontFiles() {
  std::vector<std::string> out;
  scan(assetsDir() / "fonts", out);
  scan(userDir() / "fonts", out);
  return out;
}

fs::path fontPath(const std::string& name, const char* fallback) {
  std::error_code ec;
  if (!name.empty() && name.find('/') == std::string::npos && name.find('\\') == std::string::npos) {
    for (const fs::path& d : {userDir() / "fonts", assetsDir() / "fonts"}) if (fs::exists(d / name, ec)) return d / name;
  }
  return assetsDir() / "fonts" / fallback;
}

std::string fontLabel(const std::string& name) {
  std::string s = fs::path(name).stem().string();
  for (const char* cut : {"-Regular", "_Regular", "Regular"}) if (s.size() > std::strlen(cut) && s.compare(s.size() - std::strlen(cut), std::strlen(cut), cut) == 0) s.resize(s.size() - std::strlen(cut));
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    const char c = s[i];
    if (c == '-' || c == '_') { out += ' '; continue; }
    if (i && std::isupper((unsigned char)c) && std::islower((unsigned char)s[i - 1])) out += ' ';
    if (i && std::isdigit((unsigned char)c) && std::isalpha((unsigned char)s[i - 1])) out += ' ';
    out += c;
  }
  return out;
}

float fontEmRatio(const fs::path& file) {
  static std::mutex mx; static std::map<std::string, float> cache;
  std::lock_guard<std::mutex> lk(mx);
  const std::string key = file.string();
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  float r = 1;
  auto got = readBinary(file);
  std::vector<u8> data = got ? std::move(*got) : std::vector<u8>();
  if (!data.empty()) {
    stbtt_fontinfo f;
    if (stbtt_InitFont(&f, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) {
      const float perPx = stbtt_ScaleForPixelHeight(&f, 1), perEm = stbtt_ScaleForMappingEmToPixels(&f, 1);
      if (perPx > 0 && perEm > 0) r = std::clamp(perEm / perPx, 0.6f, 2.f);
    }
  }
  cache[key] = r;
  return r;
}

} // namespace dq
