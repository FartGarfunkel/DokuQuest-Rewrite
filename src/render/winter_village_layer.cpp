// render/winter_village_layer.cpp — December in the villages (common/holiday.h christmas()): strings of coloured lights
// along the eaves of every house, and a decorated tree on the square with lights, a star and presents under it. All
// drawn here, nothing put in the world: when December is over the villages are as they were. The places are found by
// looking at the houses' blocks (the eave is the highest block over each edge of a house's plot).
#include "render/tile_box.h"
#include "assets/voxel.h"
#include "assets/tileset.h"
#include "client/session.h"
#include "client/app.h"
#include "common/holiday.h"
#include "sim/villagers.h"
#include "core/registry.h"
#include <unordered_map>

namespace dq {

namespace {
const vec3 BULB[6] = {{1.f, 0.18f, 0.16f}, {0.2f, 0.95f, 0.3f}, {0.25f, 0.45f, 1.f}, {1.f, 0.8f, 0.2f}, {1.f, 0.95f, 0.85f}, {0.95f, 0.3f, 0.85f}};

GLuint roundTex() {   // a soft round bulb
  static GLuint t = 0;
  if (t) return t;
  const int N = 16; std::vector<u8> px(N * N * 4);
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float d = std::hypot(x + 0.5f - N / 2.f, y + 0.5f - N / 2.f) / (N / 2.f);
    u8* p = &px[(y * N + x) * 4]; float k = 1 - 0.35f * d;
    p[0] = p[1] = p[2] = (u8)(255 * k); p[3] = d < 0.92f ? 255 : 0;
  }
  return t = makeTexRGBA(N, N, px.data(), true, false);
}
GLuint starTex() {   // a five-pointed star
  static GLuint t = 0;
  if (t) return t;
  const int N = 32; std::vector<u8> px(N * N * 4);
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float dx = x + 0.5f - N / 2.f, dy = N / 2.f - (y + 0.5f), r = std::hypot(dx, dy) / (N / 2.f), a = std::atan2(dx, dy);
    float k = std::fmod(a + PI * 2 + PI / 5, PI * 2 / 5) / (PI * 2 / 5);   // 0..1 across one point
    u8* p = &px[(y * N + x) * 4];
    const bool in = r < 0.42f + 0.55f * (1 - std::fabs(k - 0.5f) * 2);   // the points reach out to ~0.97
    p[0] = 255; p[1] = (u8)(210 + 45 * (1 - r)); p[2] = (u8)(70 + 120 * (1 - r)); p[3] = in ? 255 : 0;
  }
  return t = makeTexRGBA(N, N, px.data(), true, false);
}
float hrand(u32 h) { h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16; return (h & 0xffffff) / 16777216.f; }
} // namespace

class WinterVillageLayer : public RenderLayer {
public:
  const char* name() const override { return "winter-villages"; }
  int order() const override { return 114; }
  void update(GameSession& s, double dt) override {
    if (!christmas() || !s.player || s.player->realm != REALM_MAIN) return;
    if ((scanT -= dt) > 0) return;
    scanT = 1;
    World& w = s.world();
    const double now = s.app.time;
    for (auto& v : villagesNear(*s.level, s.player->pos.x, s.player->pos.z, 96)) {
      Deco& d = deco[v->id];
      if (now - d.at < 20 || !w.isLoaded(v->x, v->y, v->z)) continue;
      d = build(w, *v); d.at = now;
    }
  }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!christmas() || deco.empty() || !s.player || s.player->realm != REALM_MAIN) return;
    ContentDraw& cd = contentDraw();
    bool begun = false;
    const float t = (float)f.time;
    const glm::dvec3 cam = f.cam.pos;
    const float cy = f.cam.yaw;
    auto bulb = [&](vec3 p, int c, float size, int i) {
      vec3 rel = f.cam.rel(glm::dvec3(p));
      if (!f.cam.frustum.sphere(rel, 0.2f)) return;
      const float tw = (i % 5 == 0) ? 0.55f + 0.45f * std::sin(t * 3.1f + i * 1.7f) : 0.85f + 0.15f * std::sin(t * 1.3f + i);   // some twinkle
      mat4 m = glm::translate(mat4(1), rel); m = glm::rotate(m, cy, vec3(0, 1, 0)); m = glm::scale(m, vec3(size));
      cd.picture(roundTex(), m, vec4(0, 0, 1, 1), vec4(BULB[c % 6] * tw, 1), vec2(1, 1), false);
    };
    for (auto& [id, d] : deco) {
      if (glm::distance(glm::dvec3(d.center), cam) > 120) continue;
      if (!begun) { if (!cd.begin(f, f.cam.viewProj)) return; begun = true; }
      for (size_t i = 0; i < d.bulbs.size(); i++) if (glm::distance(glm::dvec3(d.bulbs[i].first), cam) < 64) bulb(d.bulbs[i].first, d.bulbs[i].second, 0.21f, (int)i);
      if (d.tree) drawTree(s, f, cd, d, bulb);
    }
    if (begun) cd.end();
  }
private:
  struct Deco { double at = -1e9; vec3 center{0}; std::vector<std::pair<vec3, int>> bulbs; bool tree = false; vec3 treeAt{0}; u32 seed = 0; };
  std::unordered_map<std::string, Deco> deco;
  double scanT = 0;

  static Deco build(World& w, const Village& v) {
    const Registry& R = reg();
    Deco d; d.center = vec3(v.x + 0.5f, v.y + 1, v.z + 0.5f); d.seed = v.seed;
    int c = 0;
    // the eaves: the highest block over each edge of each house's plot
    for (const VBld& b : v.blds) {
      if (b.H < 3 || d.bulbs.size() > 900) continue;
      // walking out from the edge of the plot, the outermost block of roof with air under it is the eave: a light hangs
      // under it. A plain wall up to the top gets one on its face instead.
      auto edge = [&](int x, int z, int nx, int nz) {
        int bx = 0, bz = 0, by = -1;
        for (int k = 0; k <= 3; k++) {
          const int cx = x + nx * k, cz = z + nz * k;
          if (!w.isLoaded(cx, b.y, cz)) break;
          int hang = -1;
          for (int y = b.y + b.H + 8; y >= b.y + 3; y--) {
            BlockId id = w.getBlock(cx, y, cz);
            if (!R.tSolid[id] || R.tLeaves[id]) continue;
            if (!R.tSolid[w.getBlock(cx, y - 1, cz)]) { hang = y; break; }
          }
          if (hang >= 0) { bx = cx; bz = cz; by = hang; }
        }
        if (by >= 0) { d.bulbs.push_back({vec3(bx + 0.5f, by - 0.16f, bz + 0.5f), c++ % 6}); return; }
        for (int y = b.y + b.H + 8; y >= b.y + 2; y--) {
          BlockId id = w.getBlock(x, y, z);
          if (!R.tSolid[id] || R.tLeaves[id]) continue;
          if (!R.tSolid[w.getBlock(x + nx, y, z + nz)]) d.bulbs.push_back({vec3(x + 0.5f + nx * 0.56f, y + 0.4f, z + 0.5f + nz * 0.56f), c++ % 6});
          return;
        }
      };
      for (int x = b.x0; x <= b.x1; x++) { edge(x, b.z0, 0, -1); edge(x, b.z1, 0, 1); }
      for (int z = b.z0 + 1; z < b.z1; z++) { edge(b.x0, z, -1, 0); edge(b.x1, z, 1, 0); }
    }
    // the tree: a clear spot of ground a few steps off the middle of the square
    for (int r = 4; r <= 10 && !d.tree; r++) for (int k = 0; k < 16 && !d.tree; k++) {
      const float a = k * PI / 8 + 0.3f;
      const int x = v.x + (int)std::lround(std::cos(a) * r), z = v.z + (int)std::lround(std::sin(a) * r);
      int gy = -1;
      for (int y = v.y + 4; y >= v.y - 4; y--) { BlockId id = w.getBlock(x, y, z); if (R.tLiquid[id]) break; if (R.tSolid[id]) { gy = y; break; } }
      if (gy < 0) continue;
      bool ok = true;
      for (int dz = -2; dz <= 2 && ok; dz++) for (int dx = -2; dx <= 2 && ok; dx++) for (int y = gy + 1; y <= gy + 7 && ok; y++) {
        BlockId id = w.getBlock(x + dx, y, z + dz);
        if (R.tSolid[id] || R.tLiquid[id]) ok = false;
      }
      for (int dz = -1; dz <= 1 && ok; dz++) for (int dx = -1; dx <= 1 && ok; dx++) if (!R.tSolid[w.getBlock(x + dx, gy, z + dz)]) ok = false;
      if (ok) { d.tree = true; d.treeAt = vec3(x + 0.5f, gy + 1, z + 0.5f); }
    }
    return d;
  }

  // The trunk and each tier of the tree. The picture is stretched over the piece, the way the old box was.
  struct TreeKey {
    u16 side = 0, top = 0; u8 n[3] = {}; u8 tint[3] = {255, 255, 255};
    bool operator==(const TreeKey& o) const {
      return side == o.side && top == o.top && n[0] == o.n[0] && n[1] == o.n[1] && n[2] == o.n[2]
          && tint[0] == o.tint[0] && tint[1] == o.tint[1] && tint[2] == o.tint[2];
    }
  };
  struct TreeHash {
    size_t operator()(const TreeKey& k) const {
      size_t h = (size_t)k.side * 1315423911u ^ ((size_t)k.top << 16) ^ k.n[0] ^ ((size_t)k.n[1] << 8) ^ ((size_t)k.n[2] << 16);
      return h ^ k.tint[0] ^ ((size_t)k.tint[1] << 8) ^ ((size_t)k.tint[2] << 16);
    }
  };
  static const VoxelMeshGL* treePiece(const TileSet& ts, TreeKey key) {
    struct Cache {
      const u8* px = nullptr; int count = -1, size = -1;
      std::unordered_map<TreeKey, std::unique_ptr<VoxelMeshGL>, TreeHash> m;
    };
    static Cache cache;
    if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
      cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
    }
    for (int i = 0; i < 3; i++) if (!key.n[i]) key.n[i] = 1;
    auto it = cache.m.find(key);
    if (it != cache.m.end()) return it->second.get();
    const int nx = key.n[0], ny = key.n[1], nz = key.n[2], src = ts.size > 0 ? ts.size : 32;
    VoxelGrid g(ivec3(nx, ny, nz));
    bool hole = false;
    auto sample = [&](u16 tile, float u, float v) -> u32 {
      if (tile >= ts.count) { hole = true; return 0; }
      int sx = clampv((int)(u * src), 0, src - 1), sy = clampv((int)(v * src), 0, src - 1);
      const u8* p = ts.tile(tile) + ((size_t)sy * src + sx) * 4;
      if (p[3] < 128) { hole = true; return 0; }
      return voxRGBA(p[0] * key.tint[0] / 255, p[1] * key.tint[1] / 255, p[2] * key.tint[2] / 255);
    };
    for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) {
      float v = 1.f - (y + 0.5f) / ny;
      if (u32 c = sample(key.side, 1.f - (z + 0.5f) / nz, v)) g.set(nx - 1, y, z, c);
      if (u32 c = sample(key.side, (z + 0.5f) / nz, v)) g.set(0, y, z, c);
    }
    for (int z = 0; z < nz; z++) for (int x = 0; x < nx; x++) {
      if (u32 c = sample(key.top, (x + 0.5f) / nx, (z + 0.5f) / nz)) g.set(x, ny - 1, z, c);
      if (u32 c = sample(key.top, 1.f - (x + 0.5f) / nx, (z + 0.5f) / nz)) g.set(x, 0, z, c);
    }
    for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) {
      float v = 1.f - (y + 0.5f) / ny;
      if (u32 c = sample(key.side, (x + 0.5f) / nx, v)) g.set(x, y, nz - 1, c);
      if (u32 c = sample(key.side, 1.f - (x + 0.5f) / nx, v)) g.set(x, y, 0, c);
    }
    // A solid picture gets a plain interior so only the outside faces are drawn. A hole stays a hole.
    if (!hole) {
      const u32 fill = voxRGBA(1, 1, 1);
      for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) if (!g.at(x, y, z)) g.set(x, y, z, fill);
    }
    VoxelMeshData md; buildVoxelMesh(g, md);
    auto mesh = std::make_unique<VoxelMeshGL>();
    mesh->upload(md);
    const VoxelMeshGL* out = mesh.get();
    cache.m.emplace(key, std::move(mesh));
    return out;
  }

  template <class Bulb> void drawTree(GameSession& s, const FrameCtx& f, ContentDraw& cd, const Deco& d, Bulb& bulb) {
    const vec3 rel = f.cam.rel(glm::dvec3(d.treeAt));
    if (!f.cam.frustum.sphere(rel + vec3(0, 3.5f, 0), 4.5f)) return;
    World& w = s.world();
    const u8 l = w.getLight(ifloor(d.treeAt.x), ifloor(d.treeAt.y) + 1, ifloor(d.treeAt.z));
    const vec2 light((l >> 4) / 15.f, std::max(0.8f, (l & 15) / 15.f));   // its own lights light it up
    const VoxelLight L{light.x, light.y};
    const mat4 m = glm::translate(mat4(1), rel);
    auto tileId = [&](const char* name) -> int {
      auto it = s.app.tiles.index.find(name);
      if (it == s.app.tiles.index.end() || it->second < 0 || it->second >= s.app.tiles.count) return -1;
      return it->second;
    };
    const int sideId = tileId("spruce_log_side"), topId = tileId("spruce_log_top"), leafId = tileId("spruce_leaves");
    const vec3 green(0.36f, 0.58f, 0.36f);
    const u8 gt[3] = {(u8)std::lround(green.r * 255.f), (u8)std::lround(green.g * 255.f), (u8)std::lround(green.b * 255.f)};
    float tierHw[5], tierY[5];
    for (int i = 0; i < 5; i++) { tierHw[i] = 2.2f - i * 0.43f; tierY[i] = 0.8f + i * 1.0f; }
    const float tierH = 1.25f;
    auto drawPiece = [&](VoxelRenderer& VR, vec3 a, vec3 b, u16 side, u16 top, u8 tr, u8 tg, u8 tb) -> bool {
      const vec3 ext = b - a;
      TreeKey key;
      key.side = side; key.top = top;
      key.n[0] = (u8)std::min(255, std::max(1, (int)std::lround(std::fabs(ext.x) * 32.f)));
      key.n[1] = (u8)std::min(255, std::max(1, (int)std::lround(std::fabs(ext.y) * 32.f)));
      key.n[2] = (u8)std::min(255, std::max(1, (int)std::lround(std::fabs(ext.z) * 32.f)));
      key.tint[0] = tr; key.tint[1] = tg; key.tint[2] = tb;
      const VoxelMeshGL* mesh = treePiece(s.app.tiles, key);
      if (!mesh || !mesh->ready()) return false;
      const mat4 model = m * glm::translate(mat4(1), a) * glm::scale(mat4(1), vec3(ext.x / key.n[0], ext.y / key.n[1], ext.z / key.n[2]));
      VR.draw(*mesh, model, L);
      return true;
    };
    bool logBox = sideId < 0 || topId < 0, leafBox = leafId < 0;
    if (!logBox || !leafBox) {
      cd.end();
      VoxelRenderer& VR = voxelRenderer();
      VR.begin(f, f.cam.viewProj);
      if (!logBox) logBox = !drawPiece(VR, vec3(-0.22f, 0, -0.22f), vec3(0.22f, 1.3f, 0.22f), (u16)sideId, (u16)topId, 255, 255, 255);
      if (!leafBox) {
        bool any = false;
        for (int i = 0; i < 5; i++)
          any |= drawPiece(VR, vec3(-tierHw[i], tierY[i], -tierHw[i]), vec3(tierHw[i], tierY[i] + tierH, tierHw[i]), (u16)leafId, (u16)leafId, gt[0], gt[1], gt[2]);
        leafBox = !any;
      }
      VR.end();
      if (!cd.begin(f, f.cam.viewProj)) return;
    }
    if (logBox) {
      const GLuint log = tileTexture(s.app, "spruce_log_side"), logTop = tileTexture(s.app, "spruce_log_top");
      drawTileBox(cd, m, vec3(-0.22f, 0, -0.22f), vec3(0.22f, 1.3f, 0.22f), log, logTop, logTop, light, vec3(1), false);
    }
    if (leafBox) {
      const GLuint leaves = tileTexture(s.app, "spruce_leaves");
      for (int i = 0; i < 5; i++)
        drawTileBox(cd, m, vec3(-tierHw[i], tierY[i], -tierHw[i]), vec3(tierHw[i], tierY[i] + tierH, tierHw[i]), leaves, leaves, leaves, light, green, false);
    }
    int bi = 0;
    for (int i = 0; i < 5; i++) {
      const float hw = tierHw[i], y0 = tierY[i];
      const int per = std::max(3, (int)(hw * 3.6f));   // lights round the tier's lower edge
      for (int side = 0; side < 4; side++) for (int k = 0; k < per; k++) {
        const float u = -hw + (k + 0.5f) * (2 * hw) / per, o = hw + 0.04f;
        const float ly = y0 + 0.62f;   // above the tier below
        const vec3 p = side == 0 ? vec3(u, ly, -o) : side == 1 ? vec3(o, ly, u) : side == 2 ? vec3(-u, ly, o) : vec3(-o, ly, -u);
        bulb(d.treeAt + p, (bi + i) % 6, 0.25f, bi); bi++;
      }
    }
    // the star, turning slowly on top
    {
      mat4 sm = glm::translate(m, vec3(0, 0.8f + 5 * 1.0f + 0.45f, 0));
      sm = glm::rotate(sm, (float)f.time * 0.6f, vec3(0, 1, 0)); sm = glm::scale(sm, vec3(0.8f));
      cd.picture(starTex(), sm, vec4(0, 0, 1, 1), vec4(1, 1, 1, 1), vec2(1, 1), false);
    }
    // presents underneath: wrapped boxes with a ribbon round them
    static const vec3 PAPER[5] = {{0.75f, 0.12f, 0.14f}, {0.16f, 0.45f, 0.24f}, {0.2f, 0.32f, 0.7f}, {0.85f, 0.85f, 0.82f}, {0.6f, 0.2f, 0.6f}};
    for (int k = 0; k < 5; k++) {
      const float a = k * 1.3f + hrand(d.seed + k) * 0.5f, r = 1.35f + hrand(d.seed * 7 + k) * 0.5f;
      const float sz = 0.32f + hrand(d.seed * 13 + k) * 0.22f, hy = sz * (0.7f + hrand(d.seed * 3 + k) * 0.5f);
      mat4 pm = glm::translate(m, vec3(std::cos(a) * r, 0, std::sin(a) * r)); pm = glm::rotate(pm, a * 2.1f, vec3(0, 1, 0));
      const vec3 c = PAPER[(k + d.seed) % 5];
      drawTileBox(cd, pm, vec3(-sz / 2, 0, -sz / 2), vec3(sz / 2, hy, sz / 2), 0, 0, 0, light, c, false);
      const vec3 gold(0.95f, 0.78f, 0.25f);
      drawTileBox(cd, pm, vec3(-0.035f, 0, -sz / 2 - 0.005f), vec3(0.035f, hy + 0.005f, sz / 2 + 0.005f), 0, 0, 0, light, gold, false);
      drawTileBox(cd, pm, vec3(-sz / 2 - 0.005f, 0, -0.035f), vec3(sz / 2 + 0.005f, hy + 0.005f, 0.035f), 0, 0, 0, light, gold, false);
    }
  }
};

void registerWinterVillageLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<WinterVillageLayer>(); }); }

} // namespace dq
