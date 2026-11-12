// render/painting_layer.cpp — paintings (BTA's painting, our own pictures): a framed canvas on the wall whose picture is
// painted here from its number (block entity "painting", n): landscapes, seascapes, peaks, still lifes and colour
// fields, each in one of six palettes, in 32 x 32 pixel art. Use one to turn to the next picture (sim/build.cpp).
#include "render/tile_box.h"
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "render/content_draw.h"
#include "client/session.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

static constexpr int PAINTINGS = 30, PX = 32;

namespace {
struct Pal { u32 sky0, sky1, far, near, ground, accent, sun; };
const Pal PALS[6] = {
  {0x7fb8e8, 0xd8ecf8, 0x6f8fa8, 0x4f7a3a, 0x3a5a28, 0xd84a3a, 0xfff2a8},   // a summer day
  {0x3a2a5a, 0xf08a4a, 0x5a3a5a, 0x3a2a3a, 0x2a1e28, 0xf0c060, 0xffd070},   // dusk
  {0x0a1430, 0x2a3a6a, 0x1e2a48, 0x14203a, 0x0e1628, 0xe8e0a0, 0xf4f4ff},   // night
  {0xa8c8d8, 0xeef4f6, 0x9aaab8, 0xc8d4dc, 0xe8eef2, 0x5a7aa8, 0xffffff},   // winter
  {0xe8c070, 0xf8e8b8, 0xc89058, 0xa86838, 0x8a5028, 0x3a7a8a, 0xfff0c0},   // desert
  {0x8ab8a0, 0xd8e8c8, 0x5a8a6a, 0x3a6a4a, 0x2a4a34, 0xe8a8c8, 0xf8f0d8},   // spring green
};
u32 mix(u32 a, u32 b, float t) {
  auto ch = [&](int s) { return (u32)std::lround(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t) << s; };
  return ch(16) | ch(8) | ch(0);
}
u32 shade(u32 c, float k) { return mix(0, c, std::min(1.f, k)) | (k > 1 ? mix(c, 0xffffff, k - 1) : 0); }

std::vector<u8> paint(int n) {
  std::vector<u32> im(PX * PX, 0);
  const Pal& P = PALS[n % 6];
  const int style = (n / 6) % 5;
  u32 seed = (u32)n * 2654435761u + 77;
  auto rnd = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed % 10000) / 10000.f; };
  auto put = [&](int x, int y, u32 c) { if (x >= 0 && y >= 0 && x < PX && y < PX) im[y * PX + x] = c; };
  auto disc = [&](float cx, float cy, float r, u32 c) { for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) put(x, y, c); };
  // the sky, top to bottom
  for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, mix(P.sky0, P.sky1, y / (PX * 0.7f)));
  const float ph = rnd() * 6.28f, sunX = 6 + rnd() * 20, sunY = 5 + rnd() * 6;
  if (style == 0) {   // a landscape: hills behind hills, a lone tree
    disc(sunX, sunY, 3, P.sun);
    for (int x = 0; x < PX; x++) {
      int h1 = (int)(18 + 3 * std::sin(x * 0.21f + ph)), h2 = (int)(23 + 2 * std::sin(x * 0.33f + ph * 2) + std::sin(x * 0.9f));
      for (int y = h1; y < PX; y++) put(x, y, P.far);
      for (int y = h2; y < PX; y++) put(x, y, y > 28 ? P.ground : P.near);
    }
    int tx = 8 + (int)(rnd() * 16), ty = (int)(23 + 2 * std::sin(tx * 0.33f + ph * 2) + std::sin(tx * 0.9f));
    for (int y = ty - 5; y < ty; y++) put(tx, y, 0x3a2a1a);
    disc(tx + 0.5f, ty - 7, 2.6f, shade(P.near, 0.8f));
  } else if (style == 1) {   // a seascape: the horizon, waves, a sail
    disc(sunX, sunY + 4, 3.5f, P.sun);
    for (int y = 19; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, mix(P.far, shade(P.far, 0.55f), (y - 19) / 13.f));
    for (int y = 21; y < PX; y += 3) for (int x = (y * 7) % 5; x < PX; x += 5) { put(x, y, shade(P.sky1, 0.95f)); put(x + 1, y, shade(P.sky1, 0.95f)); }
    const int bx = 6 + (int)(rnd() * 18);
    for (int k = 0; k < 6; k++) for (int x = bx; x <= bx + k / 2; x++) put(x, 12 + k, 0xf4f0e4);
    for (int x = bx - 3; x <= bx + 4; x++) put(x, 18, 0x5a3a22);
  } else if (style == 2) {   // peaks under snow
    disc(sunX, sunY - 1, 2.5f, P.sun);
    for (int m = 0; m < 3; m++) {
      const float cx = 4 + m * 11 + rnd() * 6, top = 6 + rnd() * 8;
      for (int y = (int)top; y < PX; y++) {
        const float w = (y - top) * (0.7f + 0.2f * m);
        for (int x = (int)(cx - w); x <= (int)(cx + w); x++) put(x, y, y < top + 4 ? 0xf4f8fc : x < cx ? shade(P.far, 1.1f) : P.far);
      }
    }
    for (int y = 27; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, P.near);
  } else if (style == 3) {   // a still life: a vase of flowers on a table
    for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, mix(shade(P.far, 0.6f), shade(P.far, 0.4f), y / 32.f));
    for (int y = 24; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, y == 24 ? 0x9a6a3a : 0x6a4424);
    for (int y = 15; y < 24; y++) { const int w = y < 17 ? 1 : y < 22 ? 3 : 2; for (int x = 16 - w; x <= 16 + w; x++) put(x, y, x < 16 ? shade(P.accent, 1.15f) : P.accent); }
    for (int k = 0; k < 5; k++) {
      const float fx = 11 + k * 2.5f, fy = 7 + rnd() * 5;
      for (int y = (int)fy; y < 15; y++) put((int)(fx + (16 - fx) * (y - fy) / (15 - fy)), y, 0x3a6a2a);
      disc(fx, fy, 1.5f, k & 1 ? P.sun : shade(P.accent, 1.4f));
    }
  } else {   // colour fields
    for (int y = 0; y < PX; y++) for (int x = 0; x < PX; x++) put(x, y, shade(P.far, 0.7f));
    const u32 cs[3] = {P.accent, P.sun, P.near};
    int y0 = 3;
    for (int b = 0; b < 3; b++) {
      const int h = b == 1 ? 6 + (int)(rnd() * 4) : 7 + (int)(rnd() * 4);
      for (int y = y0; y < std::min(PX - 3, y0 + h); y++) for (int x = 3; x < PX - 3; x++) put(x, y, shade(cs[b], 0.9f + 0.1f * std::sin(x * 1.7f + y * 2.3f)));
      y0 += h + 1;
    }
  }
  // a little texture of brush strokes
  for (int i = 0; i < PX * PX; i++) if (((i * 2654435761u) >> 7) % 9 == 0) im[i] = shade(im[i], 0.93f);
  std::vector<u8> rgba(PX * PX * 4);
  for (int i = 0; i < PX * PX; i++) { rgba[i * 4] = (im[i] >> 16) & 255; rgba[i * 4 + 1] = (im[i] >> 8) & 255; rgba[i * 4 + 2] = im[i] & 255; rgba[i * 4 + 3] = 255; }
  return rgba;
}

GLuint paintingTex(int n) {
  static std::unordered_map<int, GLuint> cache;
  n = ((n % PAINTINGS) + PAINTINGS) % PAINTINGS;
  auto it = cache.find(n);
  if (it != cache.end()) return it->second;
  std::vector<u8> px = paint(n);
  return cache[n] = makeTexRGBA(PX, PX, px.data(), true, false);
}
} // namespace

class PaintingLayer : public RenderLayer {
public:
  const char* name() const override { return "paintings"; }
  int order() const override { return 112; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    ContentDraw& cd = contentDraw();
    bool begun = false;
    forBlockEntities(w, f, 48.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "painting") return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.f)) return;
      if (!reg().block(w.getBlock(x, y, z)).has("painting")) return;
      if (!begun) { if (!cd.begin(f, f.cam.viewProj)) return; begun = true; }
      static const float TURN[4] = {0, -PI / 2, PI, PI / 2};
      const int fm = w.getMeta(x, y, z) & 3;
      const u8 l = w.getLight(x, y, z);
      mat4 m = glm::translate(mat4(1), rel + vec3(0.5f));
      m = glm::rotate(m, TURN[fm], vec3(0, 1, 0));
      m = glm::translate(m, vec3(0, 0, -0.5f + 0.07f));
      const mat4 m0 = m;
      m = glm::scale(m, vec3(0.8125f, 0.8125f, 1));
      cd.picture(paintingTex(e.value("n", 0)), m, vec4(0, 0, 1, 1), vec4(1), vec2((l >> 4) / 15.f, (l & 15) / 15.f));
      // a gold or an iron trim round the canvas (BTA): four thin bars of the metal block just proud of the frame
      const std::string trim = e.value("trim", std::string());
      if (!trim.empty()) {
        const BlockId mb = reg().blockId(trim == "gold" ? "gold_block" : "iron_block");
        const GLuint tx = mb ? tileTexture(s.app, reg().block(mb).faces[0]) : 0;
        if (tx) {
          const float h = 0.40625f + 0.05f, t = 0.05f, z0 = -0.035f, z1 = 0.035f;
          const vec2 L((l >> 4) / 15.f, (l & 15) / 15.f);
          drawTileBox(cd, m0, vec3(-h, h - t, z0), vec3(h, h, z1), tx, tx, tx, L, vec3(1), false);
          drawTileBox(cd, m0, vec3(-h, -h, z0), vec3(h, -h + t, z1), tx, tx, tx, L, vec3(1), false);
          drawTileBox(cd, m0, vec3(-h, -h + t, z0), vec3(-h + t, h - t, z1), tx, tx, tx, L, vec3(1), false);
          drawTileBox(cd, m0, vec3(h - t, -h + t, z0), vec3(h, h - t, z1), tx, tx, tx, L, vec3(1), false);
        }
      }
    });
    if (begun) cd.end();
  }
};

void registerPaintingLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<PaintingLayer>(); }); }

} // namespace dq
