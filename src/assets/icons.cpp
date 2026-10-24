#include "assets/icons.h"
#include "assets/tileset.h"
#include "core/registry.h"
#include "render/gl.h"
#include <unordered_map>
#include <cstring>

namespace dq {

ItemIcons& itemIcons() { static ItemIcons i; return i; }

// Isometric block icon like the original drawIsoBlock (render/textures.js): top, left and right faces.
static void drawIsoBlock(u8* dst, int stride, const TileSet& ts, const std::array<u16, 6>& faces) {
  const int S = ts.size;
  auto px = [&](int tile, int u, int v) -> const u8* { tile = clampv(tile, 0, ts.count - 1); return ts.tile(tile) + ((size_t)clampv(v, 0, S - 1) * S + clampv(u, 0, S - 1)) * 4; };
  auto put = [&](int x, int y, const u8* c, float shade) {
    if (x < 0 || y < 0 || x >= S || y >= S || c[3] < 128) return;
    u8* d = dst + (size_t)y * stride + x * 4;
    d[0] = (u8)(c[0] * shade); d[1] = (u8)(c[1] * shade); d[2] = (u8)(c[2] * shade); d[3] = 255;
  };
  // project: top rhombus (upper half), left/right parallelograms (lower)
  float h = S / 4.f;
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    float fx = x + 0.5f, fy = y + 0.5f, cx = S / 2.f;
    // top face: rhombus centred at (cx, h)
    float a = (fx - cx) / (S / 2.f), b = (fy - h) / h;
    float u = (a + b) * 0.5f + 0.5f, v = (b - a) * 0.5f + 0.5f;
    if (u >= 0 && u < 1 && v >= 0 && v < 1) { put(x, y, px(faces[2], (int)(u * S), (int)(v * S)), 1.0f); continue; }
    // left face (-x side visible as +z? use faces[4] front) and right face
    if (fx < cx) {
      float t = fx / cx; float top = h + t * h; float bot = top + S / 2.f;
      if (fy >= top && fy < bot) put(x, y, px(faces[4], (int)(t * S), (int)((fy - top) / (S / 2.f) * S)), 0.78f);
    } else {
      float t = (fx - cx) / cx; float top = 2 * h - t * h; float bot = top + S / 2.f;
      if (fy >= top && fy < bot) put(x, y, px(faces[0], (int)(t * S), (int)((fy - top) / (S / 2.f) * S)), 0.6f);
    }
  }
}

void ItemIcons::build(const TileSet& ts) {
  release();
  const Registry& R = reg();
  size = ts.size;
  int n = (int)R.items.size() + ts.count;
  int rows = (n + cols - 1) / cols;
  atlasW = cols * size; atlasH = rows * size;
  std::vector<u8> img((size_t)atlasW * atlasH * 4, 0);
  auto cell = [&](int i) { return img.data() + ((size_t)(i / cols) * size * atlasW + (i % cols) * size) * 4; };
  int i = 0;
  for (auto& it : R.items) {
    u8* d = cell(i);
    bool isBlockIcon = startsWith(it.icon, "block:") && !it.block.empty();
    if (isBlockIcon) {
      const BlockDef& b = R.block(R.blockId(it.block));
      if (b.render == Render::Cross || b.render == Render::Torch) isBlockIcon = false;
    }
    if (isBlockIcon) drawIsoBlock(d, atlasW * 4, ts, R.block(R.blockId(it.block)).faces);
    else {
      int t = R.tile(startsWith(it.icon, "block:") ? R.block(R.blockId(it.block)).tex : it.icon);
      if (t < 0 && !it.block.empty()) t = R.block(R.blockId(it.block)).faces[0];
      if (t >= 0 && t < ts.count) for (int y = 0; y < size; y++) std::memcpy(d + (size_t)y * atlasW * 4, ts.tile(t) + (size_t)y * size * 4, (size_t)size * 4);
    }
    slot[it.key] = i++;
  }
  tileBase = i; tileCount = ts.count;
  for (int t = 0; t < ts.count; t++, i++) { u8* d = cell(i); for (int y = 0; y < size; y++) std::memcpy(d + (size_t)y * atlasW * 4, ts.tile(t) + (size_t)y * size * 4, (size_t)size * 4); }
  tex = makeTexture2D(atlasW, atlasH, img.data(), true, false, false);
}

static IconRef refFor(unsigned tex, int i, int cols, int size, int w, int h) {
  IconRef r; r.tex = (void*)(uintptr_t)tex;
  float x = (float)(i % cols) * size, y = (float)(i / cols) * size;
  r.uv0 = vec2(x / w, y / h); r.uv1 = vec2((x + size) / w, (y + size) / h);
  return r;
}
IconRef ItemIcons::get(const std::string& key) const {
  auto it = slot.find(key);
  if (it == slot.end() || !tex) return {};
  return refFor(tex, it->second, cols, size, atlasW, atlasH);
}
IconRef ItemIcons::tile(int t) const {
  if (!tex || t < 0 || t >= tileCount) return {};
  return refFor(tex, tileBase + t, cols, size, atlasW, atlasH);
}
void ItemIcons::release() { if (tex) { glDeleteTextures(1, (GLuint*)&tex); tex = 0; } slot.clear(); }

} // namespace dq
