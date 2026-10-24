// assets/skin.cpp — skins: the Minecraft layout, the painted default, loading/normalising, arms (render/skins.js,
// ui/skineditor.js helpers).
#include "assets/skin.h"
#include "assets/tileset.h"
#include "common/noise.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <mutex>
#include <map>

namespace dq {

static u64 fnv(const void* d, size_t n, u64 h = 1469598103934665603ull) {
  const u8* p = (const u8*)d;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
u64 SkinImage::hash() const { u64 h = fnv(px.data(), px.size()); u8 s = slim ? 1 : 0; return fnv(&s, 1, h); }

json BodyShape::toJson() const { return json{{"height", height}, {"build", build}, {"head", head}, {"legs", legs}, {"headShape", headShape}}; }
BodyShape BodyShape::fromJson(const json& j) {
  BodyShape b;
  if (!j.is_object()) return b;
  auto f = [&](const char* k, float d) { return clampv(j.value(k, d), 0.5f, 1.6f); };
  b.height = f("height", 1); b.build = f("build", 1); b.head = f("head", 1); b.legs = f("legs", 1);
  b.headShape = j.value("headShape", std::string("square"));
  return b;
}
u64 BodyShape::hash() const { float v[4] = {height, build, head, legs}; return fnv(headShape.data(), headShape.size(), fnv(v, sizeof v)); }

// ------------------------------------------------------------------ layout
std::array<SkinRect, 6> skinRects(int u, int v, int w, int h, int d) {
  // right, left, top, bottom, back, front (FACES order: +x -x +y -y +z -z)
  return {SkinRect{u, v + d, d, h}, SkinRect{u + d + w, v + d, d, h}, SkinRect{u + d, v, w, d}, SkinRect{u + d + w, v, w, d},
          SkinRect{u + d + w + d, v + d, w, h}, SkinRect{u + d, v + d, w, h}};
}

ivec2 skinFacePixel(const std::array<SkinRect, 6>& R, int f, vec3 c, bool mirror) {
  float s, t;
  switch (f) {
    case 0: s = 1 - c.z; t = 1 - c.y; break;
    case 1: s = c.z; t = 1 - c.y; break;
    case 2: s = 1 - c.x; t = 1 - c.z; break;
    case 3: s = 1 - c.x; t = c.z; break;
    case 4: s = c.x; t = 1 - c.y; break;
    default: s = 1 - c.x; t = 1 - c.y; break;
  }
  int rf = f;
  if (mirror) { if (f == 0) rf = 1; else if (f == 1) rf = 0; s = 1 - s; }
  const SkinRect& r = R[rf];
  int x = r.x + clampv((int)std::floor(clampv(s, 0.f, 0.9999f) * r.w), 0, r.w - 1);
  int y = r.y + clampv((int)std::floor(clampv(t, 0.f, 0.9999f) * r.h), 0, r.h - 1);
  return {x, y};
}

const std::array<SkinPartDef, 6>& skinParts(bool slim) {
  static std::array<SkinPartDef, 6> P[2];
  static bool done[2] = {false, false};
  int k = slim ? 1 : 0;
  if (!done[k]) {
    float aw = slim ? 3.f : 4.f;
    int iw = (int)aw;
    P[k] = {SkinPartDef{"head", "Head", "Hat", {-4, 24, -4}, {4, 32, 4}, {0, 0}, {32, 0}, {8, 8, 8}, 0.5f},
            SkinPartDef{"body", "Body", "Jacket", {-4, 12, -2}, {4, 24, 2}, {16, 16}, {16, 32}, {8, 12, 4}, 0.25f},
            SkinPartDef{"armR", "Right arm", "Right sleeve", {4, 12, -2}, {4 + aw, 24, 2}, {40, 16}, {40, 32}, {iw, 12, 4}, 0.25f},
            SkinPartDef{"armL", "Left arm", "Left sleeve", {-4 - aw, 12, -2}, {-4, 24, 2}, {32, 48}, {48, 48}, {iw, 12, 4}, 0.25f},
            SkinPartDef{"legR", "Right leg", "Right trouser leg", {0, 0, -2}, {4, 12, 2}, {0, 16}, {0, 32}, {4, 12, 4}, 0.25f},
            SkinPartDef{"legL", "Left leg", "Left trouser leg", {-4, 0, -2}, {0, 12, 2}, {16, 48}, {0, 48}, {4, 12, 4}, 0.25f}};
    done[k] = true;
  }
  return P[k];
}

const SkinMap& skinMap(bool slim) {
  static SkinMap M[2];
  static bool done[2] = {false, false};
  int k = slim ? 1 : 0;
  if (done[k]) return M[k];
  SkinMap& m = M[k];
  m.map.fill(-1);
  auto& parts = skinParts(slim);
  for (int pi = 0; pi < 6; pi++) for (int L = 0; L < 2; L++) {
    const SkinPartDef& p = parts[pi];
    ivec2 uv = L ? p.ov : p.uv;
    auto R = skinRects(uv.x, uv.y, p.sz.x, p.sz.y, p.sz.z);
    for (int fi = 0; fi < 6; fi++) {
      int e = (int)m.entries.size();
      m.entries.push_back({pi, L, fi, R[fi]});
      m.idx[pi][L][fi] = e;
      const SkinRect& r = R[fi];
      for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) m.map[y * 64 + x] = (i16)e;
    }
  }
  done[k] = true;
  return m;
}

int skinMirrorPixel(const SkinMap& M, int x, int y) {
  static const int MP[6] = {0, 1, 3, 2, 5, 4}, MF[6] = {1, 0, 2, 3, 4, 5};
  int e = M.map[y * 64 + x]; if (e < 0) return -1;
  const SkinMapEntry& E = M.entries[e];
  const SkinRect& r = E.rect;
  const SkinRect& r2 = M.entries[M.idx[MP[E.part]][E.layer][MF[E.face]]].rect;
  return (r2.y + (y - r.y)) * 64 + r2.x + (r.w - 1 - (x - r.x));
}

bool skinIsBasePixel(int x, int y) { return y < 16 ? x < 32 : y < 32 ? x < 56 : (y >= 48 && x >= 16 && x < 48); }

// ------------------------------------------------------------------ the default skin (skins.js paintDefaultSkin)
namespace {
vec3 hexc(const char* h) { unsigned v = (unsigned)std::strtoul(h + 1, nullptr, 16); return vec3((v >> 16) & 255, (v >> 8) & 255, v & 255); }
// paint a box net: fn(face, x, y, w, h) -> colour (alpha 0 = leave)
using BoxFn = std::function<vec4(const char* face, int x, int y, int w, int h)>;
void paintBox(SkinImage& s, Mulberry32& R, int u, int v, int w, int h, int d, const BoxFn& fn, float var = 0.06f) {
  struct F { const char* n; int x0, y0, fw, fh; } faces[6] = {{"top", u + d, v, w, d}, {"bottom", u + d + w, v, w, d}, {"right", u, v + d, d, h},
    {"front", u + d, v + d, w, h}, {"left", u + d + w, v + d, d, h}, {"back", u + d + w + d, v + d, w, h}};
  for (auto& f : faces) for (int y = 0; y < f.fh; y++) for (int x = 0; x < f.fw; x++) {
    vec4 c = fn(f.n, x, y, f.fw, f.fh);
    if (c.a <= 0) continue;
    float k = 1 + ((float)R() - 0.5f) * var;
    u8* o = s.at(f.x0 + x, f.y0 + y);
    o[0] = (u8)clampv((int)(c.r * k), 0, 255); o[1] = (u8)clampv((int)(c.g * k), 0, 255); o[2] = (u8)clampv((int)(c.b * k), 0, 255); o[3] = 255;
  }
}
}

SkinImage paintDefaultSkin() {
  SkinImage s;
  Mulberry32 R(4242);
  auto C = [](const char* h) { return vec4(hexc(h), 1); };
  vec4 hair = C("#4a2c18"), skin = C("#d0a07c"), shirt = C("#2f8f86"), shirtD = C("#236b64"), pants = C("#3c4a8a"), shoes = C("#4a4a4a"), white(245, 245, 245, 1),
       eye = C("#3a5ab0"), mouth = C("#7a4630"), nose = C("#b8866a");
  auto is = [](const char* a, const char* b) { return std::strcmp(a, b) == 0; };
  paintBox(s, R, 0, 0, 8, 8, 8, [&](const char* f, int x, int y, int, int) -> vec4 {
    if (is(f, "top")) return hair;
    if (is(f, "bottom")) return skin;
    if (is(f, "back")) return y < 6 ? hair : skin;
    if (is(f, "front")) {
      if (y < 2 || (y == 2 && (x == 0 || x == 7))) return hair;
      if (y == 3) return x == 1 || x == 6 ? white : x == 2 || x == 5 ? eye : skin;
      if (y == 4 && (x == 3 || x == 4)) return nose;
      if (y == 5 && x >= 2 && x <= 5) return mouth;
      return skin;
    }
    return y < 3 || (is(f, "right") ? x < 3 && y < 5 : x > 4 && y < 5) ? hair : skin;
  });
  auto shirtFn = [&](const char* f, int x, int y, int, int h) -> vec4 { return (is(f, "front") && y == 0 && (x == 3 || x == 4)) ? skin : (is(f, "bottom") || y == h - 1) ? shirtD : shirt; };
  paintBox(s, R, 16, 16, 8, 12, 4, shirtFn);
  auto arm = [&](const char* f, int, int y, int, int) -> vec4 { return is(f, "top") ? shirt : is(f, "bottom") ? skin : y < 4 ? (y == 3 ? shirtD : shirt) : skin; };
  paintBox(s, R, 40, 16, 4, 12, 4, arm); paintBox(s, R, 32, 48, 4, 12, 4, arm);
  auto leg = [&](const char* f, int, int y, int, int) -> vec4 { return is(f, "top") ? pants : is(f, "bottom") ? shoes : y >= 10 ? shoes : pants; };
  paintBox(s, R, 0, 16, 4, 12, 4, leg); paintBox(s, R, 16, 48, 4, 12, 4, leg);
  return s;
}

// ------------------------------------------------------------------ loading
namespace {
void copyRect(u8* px, const std::vector<u8>& src, SkinRect a, SkinRect b, bool flip) {   // resample face rect a (src) into b (px)
  for (int j = 0; j < b.h; j++) for (int i = 0; i < b.w; i++) {
    int si = clampv((int)std::lround((i + 0.5f) * a.w / b.w - 0.5f), 0, a.w - 1); if (flip) si = a.w - 1 - si;
    const u8* s = src.data() + ((size_t)(a.y + j) * 64 + a.x + si) * 4;
    u8* d = px + ((size_t)(b.y + j) * 64 + b.x + i) * 4;
    std::memcpy(d, s, 4);
  }
}
}

void skinFromLegacy(u8* px) {
  // drop a fully opaque hat (old editors filled it), then the left limbs mirrored from the right ones
  bool opaque = true;
  for (int y = 0; y < 16 && opaque; y++) for (int x = 32; x < 64; x++) if (px[(y * 64 + x) * 4 + 3] < 128) { opaque = false; break; }
  if (opaque) for (int y = 0; y < 16; y++) std::memset(px + (y * 64 + 32) * 4, 0, 32 * 4);
  std::vector<u8> src(px, px + 64 * 64 * 4);
  static const int MF[6] = {1, 0, 2, 3, 4, 5};
  for (auto [su, sv, du, dv] : {std::array<int, 4>{40, 16, 32, 48}, std::array<int, 4>{0, 16, 16, 48}}) {
    auto R0 = skinRects(su, sv, 4, 12, 4), R1 = skinRects(du, dv, 4, 12, 4);
    for (int f = 0; f < 6; f++) copyRect(px, src, R0[f], R1[MF[f]], true);
  }
}

void skinConvertArms(SkinImage& s, bool toSlim) {
  int ow = toSlim ? 4 : 3, nw = toSlim ? 3 : 4;
  std::vector<u8> src = s.px;
  for (auto [u, v] : {std::array<int, 2>{40, 16}, {40, 32}, {32, 48}, {48, 48}}) {
    for (int y = v; y < v + 16; y++) std::memset(s.px.data() + (y * 64 + u) * 4, 0, 16 * 4);
    auto R0 = skinRects(u, v, ow, 12, 4), R1 = skinRects(u, v, nw, 12, 4);
    for (int f = 0; f < 6; f++) copyRect(s.px.data(), src, R0[f], R1[f], false);
  }
  s.slim = toSlim;
}

bool normalizeSkin(const u8* rgba, int w, int h, SkinImage& out, std::string* err) {
  bool legacy = w == h * 2;
  if (!(legacy || w == h) || w < 64 || w % 64) { if (err) *err = "That image is not a skin. Skins are 64x64 or 64x32 PNG files."; return false; }
  int H = legacy ? 32 : 64, k = w / 64;
  out = SkinImage();
  for (int y = 0; y < H; y++) for (int x = 0; x < 64; x++) std::memcpy(out.at(x, y), rgba + ((size_t)(y * k) * w + x * k) * 4, 4);   // nearest (no smoothing)
  auto a = [&](int x, int y) { return out.at(x, y)[3]; };
  // slim skins leave the strips a slim arm doesn't use empty
  const int strips[4][4] = {{50, 16, 2, 4}, {54, 20, 2, 12}, {42, 48, 2, 4}, {46, 52, 2, 12}};
  auto emptyStrip = [&](const int* s) { for (int y = s[1]; y < s[1] + s[3]; y++) for (int x = s[0]; x < s[0] + s[2]; x++) if (a(x, y) > 0) return false; return true; };
  bool slim = false;
  if (!legacy) {
    int n = 0; for (auto& s : strips) if (emptyStrip(s)) n++;
    slim = n == 4 || (n >= 3 && a(54, 20) == 0 && a(50, 16) == 0);
    if (!slim) {
      // some editors fill the unused strips with one flat colour (that isn't the arm's own colour carrying on)
      auto c = [&](int x, int y) { const u8* p = out.at(x, y); return (u32)p[0] << 16 | (u32)p[1] << 8 | p[2]; };
      u32 c0 = c(54, 20); u8 a0 = a(54, 20);
      auto flat = [&](const int* s) { for (int y = s[1]; y < s[1] + s[3]; y++) for (int x = s[0]; x < s[0] + s[2]; x++) if (c(x, y) != c0 || a(x, y) != a0) return false; return true; };
      slim = flat(strips[0]) && flat(strips[1]) && flat(strips[2]) && flat(strips[3]) && c(53, 25) != c0 && c(45, 57) != c0 && c(52, 25) != c0;
    }
  }
  if (legacy) skinFromLegacy(out.px.data());
  out.legacy = legacy; out.slim = slim;
  return true;
}

bool loadSkinPngData(const u8* data, size_t n, SkinImage& out, std::string* err) {
  int w, h, ch;
  u8* px = stbi_load_from_memory(data, (int)n, &w, &h, &ch, 4);
  if (!px) { if (err) *err = "That file is not a PNG image."; return false; }
  bool ok = normalizeSkin(px, w, h, out, err);
  stbi_image_free(px);
  return ok;
}

bool loadSkinFile(const fs::path& p, SkinImage& out, std::string* err) {
  auto d = readBinary(p);
  if (!d) { if (err) *err = "Could not read that file."; return false; }
  return loadSkinPngData(d->data(), d->size(), out, err);
}

bool saveSkinPng(const fs::path& p, const SkinImage& s) {
  std::error_code ec; fs::create_directories(p.parent_path(), ec);
  return savePng(p, 64, 64, s.px.data());
}

std::vector<u8> encodeSkinPng(const SkinImage& s) {
  std::vector<u8> out;
  stbi_write_png_to_func([](void* ctx, void* data, int size) { auto* o = (std::vector<u8>*)ctx; o->insert(o->end(), (u8*)data, (u8*)data + size); }, &out, 64, 64, 4, s.px.data(), 64 * 4);
  return out;
}

u32 skinAverage(const SkinImage& s, int x0, int y0, int w, int h) {
  int r = 0, g = 0, b = 0, n = 0;
  for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) { const u8* p = s.at(x, y); if (p[3] < 128) continue; r += p[0]; g += p[1]; b += p[2]; n++; }
  return n ? (u32)(r / n) | (u32)(g / n) << 8 | (u32)(b / n) << 16 | 0xFF000000u : 0;
}

// ------------------------------------------------------------------ where skins live
fs::path skinsDir() { return userSub("skins"); }

std::vector<std::string> listSkins() {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(skinsDir(), ec)) {
    if (!e.is_regular_file()) continue;
    std::string n = e.path().filename().string();
    if (endsWith(toLower(n), ".png")) out.push_back(n);
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::pair<std::string, std::string> skinArms(const std::string& ref) {
  for (const char* a : {"slim", "classic"}) {
    std::string suf = std::string("#") + a;
    if (endsWith(ref, suf)) return {ref.substr(0, ref.size() - suf.size()), a};
  }
  return {ref, ""};
}

namespace {
std::mutex g_regMu;
std::map<std::string, std::shared_ptr<const SkinImage>>& registry() { static auto* m = new std::map<std::string, std::shared_ptr<const SkinImage>>(); return *m; }
}
void registerSkin(const std::string& ref, const SkinImage& s) { std::lock_guard<std::mutex> l(g_regMu); registry()[ref] = std::make_shared<SkinImage>(s); }
std::shared_ptr<const SkinImage> registeredSkin(const std::string& ref) {
  std::lock_guard<std::mutex> l(g_regMu);
  auto it = registry().find(ref);
  return it == registry().end() ? nullptr : it->second;
}

} // namespace dq
