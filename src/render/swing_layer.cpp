// render/swing_layer.cpp — doors, fence gates and trapdoors swing open and shut instead of snapping (Phase 5:
// "village gates swing"). When one turns (World::changeOldMeta tells the open bit flipped), the block is left out of
// the chunk mesh (meshHiddenBlocks) and drawn here for a moment as boxes turning on their hinges: a door about its
// hinge post, each wing of a gate about its post, a trapdoor about its hinged edge. Then the mesh takes it back.
#include "render/tile_box.h"
#include "render/mesh_ctx.h"
#include "client/session.h"
#include "client/app.h"
#include "world/shapes.h"
#include "core/registry.h"
#include "assets/voxel.h"
#include "assets/tileset.h"
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

// turning about a hinge line through (px, pz) (1/16 block) by `ang` about y, or about x through (py, pz)
mat4 aboutY(float px, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(px, 0, pz) / 16.f); q = glm::rotate(q, ang, vec3(0, 1, 0)); return glm::translate(q, -vec3(px, 0, pz) / 16.f); }
mat4 aboutX(float py, float pz, float ang) { mat4 q = glm::translate(mat4(1), vec3(0, py, pz) / 16.f); q = glm::rotate(q, ang, vec3(1, 0, 0)); return glm::translate(q, -vec3(0, py, pz) / 16.f); }

// The same crop candleMesh uses for a gate rail (shapeST, two voxels per sixteenth).
vec2 gateST(int f, float X, float Y, float Z) {
  switch (f) {
    case 0: return {16.f - Z, 16.f - Y};
    case 1: return {Z, 16.f - Y};
    case 2: return {X, Z};
    case 3: return {16.f - X, Z};
    case 4: return {X, 16.f - Y};
    default: return {16.f - X, 16.f - Y};
  }
}

const VoxelMeshGL* gateBoxMesh(const TileSet& ts, int side, int top, int bot, float x0, float y0, float z0, float x1, float y1, float z1) {
  struct Key {
    u16 side, top, bot;
    i16 b[6];
    bool operator==(const Key& o) const {
      return side == o.side && top == o.top && bot == o.bot
          && b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2] && b[3] == o.b[3] && b[4] == o.b[4] && b[5] == o.b[5];
    }
  };
  struct Hash {
    size_t operator()(const Key& k) const {
      size_t h = ((size_t)k.side * 131u + k.top) * 131u + k.bot;
      for (int i = 0; i < 6; i++) h = h * 131u + (u16)k.b[i];
      return h;
    }
  };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  Key key;
  key.side = (u16)std::max(side, 0); key.top = (u16)std::max(top, 0); key.bot = (u16)std::max(bot, 0);
  key.b[0] = (i16)std::lround(x0); key.b[1] = (i16)std::lround(y0); key.b[2] = (i16)std::lround(z0);
  key.b[3] = (i16)std::lround(x1); key.b[4] = (i16)std::lround(y1); key.b[5] = (i16)std::lround(z1);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  auto span = [](i16 a, i16 c) {
    int v = (int)std::lround((double)(c - a) * 2.0);
    if (v < 1) v = 1;
    if (v > 48) v = 48;
    return v;
  };
  const int nx = span(key.b[0], key.b[3]), ny = span(key.b[1], key.b[4]), nz = span(key.b[2], key.b[5]);
  const int S = ts.size > 0 ? ts.size : 32;
  const float bx0 = key.b[0], by0 = key.b[1], bz0 = key.b[2], bx1 = key.b[3], by1 = key.b[4], bz1 = key.b[5];
  VoxelGrid g(ivec3(nx, ny, nz));
  bool hole = false;
  auto pix = [&](int tile, int f, float X, float Y, float Z) -> u32 {
    if (tile < 0 || tile >= ts.count) return 0;
    vec2 st = gateST(f, X, Y, Z);
    st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
    int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(tile) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  auto put = [&](int ix, int iy, int iz, u32 c) { if (!c) { hole = true; return; } g.set(ix, iy, iz, c); };
  for (int iz = 0; iz < nz; iz++) for (int iy = 0; iy < ny; iy++) {
    float Y = by0 + (iy + 0.5f) * 0.5f, Z = bz0 + (iz + 0.5f) * 0.5f;
    put(nx - 1, iy, iz, pix(side, 0, bx1, Y, Z));
    put(0, iy, iz, pix(side, 1, bx0, Y, Z));
  }
  for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
    float X = bx0 + (ix + 0.5f) * 0.5f, Z = bz0 + (iz + 0.5f) * 0.5f;
    put(ix, ny - 1, iz, pix(top, 2, X, by1, Z));
    put(ix, 0, iz, pix(bot, 3, X, by0, Z));
  }
  for (int iy = 0; iy < ny; iy++) for (int ix = 0; ix < nx; ix++) {
    float X = bx0 + (ix + 0.5f) * 0.5f, Y = by0 + (iy + 0.5f) * 0.5f;
    put(ix, iy, nz - 1, pix(side, 4, X, Y, bz1));
    put(ix, iy, 0, pix(side, 5, X, Y, bz0));
  }
  if (!hole) {
    const u32 fill = voxRGBA(1, 1, 1);
    for (int z = 1; z < nz - 1; z++) for (int y = 1; y < ny - 1; y++) for (int x = 1; x < nx - 1; x++) g.set(x, y, z, fill);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

void drawGateBox(VoxelRenderer& VR, const TileSet& ts, const mat4& base, int side, int top, int bot,
                 float x0, float y0, float z0, float x1, float y1, float z1, VoxelLight light) {
  const VoxelMeshGL* mesh = gateBoxMesh(ts, side, top, bot, x0, y0, z0, x1, y1, z1);
  if (!mesh || !mesh->ready()) return;
  const float bx0 = (float)std::lround(x0), by0 = (float)std::lround(y0), bz0 = (float)std::lround(z0);
  const float bx1 = (float)std::lround(x1), by1 = (float)std::lround(y1), bz1 = (float)std::lround(z1);
  auto span = [](float a, float c) {
    int v = (int)std::lround((double)(c - a) * 2.0);
    if (v < 1) v = 1;
    if (v > 48) v = 48;
    return v;
  };
  const int nx = span(bx0, bx1), ny = span(by0, by1), nz = span(bz0, bz1);
  const float dx = std::max(bx1 - bx0, 0.01f), dy = std::max(by1 - by0, 0.01f), dz = std::max(bz1 - bz0, 0.01f);
  const mat4 model = base * glm::translate(mat4(1), vec3(bx0, by0, bz0) / 16.f)
      * glm::scale(mat4(1), vec3(dx / (nx * 16.f), dy / (ny * 16.f), dz / (nz * 16.f)));
  VR.draw(*mesh, model, light);
}

// The resting door is a voxel plate. Draw that same plate, still sampling the tile, so the swing
// does not pop back to a flat box. A missing picture falls through to the box.
bool drawPanel(ContentDraw& cd, const mat4& model, const MeshTiles* tiles, int tile, int which, GLuint tex, vec2 light) {
  if (!tiles || !tex) return false;
  std::vector<PanelVert> q;
  if (!doorPanelQuads(tiles, tile, which, q) || q.size() < 4) return false;
  bool any = false;
  for (size_t i = 0; i + 3 < q.size(); i += 4) {
    const PanelVert& a = q[i];
    const PanelVert& b = q[i + 1];
    const PanelVert& d = q[i + 3];
    vec3 ex = b.p - a.p, ey = d.p - a.p;
    if (glm::dot(ex, ex) < 1e-12f || glm::dot(ey, ey) < 1e-12f) continue;
    mat4 local(1);
    local[0] = vec4(ex, 0); local[1] = vec4(ey, 0); local[3] = vec4(a.p + 0.5f * ex + 0.5f * ey, 1);
    float sh = a.sh / 255.f;
    cd.picture(tex, model * local, vec4(d.uv.x, d.uv.y, b.uv.x, b.uv.y), vec4(sh, sh, sh, 1), light, true);
    any = true;
  }
  return any;
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
    // A door keeps the tile picture for the whole swing, so the plate does not change.
    // A gate near you swings as the same coloured rails the resting gate uses.
    auto gateNear = [&](const Swing& o) {
      if (R.tShape[o.id] != ShapeKind::Gate) return false;
      vec3 rel = f.cam.rel(glm::dvec3(o.p));
      return glm::dot(rel, rel) < 40.f * 40.f;
    };
    bool anyPicture = false;
    for (auto& o : live) if (!gateNear(o)) { anyPicture = true; break; }
    ContentDraw& cd = contentDraw();
    bool cdOn = false;
    std::shared_ptr<const MeshTiles> tilesHold;
    const MeshTiles* tiles = nullptr;
    if (anyPicture && cd.begin(f, f.cam.viewProj)) {
      cdOn = true;
      tilesHold = meshTilesSnapshot();
      tiles = tilesHold.get();
    }
    for (auto& o : live) {
      if (gateNear(o)) continue;
      if (!cdOn) continue;
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
        const bool right = o.to & 8;
        const mat4 q = m * aboutY(right ? 14.5f : 1.5f, 14.5f, (right ? -1 : 1) * PI / 2 * open);
        // The plate uses the tile's own texels. The box is only the fallback, and it keeps uvMode 1.
        if (!drawPanel(cd, q, tiles, side, 0, ts, light))
          drawTileBox(cd, q, vec3(0, 0, 13), vec3(16, 16, 16), ts, tt, tb, light, vec3(1), true, true, vec3(1), 1);
      } else if (kind == ShapeKind::Trapdoor) {
        const bool high = o.to & 8;
        const mat4 q = m * (high ? aboutX(14.5f, 1.5f, PI / 2 * open) : aboutX(1.5f, 1.5f, -PI / 2 * open));
        if (!drawPanel(cd, q, tiles, side, high ? 2 : 1, ts, light))
          drawTileBox(cd, q, vec3(0, high ? 13 : 0, 0), vec3(16, high ? 16 : 3, 16), ts, tt, tb, light, vec3(1), true, true, vec3(1), 2);
      } else {   // a far gate: two posts, and a wing on each turning about its post
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
    if (cdOn) cd.end();
    bool voxOn = false;
    VoxelRenderer& VR = voxelRenderer();
    const TileSet& tileSet = s.app.tiles;
    for (auto& o : live) {
      if (!gateNear(o)) continue;
      const BlockDef& d = R.block(o.id);
      const float a = std::min(1.f, o.t / SWING), e = a * a * (3 - 2 * a);
      const float open = (o.to & 4) ? e : 1 - e;
      const int fm = o.to & 3;
      const int side = d.faces[0], top = d.faces[2], bot = d.faces[3];
      const u8 l = w.getLight(o.p.x, o.p.y, o.p.z);
      const VoxelLight light{(l >> 4) / 15.f, (l & 15) / 15.f};
      mat4 m = glm::translate(mat4(1), f.cam.rel(glm::dvec3(o.p)) + vec3(0.5f, 0, 0.5f));
      m = glm::rotate(m, -fm * PI / 2, vec3(0, 1, 0));
      m = glm::translate(m, vec3(-0.5f, 0, -0.5f));
      if (!voxOn) { VR.begin(f, f.cam.viewProj); voxOn = true; }
      drawGateBox(VR, tileSet, m, side, top, bot, 0, 5, 7, 2, 16, 9, light);
      drawGateBox(VR, tileSet, m, side, top, bot, 14, 5, 7, 16, 16, 9, light);
      for (int k = 0; k < 2; k++) {
        const mat4 q = m * aboutY(k ? 15.f : 1.f, 8, (k ? 1 : -1) * PI / 2 * open);
        const float x0 = k ? 8.f : 2.f, x1 = k ? 14.f : 8.f;
        drawGateBox(VR, tileSet, q, side, top, bot, x0, 6, 7, x1, 9, 9, light);
        drawGateBox(VR, tileSet, q, side, top, bot, x0, 12, 7, x1, 15, 9, light);
        drawGateBox(VR, tileSet, q, side, top, bot, k ? 8.f : 6.f, 9, 7, k ? 10.f : 8.f, 12, 9, light);
      }
    }
    if (voxOn) VR.end();
  }
private:
  World* watchOn = nullptr;
  std::vector<Swing> live;
  static void unhide(const ivec3& p) { auto& H = meshHiddenBlocks(); for (size_t i = 0; i < H.size(); i++) if (H[i] == p) { H.erase(H.begin() + i); return; } }
  void release(GameSession&, bool) { live.clear(); meshHiddenBlocks().clear(); }
};

void registerSwingLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<SwingLayer>(); }); }

} // namespace dq
