// render/tile_box.cpp — see tile_box.h.
#include "render/tile_box.h"
#include "client/app.h"
#include <unordered_map>

namespace dq {

GLuint tileTexture(App& a, int layer) {
  static std::unordered_map<int, GLuint> cache;
  static const TileSet* of = nullptr; static int count = -1; static u8 first = 0;
  const u8 f0 = a.tiles.count ? a.tiles.tile(0)[0] : 0;
  if (of != &a.tiles || count != a.tiles.count || first != f0) { for (auto& [k, t] : cache) glDeleteTextures(1, &t); cache.clear(); of = &a.tiles; count = a.tiles.count; first = f0; }
  auto it = cache.find(layer);
  if (it != cache.end()) return it->second;
  if (layer < 0 || layer >= a.tiles.count) return 0;
  const int N = a.tiles.size;
  std::vector<u8> px(a.tiles.tile(layer), a.tiles.tile(layer) + N * N * 4);
  for (size_t i = 3; i < px.size(); i += 4) if (px[i] >= 250) px[i] = 255;
  return cache[layer] = makeTexRGBA(N, N, px.data(), true, false);
}
GLuint tileTexture(App& a, const std::string& name) { auto it = a.tiles.index.find(name); return it == a.tiles.index.end() ? 0 : tileTexture(a, it->second); }

void drawTileBox(ContentDraw& cd, const mat4& m, vec3 a, vec3 b, GLuint side, GLuint top, GLuint bot, vec2 light, vec3 color, bool sixteenths, bool lit) {
  if (sixteenths) { a /= 16.f; b /= 16.f; }
  const vec3 c = (a + b) * 0.5f, s = b - a;
  // uv: where the box sits in its block (like the mesher), wrapped for boxes bigger than a block
  auto fr = [](float v) { return v - std::floor(v); };
  auto uvr = [&](float u0, float u1, float v0, float v1) {
    if (u1 - u0 > 1.001f || v1 - v0 > 1.001f) return vec4(0, 0, 1, 1);
    float a0 = fr(u0), b0 = a0 + (u1 - u0), c0 = fr(v0), d0 = c0 + (v1 - v0);
    return vec4(a0, 1 - d0, b0, 1 - c0);
  };
  auto pic = [&](GLuint t, const mat4& q, vec4 uv, float shade) { cd.picture(t, m * q, uv, vec4(color * shade, 1), light, lit); };
  for (int k = 0; k < 2; k++) {   // facing z
    mat4 q = glm::translate(mat4(1), vec3(c.x, c.y, k ? b.z : a.z)); q = glm::scale(q, vec3(s.x, s.y, 1));
    pic(side, q, uvr(a.x, b.x, a.y, b.y), 0.9f);
  }
  for (int k = 0; k < 2; k++) {   // facing x
    mat4 q = glm::translate(mat4(1), vec3(k ? b.x : a.x, c.y, c.z)); q = glm::rotate(q, PI / 2, vec3(0, 1, 0)); q = glm::scale(q, vec3(s.z, s.y, 1));
    pic(side, q, uvr(a.z, b.z, a.y, b.y), 0.75f);
  }
  for (int k = 0; k < 2; k++) {   // facing y
    mat4 q = glm::translate(mat4(1), vec3(c.x, k ? b.y : a.y, c.z)); q = glm::rotate(q, PI / 2, vec3(1, 0, 0)); q = glm::scale(q, vec3(s.x, s.z, 1));
    vec4 uv = uvr(a.x, b.x, a.z, b.z);
    pic(k ? top : bot, q, uv, k ? 1.f : 0.6f);
  }
}

} // namespace dq
