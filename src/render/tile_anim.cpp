#include "render/tile_anim.h"
#include <cstring>

namespace dq {

void addBuiltinTileAnims(TileSet& ts) {
  auto j = readJson(dataDir() / "tile_anims.json");
  int w = 0, h = 0; std::vector<u8> img;
  if (!j || !loadPng(dataDir() / "tile_anims.png", w, h, img)) { DQ_WARN("data/tile_anims.* missing: tiles won't animate"); return; }
  int S = (*j).value("size", 32);
  if (S != ts.size) return;
  size_t tpx = (size_t)S * S;
  for (auto& a : (*j)["anims"]) {
    auto it = ts.index.find(a.value("tile", std::string()));
    if (it == ts.index.end()) continue;
    int layer = it->second, n = a.value("frames", 0), row = a.value("row", 0);
    float fps = a.value("fps", 8.f);
    if (n < 2 || (row + 1) * S > h) continue;
    TileAnim an; an.layer = layer; an.frameTime = std::max(1, (int)std::lround(1000.0 / std::max(0.5f, fps)));
    for (int f = 0; f < n && (f + 1) * S <= w; f++) {
      int t = ts.count++;
      ts.rgba.resize((size_t)ts.count * tpx * 4);
      for (int y = 0; y < S; y++) std::memcpy(ts.tile(t) + (size_t)y * S * 4, img.data() + ((size_t)(row * S + y) * w + f * S) * 4, (size_t)S * 4);
      // the frames keep the relief and glow of the tile they animate
      if (ts.height.size() >= (size_t)(layer + 1) * tpx) { ts.height.resize((size_t)ts.count * tpx); std::memcpy(&ts.height[(size_t)t * tpx], &ts.height[(size_t)layer * tpx], tpx); }
      if (ts.emissive.size() >= (size_t)(layer + 1) * tpx) { ts.emissive.resize((size_t)ts.count * tpx); std::memcpy(&ts.emissive[(size_t)t * tpx], &ts.emissive[(size_t)layer * tpx], tpx); }
      an.frames.push_back(t);
    }
    if (an.frames.size() > 1) ts.anims.push_back(std::move(an));
  }
  // liquids with no frames of their own (the Sulfur Pools' acid): made from their still picture, a slow seamless ripple
  // (each texel sways along sines whose period is the tile, so the frames still tile) and a gentle pulse of brightness
  for (const char* key : {"acid"}) {
    auto it = ts.index.find(key);
    if (it == ts.index.end()) continue;
    const int layer = it->second;
    if (std::any_of(ts.anims.begin(), ts.anims.end(), [&](const TileAnim& a) { return a.layer == layer; })) continue;
    std::vector<u8> base(ts.tile(layer), ts.tile(layer) + tpx * 4);
    constexpr int N = 32; constexpr double TAU = 6.283185307179586;
    TileAnim an; an.layer = layer; an.frameTime = 100;
    for (int f = 0; f < N; f++) {
      const int t = ts.count++;
      ts.rgba.resize((size_t)ts.count * tpx * 4);
      const double ph = TAU * f / N;
      u8* out = ts.tile(t);
      for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        const int sx = ((x + (int)std::lround(1.6 * std::sin(TAU * y / S + ph)) + f * S / N) % S + S) % S;
        const int sy = ((y + (int)std::lround(1.6 * std::sin(TAU * x / S + ph * 2))) % S + S) % S;
        const u8* p = &base[((size_t)sy * S + sx) * 4];
        const double k = 1.0 + 0.08 * std::sin(TAU * (x + y) / S + ph);
        for (int c = 0; c < 3; c++) out[((size_t)y * S + x) * 4 + c] = (u8)std::clamp(p[c] * k, 0.0, 255.0);
        out[((size_t)y * S + x) * 4 + 3] = p[3];
      }
      if (ts.height.size() >= (size_t)(layer + 1) * tpx) { ts.height.resize((size_t)ts.count * tpx); std::memcpy(&ts.height[(size_t)t * tpx], &ts.height[(size_t)layer * tpx], tpx); }
      if (ts.emissive.size() >= (size_t)(layer + 1) * tpx) { ts.emissive.resize((size_t)ts.count * tpx); std::memcpy(&ts.emissive[(size_t)t * tpx], &ts.emissive[(size_t)layer * tpx], tpx); }
      an.frames.push_back(t);
    }
    ts.anims.push_back(std::move(an));
  }
  DQ_LOG("tiles: %zu built-in animations", ts.anims.size());
}

namespace {
// three copies used in turn, so the one being written is never the one the GPU is still reading
constexpr int MAP_N = 64, MAP_RING = 3;
GLuint gMap[MAP_RING] = {0, 0, 0}; int gCur = 0;
std::vector<u16> gData;
void identity() { gData.resize(MAP_N * MAP_N); for (int i = 0; i < MAP_N * MAP_N; i++) gData[i] = (u16)i; }
void uploadMap() {
  gCur = (gCur + 1) % MAP_RING;
  glBindTexture(GL_TEXTURE_2D, gMap[gCur]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAP_N, MAP_N, GL_RED_INTEGER, GL_UNSIGNED_SHORT, gData.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
}
}

GLuint tileAnimMap() {
  if (!gMap[0]) {
    identity();
    glGenTextures(MAP_RING, gMap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    for (GLuint t : gMap) {
      glBindTexture(GL_TEXTURE_2D, t);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, MAP_N, MAP_N, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, gData.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  return gMap[gCur];
}

void TileAnimator::update(const TileSet& ts, const TileTextures&, double t) {
  tileAnimMap();
  bool changed = false;
  if (lastData != ts.rgba.data() || cur.size() != ts.anims.size()) { cur.assign(ts.anims.size(), -1); lastData = ts.rgba.data(); identity(); changed = true; }
  for (size_t i = 0; i < ts.anims.size(); i++) {
    const TileAnim& a = ts.anims[i];
    if (a.frames.empty() || a.layer < 0 || a.layer >= std::min(ts.count, MAP_N * MAP_N)) continue;
    int f = (int)((i64)(t * 1000.0 / std::max(1, a.frameTime)) % (i64)a.frames.size());
    if (f == cur[i]) continue;
    cur[i] = f;
    int src = a.frames[f];
    if (src < 0 || src >= ts.count) continue;   // (a frame may be any layer: the map's values are 16-bit; the animated tile itself is under 4096)
    if (gData[a.layer] != (u16)src) { gData[a.layer] = (u16)src; changed = true; }
  }
  if (changed) uploadMap();
}

} // namespace dq
