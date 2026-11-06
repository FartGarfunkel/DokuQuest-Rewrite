// render/sign_layer.cpp — signs (render NONE blocks, like item piles): a planks board on a post, or flat on a wall, and
// the four lines written on it (ui/sign_screen.cpp) painted with the game's pixel font. Glow ink (sim/build.cpp) lights
// the writing up, unlit and ringed like Minecraft's glowing text; a dye colours it.
// Standing sign meta = rotation 0..15 (the placer's yaw); wall sign meta = 16 | direction into the wall (0 N 1 E 2 S 3 W).
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "render/tile_box.h"
#include "render/creature_draw.h"
#include "assets/voxel.h"
#include "world/shapes.h"
#include "client/app.h"
#include "ui/theme.h"
#include "imgui.h"
#include <map>
#include <array>
#include <cmath>
#include <unordered_map>

namespace dq {
namespace {

const std::map<std::string, u32>& dyeColours() {
  static const std::map<std::string, u32> M = {
    {"white", 0xf0f0f0}, {"orange", 0xf08a2a}, {"magenta", 0xd04ec8}, {"light_blue", 0x6ab4ea}, {"yellow", 0xf2d840},
    {"lime", 0x82d82e}, {"pink", 0xf2a2c0}, {"gray", 0x5a5a5a}, {"light_gray", 0xa8a8a8}, {"cyan", 0x2aa0a8},
    {"purple", 0x8a3ec8}, {"blue", 0x3a4ec8}, {"brown", 0x7a4e2a}, {"green", 0x4e7a22}, {"red", 0xc83a32}, {"black", 0x1c140c}};
  return M;
}

struct SignTex { GLuint tex = 0; std::string key; u64 frame = 0; };

// the four lines in the UI's pixel font, 128 x 64, centred; glowing text gets a dark ring and brighter letters
std::vector<u8> paintSign(const std::array<std::string, 4>& L, u32 rgb, bool glow) {
  const int W = 128, H = 64, LH = 16;
  std::vector<u8> px((size_t)W * H * 4, 0);
  ImFont* f = ui::font(ui::F_BIG);
  ImGuiIO& io = ImGui::GetIO();
  unsigned char* atlas = nullptr; int aw = 0, ah = 0;
  io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
  if (!f || !atlas) return px;
  float k = (LH + 3) / f->FontSize;   // the pixel font fills its line like Minecraft sign text
  std::vector<float> cov((size_t)W * H, 0);
  for (int li = 0; li < 4; li++) {
    const std::string& s = L[li];
    float width = 0;
    for (unsigned char c : s) if (const ImFontGlyph* g = f->FindGlyph((ImWchar)c)) width += g->AdvanceX * k;
    float x = std::round((W - width) * 0.5f), y0 = (float)(li * LH) - 2;
    for (unsigned char c : s) {
      const ImFontGlyph* g = f->FindGlyph((ImWchar)c);
      if (!g) continue;
      float gx0 = x + g->X0 * k, gy0 = y0 + g->Y0 * k, gx1 = x + g->X1 * k, gy1 = y0 + g->Y1 * k;
      for (int ty = (int)std::floor(gy0); ty < (int)std::ceil(gy1); ty++) for (int tx = (int)std::floor(gx0); tx < (int)std::ceil(gx1); tx++) {
        if (tx < 0 || ty < 0 || tx >= W || ty >= H) continue;
        float u = g->U0 + ((tx + 0.5f - gx0) / std::max(0.01f, gx1 - gx0)) * (g->U1 - g->U0);
        float v = g->V0 + ((ty + 0.5f - gy0) / std::max(0.01f, gy1 - gy0)) * (g->V1 - g->V0);
        int ax = clampv((int)(u * aw), 0, aw - 1), ay = clampv((int)(v * ah), 0, ah - 1);
        float a = atlas[((size_t)ay * aw + ax) * 4 + 3] / 255.f;
        float& cv = cov[(size_t)ty * W + tx]; cv = std::max(cv, a >= 0.5f ? 1.f : 0.f);
      }
      x += g->AdvanceX * k;
    }
  }
  u8 r = rgb >> 16 & 255, gg = rgb >> 8 & 255, b = rgb & 255;
  if (glow) { r = (u8)std::min(255, r + 90); gg = (u8)std::min(255, gg + 90); b = (u8)std::min(255, b + 90); }
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    u8* d = &px[((size_t)y * W + x) * 4];
    if (cov[(size_t)y * W + x] > 0) { d[0] = r; d[1] = gg; d[2] = b; d[3] = 255; continue; }
    if (!glow) continue;
    bool ring = false;
    for (int dy = -1; dy <= 1 && !ring; dy++) for (int dx = -1; dx <= 1; dx++) {
      int xx = x + dx, yy = y + dy;
      if (xx >= 0 && yy >= 0 && xx < W && yy < H && cov[(size_t)yy * W + xx] > 0) { ring = true; break; }
    }
    if (ring) { d[0] = (u8)(rgb >> 16 & 255) / 3; d[1] = (u8)(rgb >> 8 & 255) / 3; d[2] = (u8)(rgb & 255) / 3; d[3] = 255; }
  }
  return px;
}

// The board and the post are one voxel per texel of the plank and the log. The writing stays a picture on top.
struct SignWoodKey {
  u16 side = 0, top = 0, post = 0;
  u8 wall = 0;
  u16 S = 32;
  bool operator==(const SignWoodKey& o) const { return side == o.side && top == o.top && post == o.post && wall == o.wall && S == o.S; }
};
struct SignWoodHash {
  size_t operator()(const SignWoodKey& k) const {
    return (size_t)k.side * 1315423911u ^ ((size_t)k.top << 16) ^ ((size_t)k.post << 1) ^ ((size_t)k.wall << 24) ^ k.S;
  }
};
static const VoxelMeshGL* signWoodMesh(const TileSet& ts, const SignWoodKey& key) {
  struct Cache {
    const u8* px = nullptr;
    int count = -1, size = -1;
    std::unordered_map<SignWoodKey, std::unique_ptr<VoxelMeshGL>, SignWoodHash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = key.S;
  const int src = ts.size > 0 ? ts.size : S;
  VoxelGrid g(ivec3(S, S, S));
  auto span = [&](float a, float b, int& lo, int& hi) {
    lo = clampv((int)std::floor(a * S / 16.f), 0, S - 1);
    hi = clampv((int)std::ceil(b * S / 16.f) - 1, lo, S - 1);
  };
  auto texel = [&](int tile, int x, int y, int z, int f, float mul) -> u32 {
    if (tile < 0 || tile >= ts.count) return 0;
    float fx = (x + 0.5f) / S, fy = (y + 0.5f) / S, fz = (z + 0.5f) / S;
    float u, v;
    switch (f) {   // the same flips as faceUV in mesher.cpp
      case 0: u = 1.f - fz; v = 1.f - fy; break;
      case 1: u = fz; v = 1.f - fy; break;
      case 2: u = fx; v = fz; break;
      case 3: u = 1.f - fx; v = fz; break;
      case 4: u = fx; v = 1.f - fy; break;
      default: u = 1.f - fx; v = 1.f - fy; break;
    }
    int sx = clampv((int)(u * src), 0, src - 1), sy = clampv((int)(v * src), 0, src - 1);
    const u8* p = ts.tile(tile) + ((size_t)sy * src + sx) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA((int)std::lround(p[0] * mul), (int)std::lround(p[1] * mul), (int)std::lround(p[2] * mul));
  };
  auto box = [&](float x0, float y0, float z0, float x1, float y1, float z1, int side, int top, float mul) {
    int xa, xb, ya, yb, za, zb;
    span(x0, x1, xa, xb); span(y0, y1, ya, yb); span(z0, z1, za, zb);
    u32 fill = voxRGBA(1, 1, 1);
    for (int z = za; z <= zb; z++) for (int y = ya; y <= yb; y++) for (int x = xa; x <= xb; x++) g.set(x, y, z, fill);
    // Caps first. The broad faces are written last and keep the rim, so the plank shows on the board.
    static const int order[6] = {2, 3, 0, 1, 4, 5};
    for (int oi = 0; oi < 6; oi++) {
      int f = order[oi];
      int tile = (f == 2 || f == 3) ? top : side;
      for (int vv = 0; vv < S; vv++) for (int u = 0; u < S; u++) {
        int x = xa, y = ya, z = za;
        if (f == 0) { x = xb; y = ya + vv; z = za + u; if (y > yb || z > zb) continue; }
        else if (f == 1) { x = xa; y = ya + vv; z = za + u; if (y > yb || z > zb) continue; }
        else if (f == 2) { y = yb; x = xa + u; z = za + vv; if (x > xb || z > zb) continue; }
        else if (f == 3) { y = ya; x = xa + u; z = za + vv; if (x > xb || z > zb) continue; }
        else if (f == 4) { z = zb; x = xa + u; y = ya + vv; if (x > xb || y > yb) continue; }
        else { z = za; x = xa + u; y = ya + vv; if (x > xb || y > yb) continue; }
        if (u32 c = texel(tile, x, y, z, f, mul)) g.set(x, y, z, c);
      }
    }
  };
  float by0 = key.wall ? 4.5f : 7.f, by1 = by0 + 8.f, bz0 = key.wall ? 0.f : 7.f, bz1 = bz0 + 1.6f;
  if (!key.wall) box(7.2f, 0, 7.2f, 8.8f, by0, 8.8f, key.post, key.top, 0.85f);
  box(0, by0, bz0, 16, by1, bz1, key.side, key.top, 1.f);
  VoxelMeshData md;
  buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

class SignLayer : public RenderLayer {
public:
  const char* name() const override { return "signs"; }
  int order() const override { return 112; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    App& a = app();
    frameNo++;
    ContentDraw& cd = contentDraw();
    CreatureDraw& woodDraw = creatureDraw();
    bool begun = false, woodOn = false;
    const int woodS = clampv(a.tiles.size > 0 ? a.tiles.size : 32, 1, 32);
    struct Text { GLuint tex; mat4 m; vec2 light; };
    std::vector<Text> texts;
    forBlockEntities(w, f, 40.f, [&](const Chunk&, json& e, int x, int y, int z) {
      if (e.value("type", "") != "sign") return;
      BlockId id = w.getBlock(x, y, z);
      const BlockDef& bd = reg().block(id);
      if (!bd.has("sign")) return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (!f.cam.frustum.sphere(rel + vec3(0.5f), 1.f)) return;
      u8 l = w.getLight(x, y, z);
      vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
      u8 meta = w.getMeta(x, y, z);
      bool wall = meta & 16;
      float ang = wall ? -(float)(meta & 3) * PI * 0.5f : (float)(meta & 15) / 16.f * TAU;
      SeatShift sh = seatShiftOf(id, meta, w.getCarve(x, y, z), WorldShapeNb(w, x, y, z));
      vec3 anchor(0.5f, 0, 0.5f);
      if (sh.micro) anchor += vec3(sh.x, sh.y, sh.z) / 16.f;
      // the wood's planks for the board (oak_sign -> planks, birch_sign -> birch_planks ...)
      std::string wood = bd.key.substr(0, bd.key.size() - 5);
      BlockId pl = reg().blockId(wood == "oak" ? "planks" : wood + "_planks");
      if (!pl) pl = reg().blockId("planks");
      if (e.contains("paint") && e["paint"].is_string()) if (BlockId pp = reg().blockId(e["paint"].get<std::string>() + "_painted_planks")) pl = pp;   // painted with the brush (BTA)
      u16 sideTile = reg().block(pl).faces[0], topTile = reg().block(pl).faces[2];
      u16 postTile = sideTile;
      if (!wall) {
        BlockId lg = reg().blockId(wood == "oak" ? "log" : wood + "_log");
        if (lg) postTile = reg().block(lg).faces[0];
      }
      if (sideTile >= a.tiles.count) return;
      mat4 m = glm::translate(mat4(1), rel + anchor);
      m = glm::rotate(m, ang, vec3(0, 1, 0));
      m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
      // in this frame the writing faces +z; a wall sign's board lies against the wall at z 0
      float by0 = wall ? 4.5f : 7.f, by1 = by0 + 8.f, bz0 = wall ? 0.f : 7.f, bz1 = bz0 + 1.6f;
      SignWoodKey wk; wk.side = sideTile; wk.top = topTile; wk.post = postTile; wk.wall = wall ? 1 : 0; wk.S = (u16)woodS;
      const VoxelMeshGL* mesh = signWoodMesh(a.tiles, wk);
      if (mesh && mesh->ready() && woodDraw.begin(f)) {
        woodOn = true;
        woodDraw.draw(*mesh, m * glm::scale(mat4(1), vec3(1.f / woodS)), VoxelLight{light.x, light.y});
      } else {
        GLuint side = tileTexture(a, sideTile), top = tileTexture(a, topTile);
        if (!side) return;
        if (!begun) { if (!cd.begin(f, f.cam.viewProj)) return; begun = true; }
        drawTileBox(cd, m, vec3(0, by0, bz0), vec3(16, by1, bz1), side, top, top, light);
        if (!wall) {
          GLuint post = tileTexture(a, postTile);
          drawTileBox(cd, m, vec3(7.2f, 0, 7.2f), vec3(8.8f, by0, 8.8f), post ? post : side, top, top, light, vec3(0.85f));
        }
      }
      // the writing
      std::array<std::string, 4> L;
      bool any = false;
      if (e.contains("lines") && e["lines"].is_array())
        for (size_t i = 0; i < 4 && i < e["lines"].size(); i++) if (e["lines"][i].is_string()) { L[i] = e["lines"][i].get<std::string>(); any |= !L[i].empty(); }
      if (!any) return;
      vec3 nrm(std::sin(ang), 0, std::cos(ang));                       // the writing's side (+z turned by ang)
      if (glm::dot(-(rel + anchor + vec3(0, 0.6f, 0)), nrm) < 0) return;   // seen from behind: plain boards
      bool glow = e.value("glow", false);
      std::string col = e.value("color", std::string("black"));
      auto dc = dyeColours().find(col);
      u32 rgb = dc != dyeColours().end() ? dc->second : 0x1c140c;
      std::string key = L[0] + "\n" + L[1] + "\n" + L[2] + "\n" + L[3] + "|" + col + (glow ? "*" : "");
      u64 pk = ((u64)(u32)x << 40) ^ ((u64)(u32)y << 20) ^ (u64)(u32)z;
      SignTex& st = cache[pk];
      if (st.key != key || !st.tex) {
        auto px = paintSign(L, rgb, glow);
        if (st.tex) updateTexRGBA(st.tex, 128, 64, px.data(), false);
        else st.tex = makeTexRGBA(128, 64, px.data(), true, false);
        st.key = key;
      }
      st.frame = frameNo;
      mat4 tm = glm::translate(m, vec3(0.5f, (by0 + 4.f) / 16.f, bz1 / 16.f + 0.004f));
      tm = glm::scale(tm, vec3(14.f / 16.f, 7.f / 16.f, 1));
      texts.push_back({st.tex, tm, glow ? vec2(1, 1) : light});
    });
    if (woodOn) woodDraw.end();
    if (!texts.empty() && !begun) begun = cd.begin(f, f.cam.viewProj);
    for (auto& t : texts) {
      if (!begun) break;
      bool glowLit = t.light == vec2(1, 1);
      cd.picture(t.tex, t.m, vec4(0, 0, 1, 1), vec4(1), t.light, !glowLit);
    }
    if (begun) cd.end();
    // forget the writing of signs out of sight for a while
    if (frameNo % 600 == 0)
      for (auto it = cache.begin(); it != cache.end();) { if (frameNo - it->second.frame > 600) { glDeleteTextures(1, &it->second.tex); it = cache.erase(it); } else ++it; }
  }
private:
  std::map<u64, SignTex> cache;
  u64 frameNo = 0;
};

} // namespace

void registerSignLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<SignLayer>(); }); }

} // namespace dq
