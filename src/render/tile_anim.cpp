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
  DQ_LOG("tiles: %zu built-in animations", ts.anims.size());
}

void TileAnimator::update(const TileSet& ts, const TileTextures& tex, double t) {
  if (lastData != ts.rgba.data() || cur.size() != ts.anims.size()) { cur.assign(ts.anims.size(), -1); lastData = ts.rgba.data(); }
  const int S = ts.size;
  std::vector<u8> mip[5];
  for (size_t i = 0; i < ts.anims.size(); i++) {
    const TileAnim& a = ts.anims[i];
    if (a.frames.empty() || a.layer < 0 || a.layer >= ts.count) continue;
    int f = (int)((i64)(t * 1000.0 / std::max(1, a.frameTime)) % (i64)a.frames.size());
    if (f == cur[i]) continue;
    cur[i] = f;
    int src = a.frames[f];
    if (src < 0 || src >= ts.count) continue;
    int page = a.layer / tex.page, z = a.layer % tex.page;
    if (page > 1 || !tex.color[page]) continue;
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex.color[page]);
    // level 0, then a box-filtered mip chain (the array has 4 extra levels)
    mip[0].assign(ts.tile(src), ts.tile(src) + (size_t)S * S * 4);
    int sz = S;
    for (int lv = 0; lv <= 4 && sz >= 1; lv++) {
      if (lv > 0) {
        int ps = sz * 2; mip[lv].assign((size_t)sz * sz * 4, 0);
        for (int y = 0; y < sz; y++) for (int x = 0; x < sz; x++) for (int c = 0; c < 4; c++) {
          const std::vector<u8>& p = mip[lv - 1];
          int s = p[((2 * y) * ps + 2 * x) * 4 + c] + p[((2 * y) * ps + 2 * x + 1) * 4 + c] + p[((2 * y + 1) * ps + 2 * x) * 4 + c] + p[((2 * y + 1) * ps + 2 * x + 1) * 4 + c];
          mip[lv][(y * sz + x) * 4 + c] = (u8)((s + 2) / 4);
        }
      }
      glTexSubImage3D(GL_TEXTURE_2D_ARRAY, lv, 0, 0, z, sz, sz, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip[lv].data());
      sz /= 2;
    }
  }
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

} // namespace dq
