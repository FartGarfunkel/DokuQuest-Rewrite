// render/swing_layer.cpp — doors, fence gates and trapdoors swing open and shut instead of snapping (Phase 5:
// "village gates swing"). When one turns (World::changeOldMeta tells the open bit flipped), the block is left out of
// the chunk mesh (meshHiddenBlocks) and drawn here for a moment as boxes turning on their hinges: a door about its
// hinge post, each wing of a gate about its post, a trapdoor about its hinged edge. Then the mesh takes it back.
#include "render/content_draw.h"
#include "render/mesher.h"
#include "client/session.h"
#include "client/app.h"
#include "world/shapes.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

namespace {
const float SLOW = std::getenv("DQ_SWING_SLOW") ? 25.f : 1.f;   // tests: slow enough to see
const float SWING = 0.22f * SLOW, HOLD = 0.2f;   // seconds turning, then drawn still while the mesh catches up

struct Swing { ivec3 p; BlockId id; u8 from, to; float t = 0; bool hidden = true; };

class SwingWatch : public WorldSystem {
public:
  std::vector<Swing> fresh;
  const char* name() const override { return "swingWatch"; }
  void onBlockChanged(World& w, int x, int y, int z, BlockId o, BlockId n) override {
    if (o != n || !n) return;
    const int k = reg().tShape[n];
    if (k != ShapeKind::Door && k != ShapeKind::Gate && k != ShapeKind::Trapdoor) return;
    const u8 m = w.getMeta(x, y, z);
    if (!((w.changeOldMeta ^ m) & 4)) return;   // only the open bit
    fresh.push_back({ivec3(x, y, z), n, w.changeOldMeta, m});
  }
};

GLuint tileTex(App& a, int layer) {
  static std::unordered_map<int, GLuint> cache;
  static const TileSet* of = nullptr; static int count = -1;
  if (of != &a.tiles || count != a.tiles.count) { for (auto& [k, t] : cache) glDeleteTextures(1, &t); cache.clear(); of = &a.tiles; count = a.tiles.count; }
  auto it = cache.find(layer);
  if (it != cache.end()) return it->second;
  if (layer < 0 || layer >= a.tiles.count) return 0;
  const int N = a.tiles.size;
  std::vector<u8> px(a.tiles.tile(layer), a.tiles.tile(layer) + N * N * 4);
  for (size_t i = 3; i < px.size(); i += 4) if (px[i] >= 250) px[i] = 255;
  return cache[layer] = makeTexRGBA(N, N, px.data(), true, false);
}

// one box (in 1/16 block, x0..x1 etc.) as six pictures under `m`; side tile on the four sides, top/bottom on the ends
void box(ContentDraw& cd, const mat4& m, vec3 a, vec3 b, GLuint side, GLuint top, GLuint bot, vec2 light) {
  a /= 16.f; b /= 16.f;
  const vec3 c = (a + b) * 0.5f, s = b - a;
  auto pic = [&](GLuint t, const mat4& q, vec4 uv, float shade) { cd.picture(t, m * q, uv, vec4(shade, shade, shade, 1), light); };
  // the faces facing z (front and back)
  for (int k = 0; k < 2; k++) {
    mat4 q = glm::translate(mat4(1), vec3(c.x, c.y, k ? b.z : a.z)); q = glm::scale(q, vec3(s.x, s.y, 1));
    pic(side, q, vec4(a.x, 1 - b.y, b.x, 1 - a.y), 0.9f);
  }
  for (int k = 0; k < 2; k++) {   // facing x
    mat4 q = glm::translate(mat4(1), vec3(k ? b.x : a.x, c.y, c.z)); q = glm::rotate(q, PI / 2, vec3(0, 1, 0)); q = glm::scale(q, vec3(s.z, s.y, 1));
    pic(side, q, vec4(a.z, 1 - b.y, b.z, 1 - a.y), 0.75f);
  }
  for (int k = 0; k < 2; k++) {   // facing y
    mat4 q = glm::translate(mat4(1), vec3(c.x, k ? b.y : a.y, c.z)); q = glm::rotate(q, PI / 2, vec3(1, 0, 0)); q = glm::scale(q, vec3(s.x, s.z, 1));
    pic(k ? top : bot, q, vec4(a.x, a.z, b.x, b.z), k ? 1.f : 0.6f);
  }
}
// turning about a hinge line through (px, pz) (1/16 block) by `ang` about y, or about x through (py, pz)
mat4 aboutY(float px, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(px, 0, pz) / 16.f); q = glm::rotate(q, ang, vec3(0, 1, 0)); return glm::translate(q, -vec3(px, 0, pz) / 16.f); }
mat4 aboutX(float py, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(0, py, pz) / 16.f); q = glm::rotate(q, ang, vec3(1, 0, 0)); return glm::translate(q, -vec3(0, py, pz) / 16.f); }
} // namespace

class SwingLayer : public RenderLayer {
public:
  const char* name() const override { return "swing"; }
  int order() const override { return 113; }
  void update(GameSession& s, double dt) override {
    World& w = s.world();
    if (watchOn != &w) {   // a new world (or realm): watch it, and let go of what we were drawing
      if (!w.system<SwingWatch>()) w.addSystem(std::make_shared<SwingWatch>());
      watchOn = &w; release(s, true);
    }
    auto* sw = w.system<SwingWatch>();
    for (auto& f : sw->fresh) {
      for (auto& o : live) if (o.p == f.p) { o.hidden = false; o.t = 1e9f; }   // turned again mid-swing: start over
      live.push_back(f);
      meshHiddenBlocks().push_back(f.p);
      w.markBlockDirty(f.p.x, f.p.y, f.p.z, true);
    }
    sw->fresh.clear();
    for (auto& o : live) {
      o.t += (float)dt;
      if (o.hidden && o.t >= SWING) { o.hidden = false; unhide(o.p); w.markBlockDirty(o.p.x, o.p.y, o.p.z, true); }
    }
    live.erase(std::remove_if(live.begin(), live.end(), [&](const Swing& o) { return o.t >= SWING + HOLD || w.getBlock(o.p.x, o.p.y, o.p.z) != o.id; }), live.end());
    // a block taken away mid-swing: make sure the mesh has it back
    auto& H = meshHiddenBlocks();
    H.erase(std::remove_if(H.begin(), H.end(), [&](const ivec3& p) { for (auto& o : live) if (o.p == p && o.hidden) return false; w.markBlockDirty(p.x, p.y, p.z, true); return true; }), H.end());
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (live.empty()) return;
    World& w = s.world();
    const Registry& R = reg();
    ContentDraw& cd = contentDraw();
    if (!cd.begin(f, f.cam.viewProj)) return;
    for (auto& o : live) {
      const BlockDef& d = R.block(o.id);
      const BlockTraits& T = traits(o.id);
      const int kind = R.tShape[o.id];
      const float a = std::min(1.f, o.t / SWING), e = a * a * (3 - 2 * a);
      const float open = (o.to & 4) ? e : 1 - e;   // 0 shut .. 1 open
      const int fm = o.to & 3;
      int side = d.faces[0], top = d.faces[2], bot = d.faces[3];
      if (T.door) { if ((o.to & 16) && T.upperTile >= 0) side = T.upperTile; top = bot = side; }
      const GLuint ts = tileTex(s.app, side), tt = tileTex(s.app, top), tb = tileTex(s.app, bot);
      const u8 l = w.getLight(o.p.x, o.p.y, o.p.z);
      const vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
      mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(o.p)) + vec3(0.5f, 0, 0.5f));
      m = glm::rotate(m, -fm * PI / 2, vec3(0, 1, 0));
      m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
      if (kind == ShapeKind::Door) {
        const bool right = o.to & 8;
        box(cd, m * aboutY(right ? 14.5f : 1.5f, 14.5f, (right ? -1 : 1) * PI / 2 * open), vec3(0, 0, 13), vec3(16, 16, 16), ts, tt, tb, light);
      } else if (kind == ShapeKind::Trapdoor) {
        const bool high = o.to & 8;
        const mat4 q = m * (high ? aboutX(14.5f, 1.5f, PI / 2 * open) : aboutX(1.5f, 1.5f, -PI / 2 * open));
        box(cd, q, vec3(0, high ? 13 : 0, 0), vec3(16, high ? 16 : 3, 16), ts, tt, tb, light);
      } else {   // a gate: two posts, and a wing on each turning about its post
        box(cd, m, vec3(0, 5, 7), vec3(2, 16, 9), ts, tt, tb, light);
        box(cd, m, vec3(14, 5, 7), vec3(16, 16, 9), ts, tt, tb, light);
        for (int k = 0; k < 2; k++) {
          const mat4 q = m * aboutY(k ? 15.f : 1.f, 8, (k ? 1 : -1) * PI / 2 * open);
          const float x0 = k ? 8 : 2, x1 = k ? 14 : 8;
          box(cd, q, vec3(x0, 6, 7), vec3(x1, 9, 9), ts, tt, tb, light);
          box(cd, q, vec3(x0, 12, 7), vec3(x1, 15, 9), ts, tt, tb, light);
          box(cd, q, vec3(k ? 8 : 6, 9, 7), vec3(k ? 10 : 8, 12, 9), ts, tt, tb, light);
        }
      }
    }
    cd.end();
  }
private:
  World* watchOn = nullptr;
  std::vector<Swing> live;
  static void unhide(const ivec3& p) { auto& H = meshHiddenBlocks(); for (size_t i = 0; i < H.size(); i++) if (H[i] == p) { H.erase(H.begin() + i); return; } }
  void release(GameSession&, bool) { live.clear(); meshHiddenBlocks().clear(); }
};

void registerSwingLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<SwingLayer>(); }); }

} // namespace dq
