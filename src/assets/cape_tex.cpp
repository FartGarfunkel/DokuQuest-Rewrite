// assets/cape_tex.cpp — see cape_tex.h
#include "assets/cape_tex.h"
#include "assets/char_model.h"
#include "assets/char_creator.h"
#include "assets/tileset.h"
#include "common/settings.h"
#include <stb_image.h>
#include <mutex>
#include <map>
#include <algorithm>

namespace dq {

const std::vector<CapeRect>& capeRects() {
  static const std::vector<CapeRect> r = {
    {1, 0, 10, 1, "Top edge", false}, {11, 0, 10, 1, "Bottom edge", false}, {1, 1, 10, 16, "Outside (back)", false},
    {12, 1, 10, 16, "Inside (against the body)", true}, {0, 1, 1, 16, "Right edge", false}, {11, 1, 1, 16, "Left edge", false}};
  return r;
}
bool capePixelUsed(int x, int y) {
  for (auto& r : capeRects()) if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h) return true;
  return false;
}

static void paintRect(CapePixels& c, const CapeRect& r, u8 R, u8 G, u8 B) {
  for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) { u8* p = c.at(x, y); p[0] = R; p[1] = G; p[2] = B; p[3] = 255; }
}
CapePixels defaultCapePixels() {
  CapePixels c;
  for (auto& r : capeRects()) {
    if (r.inner) paintRect(c, r, 0x6e, 0x1c, 0x1c); else if (r.h == 16 && r.w == 10) paintRect(c, r, 0xa8, 0x2a, 0x2a); else paintRect(c, r, 0x7d, 0x20, 0x20);
  }
  for (int x = 1; x < 11; x++) for (int y : {1, 2, 16}) { u8* p = c.at(x, y); p[0] = 0xe8; p[1] = 0xb2; p[2] = 0x5a; }   // a gold trim on the outside
  return c;
}
CapePixels blankCapePixels() {
  CapePixels c;
  for (auto& r : capeRects()) paintRect(c, r, 0x9a, 0x9a, 0x9a);
  return c;
}

fs::path capesDir() { return userSub("capes"); }
std::vector<std::string> listCapes() {
  std::vector<std::string> out; std::error_code ec;
  for (auto& e : fs::directory_iterator(capesDir(), ec)) {
    if (!e.is_regular_file()) continue;
    std::string n = e.path().filename().string();
    if (endsWith(toLower(n), ".png")) out.push_back(n.substr(0, n.size() - 4));
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool loadCapePng(const fs::path& p, CapePixels& out, std::string* err) {
  auto d = readBinary(p);
  if (!d) { if (err) *err = "Could not read that file."; return false; }
  int w, h, ch;
  u8* px = stbi_load_from_memory(d->data(), (int)d->size(), &w, &h, &ch, 4);
  if (!px) { if (err) *err = "That file is not a PNG image."; return false; }
  bool ok = true;
  CapePixels c;
  if (w >= 64 && h >= 32 && w % 64 == 0 && w * 32 == h * 64) {            // 64x32, 128x64 ...: downsample to 64x32 (nearest)
    int k = w / 64;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) std::copy_n(px + ((size_t)(y * k) * w + x * k) * 4, 4, c.at(x, y));
  } else if (w == 22 && h == 17) {                                        // the old cape size: same pictures, no top/bottom strips
    for (int y = 0; y < 17; y++) for (int x = 0; x < 22; x++) std::copy_n(px + ((size_t)y * w + x) * 4, 4, c.at(x, y));
  } else if (w >= 64 && h >= 32) {                                        // a taller file (a skin sheet): its top-left 64x32
    for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) std::copy_n(px + ((size_t)y * w + x) * 4, 4, c.at(x, y));
  } else { ok = false; if (err) *err = strFormat("That is %dx%d - a cape is 64x32.", w, h); }
  stbi_image_free(px);
  if (!ok) return false;
  // only the layout's own pixels count; the capes everyone makes are opaque, so a see-through pixel gets the colour of its neighbour
  CapePixels clean;
  for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) if (capePixelUsed(x, y)) {
    const u8* s = c.at(x, y); u8* o = clean.at(x, y);
    if (s[3] >= 128) { std::copy_n(s, 3, o); o[3] = 255; } else { o[0] = 0x9a; o[1] = 0x9a; o[2] = 0x9a; o[3] = 255; }
  }
  out = std::move(clean);
  return true;
}
bool saveCapePng(const fs::path& p, const CapePixels& c) {
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  return savePng(p, c.w, c.h, c.rgba.data());
}
static fs::path namedPath(const std::string& name) { return capesDir() / (sanitizeName(name) + ".png"); }
bool loadCapeNamed(const std::string& name, CapePixels& out) { return !name.empty() && loadCapePng(namedPath(name), out); }
bool saveCapeNamed(const std::string& name, const CapePixels& c) { return !sanitizeName(name).empty() && saveCapePng(namedPath(name), c); }

std::string wornCapeName(const Settings& s) {
  return s.extra.is_object() && s.extra.contains("cape") && s.extra["cape"].is_string() ? s.extra["cape"].get<std::string>() : std::string();
}
void setWornCapeName(Settings& s, const std::string& name) {
  if (!s.extra.is_object()) s.extra = json::object();
  if (name.empty()) s.extra.erase("cape"); else s.extra["cape"] = name;
  s.save();
}

namespace {
std::mutex g_mu;
std::map<std::string, CapePixels>& live() { static std::map<std::string, CapePixels> m; return m; }
}
bool loadCustomCape(const std::string& key, CapePixels& out) {
  if (!isCustomCape(key)) return false;
  std::string name = key.substr(5);
  { std::lock_guard<std::mutex> l(g_mu); auto it = live().find(name); if (it != live().end()) { out = it->second; return true; } }
  return loadCapeNamed(name, out);
}
void setLiveCape(const std::string& name, const CapePixels& c) {
  { std::lock_guard<std::mutex> l(g_mu); live()[name] = c; }
  capeMeshInvalidate(customCapeKey(name));
}
void dropLiveCape(const std::string& name) {
  { std::lock_guard<std::mutex> l(g_mu); live().erase(name); }
  capeMeshInvalidate(customCapeKey(name));
}

} // namespace dq
