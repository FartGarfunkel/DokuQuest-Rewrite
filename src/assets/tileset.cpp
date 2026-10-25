#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include "assets/tileset.h"
#include <cstring>

namespace dq {

bool loadPngMem(const u8* data, size_t n, int& w, int& h, std::vector<u8>& rgba) {
  int ch; u8* px = stbi_load_from_memory(data, (int)n, &w, &h, &ch, 4);
  if (!px) return false;
  rgba.assign(px, px + (size_t)w * h * 4); stbi_image_free(px);
  return true;
}
bool loadPng(const fs::path& p, int& w, int& h, std::vector<u8>& rgba) {
  auto b = readBinary(p);
  return b && loadPngMem(b->data(), b->size(), w, h, rgba);
}
bool savePng(const fs::path& p, int w, int h, const u8* rgba) {
  std::error_code ec; if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  return stbi_write_png(p.string().c_str(), w, h, 4, rgba, w * 4) != 0;
}

u32 TileSet::avgColor(int i) const {
  if (i < 0 || i >= count) return 0xFF808080;
  const u8* t = tile(i); u64 r = 0, g = 0, b = 0, n = 0;
  for (int k = 0; k < size * size; k++) if (t[k * 4 + 3] > 127) { r += t[k * 4]; g += t[k * 4 + 1]; b += t[k * 4 + 2]; n++; }
  if (!n) return 0;
  return 0xFF000000u | (u32)(b / n) << 16 | (u32)(g / n) << 8 | (u32)(r / n);
}

bool loadBuiltinTiles(TileSet& ts) {
  auto tj = readJson(dataDir() / "tiles.json");
  int w, h; std::vector<u8> img;
  if (!tj || !loadPng(dataDir() / "tiles.png", w, h, img)) { DQ_ERR("data/tiles.png missing"); return false; }
  ts.size = (*tj)["tile"]; int per = (*tj)["perRow"]; ts.count = (*tj)["count"];
  for (auto& [k, v] : (*tj)["names"].items()) ts.index[k] = v;
  int S = ts.size;
  ts.rgba.assign((size_t)ts.count * S * S * 4, 0);
  for (int t = 0; t < ts.count; t++) {
    int tx = (t % per) * S, ty = (t / per) * S;
    for (int y = 0; y < S; y++) std::memcpy(ts.tile(t) + (size_t)y * S * 4, img.data() + ((size_t)(ty + y) * w + tx) * 4, (size_t)S * 4);
  }
  ts.emissive.assign((size_t)ts.count * S * S, 0);
  ts.res.assign(ts.count, (u8)S);
  if (tj->contains("res") && (*tj)["res"].is_array()) for (int t = 0; t < ts.count && t < (int)(*tj)["res"].size(); t++) ts.res[t] = (u8)(*tj)["res"][t].get<int>();
  deriveMicroVoxelHeights(ts);
  return true;
}

int appendTile(TileSet& ts, const u8* px) {
  size_t S2 = (size_t)ts.size * ts.size;
  ts.rgba.insert(ts.rgba.end(), px, px + S2 * 4);
  ts.height.resize(ts.height.size() + S2, 0);
  ts.emissive.resize(ts.emissive.size() + S2, 0);
  ts.res.push_back((u8)ts.size);
  return ts.count++;
}

// Relief from colour: darker-than-local-average micro-voxels (mortar, cracks, bark grooves) sink,
// bright edges stand proud. The assets module may refine this per material.
void deriveMicroVoxelHeights(TileSet& ts) {
  int S = ts.size;
  ts.height.assign((size_t)ts.count * S * S, 0);
  std::vector<float> lum(S * S);
  for (int t = 0; t < ts.count; t++) {
    const u8* p = ts.tile(t);
    float avg = 0; int n = 0;
    for (int i = 0; i < S * S; i++) { lum[i] = (0.299f * p[i * 4] + 0.587f * p[i * 4 + 1] + 0.114f * p[i * 4 + 2]) / 255.f; if (p[i * 4 + 3] > 127) { avg += lum[i]; n++; } }
    if (!n) continue;
    avg /= n;
    u8* hgt = ts.height.data() + (size_t)t * S * S;
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      // local average (5x5, wrapping) so large dark areas don't all sink
      float la = 0; for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) la += lum[((y + dy + S) % S) * S + (x + dx + S) % S];
      la /= 25.f;
      float d = (la - lum[y * S + x]) * 3.5f + (avg - lum[y * S + x]) * 1.0f;   // darker -> deeper
      hgt[y * S + x] = (u8)clampv(d * 255.f, 0.f, 255.f);
    }
  }
}

} // namespace dq
