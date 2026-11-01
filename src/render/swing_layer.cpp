// render/swing_layer.cpp — doors, fence gates and trapdoors swing open and shut instead of snapping (Phase 5:
// "village gates swing"). When one turns (World::changeOldMeta tells the open bit flipped), the block is left out of
// the chunk mesh (meshHiddenBlocks) and drawn here for a moment as boxes turning on their hinges: a door about its
// hinge post, each wing of a gate about its post, a trapdoor about its hinged edge. Then the mesh takes it back.
#include "render/tile_box.h"
#include "render/mesher.h"
#include "client/session.h"
#include "client/app.h"
#include "world/shapes.h"
#include "core/registry.h"
#include "assets/voxel.h"
#include <unordered_map>
#include <memory>

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

// turning about a hinge line through (px, pz) (1/16 block) by `ang` about y, or about x through (py, pz)
mat4 aboutY(float px, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(px, 0, pz) / 16.f); q = glm::rotate(q, ang, vec3(0, 1, 0)); return glm::translate(q, -vec3(px, 0, pz) / 16.f); }
mat4 aboutX(float py, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(0, py, pz) / 16.f); q = glm::rotate(q, ang, vec3(1, 0, 0)); return glm::translate(q, -vec3(0, py, pz) / 16.f); }

// A door or trapdoor leaf as micro-voxels with the same raised and sunken pixels the chunk mesh gives it (the tile set's
// baked relief), so it doesn't go flat while it turns: a slab 32 x 32 x 6 voxels, its face picture on both sides (the
// back mirrored, as the mesher draws it), each pixel sunk by its relief step (keeping a voxel of core). Doors stand in
// x/y with the slab across z; trapdoors lie in x/z with the slab across y.
std::unique_ptr<VoxelMeshGL> leafModel(const TileSet& ts, int tile, bool lying) {
  const int S = ts.size, D = 6;
  if (tile < 0 || tile >= ts.count || S != 32) return nullptr;
  const u8* px = ts.tile(tile);
  const bool hasRelief = ts.relief.size() >= (size_t)ts.count * S * S;
  auto rel = [&](int c, int r) { return hasRelief ? (int)ts.relief[(size_t)tile * S * S + r * S + c] : 0; };
  auto col = [&](int c, int r) -> u32 { const u8* q = px + ((size_t)r * S + c) * 4; return q[3] < 128 ? 0u : voxRGBA(q[0], q[1], q[2]); };
  VoxelGrid g(lying ? ivec3(S, D, S) : ivec3(S, S, D));
  for (int a = 0; a < S; a++) for (int b = 0; b < S; b++) {
    // (a, b) across the leaf; the front picture's column/row and the back's
    int fc, fr, bc, br;
    if (lying) { fc = a; fr = b; bc = S - 1 - a; br = b; }                    // top: (X, Z); bottom: (16 - X, Z)
    else { fc = a; fr = S - 1 - b; bc = S - 1 - a; br = S - 1 - b; }         // +z: (X, 16 - Y); -z: (16 - X, 16 - Y)
    const u32 cf = col(fc, fr), cb = col(bc, br);
    int rf = std::min(rel(fc, fr), 3), rb = std::min(rel(bc, br), 2);
    for (int d = 0; d < D; d++) {   // d = 0 at the back, D-1 at the front
      const bool front = d >= D / 2;
      const u32 c = front ? cf : cb;
      if (!c) continue;
      if (front ? (D - 1 - d) < rf : d < rb) continue;   // sunk
      if (lying) g.set(a, d, b, c); else g.set(a, b, d, c);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto m = std::make_unique<VoxelMeshGL>(); m->upload(md);
  return m;
}
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
    // doors and trapdoors: relief voxels (drawn first, in their own pass)
    {
      const TileSet& ts = s.app.tiles;
      if (&ts != modelsOf || ts.count != modelsCount) { models.clear(); modelsOf = &ts; modelsCount = ts.count; }
      bool begun = false;
      for (auto& o : live) {
        const int kind = R.tShape[o.id];
        if (kind != ShapeKind::Door && kind != ShapeKind::Trapdoor) continue;
        const BlockDef& d = R.block(o.id);
        const BlockTraits& T = traits(o.id);
        int side = kind == ShapeKind::Door ? d.faces[0] : d.faces[2];
        if (T.door && (o.to & 16) && T.upperTile >= 0) side = T.upperTile;
        const int key = side * 2 + (kind == ShapeKind::Trapdoor);
        auto it = models.find(key);
        if (it == models.end()) it = models.emplace(key, leafModel(ts, side, kind == ShapeKind::Trapdoor)).first;
        if (!it->second || !it->second->ready()) continue;
        if (!begun) { voxelRenderer().begin(f, f.cam.viewProj); begun = true; }
        const float a = std::min(1.f, o.t / SWING), e = a * a * (3 - 2 * a);
        const float open = (o.to & 4) ? e : 1 - e;
        const u8 l = w.getLight(o.p.x, o.p.y, o.p.z);
        mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(o.p)) + vec3(0.5f, 0, 0.5f));
        m = glm::rotate(m, -(o.to & 3) * PI / 2, vec3(0, 1, 0));
        m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
        mat4 q;
        if (kind == ShapeKind::Door) {
          const bool right = o.to & 8;
          q = m * aboutY(right ? 14.5f : 1.5f, 14.5f, (right ? -1 : 1) * PI / 2 * open) * glm::translate(mat4(1), vec3(0, 0, 13 / 16.f));
        } else {
          const bool high = o.to & 8;
          q = m * (high ? aboutX(14.5f, 1.5f, PI / 2 * open) : aboutX(1.5f, 1.5f, -PI / 2 * open)) * glm::translate(mat4(1), vec3(0, high ? 13 / 16.f : 0, 0));
        }
        voxelRenderer().draw(*it->second, glm::scale(q, vec3(1 / 32.f)), VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
      }
      if (begun) voxelRenderer().end();
    }
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
      const GLuint ts = tileTexture(s.app, side), tt = tileTexture(s.app, top), tb = tileTexture(s.app, bot);
      const u8 l = w.getLight(o.p.x, o.p.y, o.p.z);
      const vec2 light((l >> 4) / 15.f, (l & 15) / 15.f);
      mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(o.p)) + vec3(0.5f, 0, 0.5f));
      m = glm::rotate(m, -fm * PI / 2, vec3(0, 1, 0));
      m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
      if (kind == ShapeKind::Door) {
        if (hasModel(o)) continue;
        const bool right = o.to & 8;
        drawTileBox(cd, m * aboutY(right ? 14.5f : 1.5f, 14.5f, (right ? -1 : 1) * PI / 2 * open), vec3(0, 0, 13), vec3(16, 16, 16), ts, tt, tb, light);
      } else if (kind == ShapeKind::Trapdoor) {
        if (hasModel(o)) continue;
        const bool high = o.to & 8;
        const mat4 q = m * (high ? aboutX(14.5f, 1.5f, PI / 2 * open) : aboutX(1.5f, 1.5f, -PI / 2 * open));
        drawTileBox(cd, q, vec3(0, high ? 13 : 0, 0), vec3(16, high ? 16 : 3, 16), ts, tt, tb, light);
      } else {   // a gate: two posts, and a wing on each turning about its post
        drawTileBox(cd, m, vec3(0, 5, 7), vec3(2, 16, 9), ts, tt, tb, light);
        drawTileBox(cd, m, vec3(14, 5, 7), vec3(16, 16, 9), ts, tt, tb, light);
        for (int k = 0; k < 2; k++) {
          const mat4 q = m * aboutY(k ? 15.f : 1.f, 8, (k ? 1 : -1) * PI / 2 * open);
          const float x0 = k ? 8 : 2, x1 = k ? 14 : 8;
          drawTileBox(cd, q, vec3(x0, 6, 7), vec3(x1, 9, 9), ts, tt, tb, light);
          drawTileBox(cd, q, vec3(x0, 12, 7), vec3(x1, 15, 9), ts, tt, tb, light);
          drawTileBox(cd, q, vec3(k ? 8 : 6, 9, 7), vec3(k ? 10 : 8, 12, 9), ts, tt, tb, light);
        }
      }
    }
    cd.end();
  }
private:
  World* watchOn = nullptr;
  std::unordered_map<int, std::unique_ptr<VoxelMeshGL>> models;   // leaf models by tile*2 + trapdoor
  const TileSet* modelsOf = nullptr; int modelsCount = -1;
  bool hasModel(const Swing& o) const {
    const Registry& R = reg(); const int kind = R.tShape[o.id];
    const BlockDef& d = R.block(o.id); const BlockTraits& T = traits(o.id);
    int side = kind == ShapeKind::Door ? d.faces[0] : d.faces[2];
    if (T.door && (o.to & 16) && T.upperTile >= 0) side = T.upperTile;
    auto it = models.find(side * 2 + (kind == ShapeKind::Trapdoor));
    return it != models.end() && it->second && it->second->ready();
  }
  std::vector<Swing> live;
  static void unhide(const ivec3& p) { auto& H = meshHiddenBlocks(); for (size_t i = 0; i < H.size(); i++) if (H[i] == p) { H.erase(H.begin() + i); return; } }
  void release(GameSession&, bool) { live.clear(); meshHiddenBlocks().clear(); }
};

void registerSwingLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<SwingLayer>(); }); }

} // namespace dq
