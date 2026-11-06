// render/torch_layer.cpp — a torch or candle near the player is coloured voxels, the same crop of its picture
// the cutout box used to show. Flames keep changing frame. Past that distance the chunk mesh still draws them.
#include "client/session.h"
#include "client/app.h"
#include "assets/voxel.h"
#include "assets/tileset.h"
#include <unordered_map>
#include <cmath>

namespace dq {

namespace {

const VoxelMeshGL* torchMesh(const TileSet& ts, int layer) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<int, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  auto it = cache.m.find(layer);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  const int u0 = (int)(7.f / 16.f * S);
  const int u1 = std::max(u0 + 1, (int)(9.f / 16.f * S));
  const int vTop = (int)(6.f / 16.f * S);
  const int vBot = S;
  const int nx = u1 - u0, ny = std::max(1, vBot - vTop), nz = nx;
  VoxelGrid g(ivec3(nx, ny, nz));
  auto pix = [&](int col, int row) -> u32 {
    if (layer < 0 || layer >= ts.count) return 0;
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  auto colAt = [&](float across) { return u0 + (int)(across * (u1 - u0)); };
  auto rowAt = [&](float up) { return vTop + (int)((1.f - up) * (vBot - vTop)); };
  for (int z = 0; z < nz; z++) for (int y = 0; y < ny; y++) {
    const int row = rowAt((y + 0.5f) / ny), col = colAt((z + 0.5f) / nz);
    if (u32 c = pix(col, row)) { g.set(nx - 1, y, z, c); g.set(0, y, z, c); }
  }
  for (int y = 0; y < ny; y++) for (int x = 0; x < nx; x++) {
    const int row = rowAt((y + 0.5f) / ny), col = colAt((x + 0.5f) / nx);
    if (u32 c = pix(col, row)) { g.set(x, y, nz - 1, c); g.set(x, y, 0, c); }
  }
  const int top0 = vTop, top1 = std::min(S, (int)(8.f / 16.f * S));
  const int bot0 = (int)(14.f / 16.f * S);
  for (int z = 0; z < nz; z++) for (int x = 0; x < nx; x++) {
    const int col = colAt((x + 0.5f) / nx);
    if (u32 c = pix(col, top0 + (int)((z + 0.5f) / nz * std::max(1, top1 - top0)))) g.set(x, ny - 1, z, c);
    if (u32 c = pix(col, vBot - 1 - (int)((z + 0.5f) / nz * std::max(1, vBot - bot0)))) g.set(x, 0, z, c);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(layer, std::move(mesh));
  return out;
}

// The same crop emitBox takes from a candle or sea pickle (shapeST, in sixteenths).
vec2 shapeST(int f, float X, float Y, float Z) {
  switch (f) {
    case 0: return {16.f - Z, 16.f - Y};
    case 1: return {Z, 16.f - Y};
    case 2: return {X, Z};
    case 3: return {16.f - X, Z};
    case 4: return {X, 16.f - Y};
    default: return {16.f - X, 16.f - Y};
  }
}

struct WaxKey {
  u16 tile = 0;
  i16 b[6] = {};
  bool operator==(const WaxKey& o) const {
    return tile == o.tile && b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2]
        && b[3] == o.b[3] && b[4] == o.b[4] && b[5] == o.b[5];
  }
};
struct WaxHash {
  size_t operator()(const WaxKey& k) const {
    size_t h = k.tile;
    for (int i = 0; i < 6; i++) h = h * 131u + (u16)k.b[i];
    return h;
  }
};

void waxCounts(const i16 b[6], int& nx, int& ny, int& nz) {
  auto n = [](i16 a, i16 c) {
    int v = (int)std::lround((double)(c - a) * 2.0);
    if (v < 1) v = 1;
    if (v > 48) v = 48;
    return v;
  };
  nx = n(b[0], b[3]); ny = n(b[1], b[4]); nz = n(b[2], b[5]);
}

// Two voxels per sixteenth. A see-through texel stays empty; a solid crop is filled so only the outside shows.
const VoxelMeshGL* candleMesh(const TileSet& ts, int layer, const i16 b[6]) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<WaxKey, std::unique_ptr<VoxelMeshGL>, WaxHash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  WaxKey key; key.tile = (u16)layer;
  for (int i = 0; i < 6; i++) key.b[i] = b[i];
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  int nx, ny, nz; waxCounts(b, nx, ny, nz);
  const int S = ts.size > 0 ? ts.size : 32;
  const float x0 = b[0], y0 = b[1], z0 = b[2], x1 = b[3], y1 = b[4], z1 = b[5];
  VoxelGrid g(ivec3(nx, ny, nz));
  bool hole = false;
  auto pix = [&](int f, float X, float Y, float Z) -> u32 {
    if (layer < 0 || layer >= ts.count) return 0;
    vec2 st = shapeST(f, X, Y, Z);
    st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
    int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  auto put = [&](int ix, int iy, int iz, u32 c) { if (!c) { hole = true; return; } g.set(ix, iy, iz, c); };
  for (int iz = 0; iz < nz; iz++) for (int iy = 0; iy < ny; iy++) {
    float Y = y0 + (iy + 0.5f) * 0.5f, Z = z0 + (iz + 0.5f) * 0.5f;
    put(nx - 1, iy, iz, pix(0, x1, Y, Z));
    put(0, iy, iz, pix(1, x0, Y, Z));
  }
  for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
    float X = x0 + (ix + 0.5f) * 0.5f, Z = z0 + (iz + 0.5f) * 0.5f;
    put(ix, ny - 1, iz, pix(2, X, y1, Z));
    put(ix, 0, iz, pix(3, X, y0, Z));
  }
  for (int iy = 0; iy < ny; iy++) for (int ix = 0; ix < nx; ix++) {
    float X = x0 + (ix + 0.5f) * 0.5f, Y = y0 + (iy + 0.5f) * 0.5f;
    put(ix, iy, nz - 1, pix(4, X, Y, z1));
    put(ix, iy, 0, pix(5, X, Y, z0));
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

// The flame picture is a few texels inside the whole tile. One voxel per texel keeps that sprite;
// a plate only as wide as the quad (about four voxels) samples the empty part and the flame vanishes.
const VoxelMeshGL* flamePlate(const TileSet& ts, int layer) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<int, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  auto it = cache.m.find(layer);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  VoxelGrid g(ivec3(S, S, 1));
  if (layer >= 0 && layer < ts.count) {
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      const u8* p = ts.tile(layer) + ((size_t)(S - 1 - y) * S + x) * 4;
      if (p[3] < 128) continue;
      g.set(x, y, 0, voxRGBA(p[0], p[1], p[2], VOXEL_GLOW));
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(layer, std::move(mesh));
  return out;
}

class TorchLayer : public RenderLayer {
public:
  const char* name() const override { return "voxel-torches"; }
  int order() const override { return 40; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    const TileSet& ts = s.app.tiles;
    if (ts.count <= 0) return;
    const int S = ts.size > 0 ? ts.size : 32;
    std::vector<int> anim(ts.count, -1);
    for (const TileAnim& an : ts.anims) {
      if (an.frames.empty() || an.layer < 0 || an.layer >= ts.count) continue;
      int fr = (int)((int64_t)(f.time * 1000.0 / std::max(1, an.frameTime)) % (int64_t)an.frames.size());
      int src = an.frames[(size_t)fr];
      if (src >= 0 && src < ts.count) anim[an.layer] = src;
    }
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    const float sc = 1.f / S, half = 1.f / 16.f, LEAN = 0.4f;
    const float sa = std::sin(LEAN), ca = std::cos(LEAN);
    s.worldRenderer.forEachTorchSection([&](vec3 origin, const std::vector<TorchSpot>& spots) {
      vec3 relO = f.cam.rel(glm::dvec3(origin));
      if (!f.cam.frustum.sphere(relO + vec3(8), 14.f)) return;
      for (const TorchSpot& t : spots) {
        vec3 rel = relO + vec3(t.x, t.y, t.z);
        if (!f.cam.frustum.sphere(rel, 0.6f)) continue;
        int layer = t.tile < ts.count ? (int)t.tile : -1;
        if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
        const VoxelMeshGL* mesh = layer >= 0 ? torchMesh(ts, layer) : nullptr;
        if (!mesh || !mesh->ready()) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        mat4 lean(1.f);
        if (t.lx || t.lz) lean[1] = vec4((float)t.lx * sa, ca, (float)t.lz * sa, 0.f);
        const mat4 model = glm::translate(mat4(1), rel) * lean * glm::translate(mat4(1), vec3(-half, 0, -half)) * glm::scale(mat4(1), vec3(sc));
        const u8 l = w.getLight((int)origin.x + t.cx, (int)origin.y + t.cy, (int)origin.z + t.cz);
        VR.draw(*mesh, model, VoxelLight{(l >> 4) / 15.f, (l & 15) / 15.f});
      }
    });
    const float FW = 0.11f, FH = 0.2f;
    s.worldRenderer.forEachCandleSection([&](vec3 origin, const std::vector<CandleSpot>& spots) {
      vec3 relO = f.cam.rel(glm::dvec3(origin));
      if (!f.cam.frustum.sphere(relO + vec3(8), 14.f)) return;
      for (const CandleSpot& c : spots) {
        vec3 cell = relO + vec3(c.cx, c.cy, c.cz);
        if (!f.cam.frustum.sphere(cell + vec3(0.5f), 0.6f)) continue;
        int layer = c.tile < ts.count ? (int)c.tile : -1;
        i16 box[6] = {
          (i16)std::lround(c.x0), (i16)std::lround(c.y0), (i16)std::lround(c.z0),
          (i16)std::lround(c.x1), (i16)std::lround(c.y1), (i16)std::lround(c.z1)};
        int nx = 1, ny = 1, nz = 1;
        waxCounts(box, nx, ny, nz);
        const VoxelMeshGL* mesh = layer >= 0 ? candleMesh(ts, layer, box) : nullptr;
        const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
        VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
        if (mesh && mesh->ready()) {
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          float dx = std::max(c.x1 - c.x0, 0.01f), dy = std::max(c.y1 - c.y0, 0.01f), dz = std::max(c.z1 - c.z0, 0.01f);
          vec3 corner = cell + vec3(c.x0, c.y0 + c.sink, c.z0) / 16.f;
          const mat4 model = glm::translate(mat4(1), corner) * glm::scale(mat4(1), vec3(dx / (nx * 16.f), dy / (ny * 16.f), dz / (nz * 16.f)));
          VR.draw(*mesh, model, light);
        }
        int flame = c.flame;
        if (flame < 0 || flame >= ts.count) continue;
        if (anim[flame] >= 0) flame = anim[flame];
        const VoxelMeshGL* plate = flamePlate(ts, flame);
        if (!plate || !plate->ready()) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        const int FS = ts.size > 0 ? ts.size : 32;
        const float th = FW / FS;
        vec3 ctr = cell + vec3((c.x0 + c.x1) / 32.f, (c.y1 + c.sink) / 16.f, (c.z0 + c.z1) / 32.f);
        const mat4 park = glm::translate(mat4(1), vec3(-FW * 0.5f, 0.f, -th * 0.5f));
        const mat4 sc = glm::scale(mat4(1), vec3(FW / FS, FH / FS, th));
        const mat4 at = glm::translate(mat4(1), ctr);
        VR.draw(*plate, at * park * sc, light);
        VR.draw(*plate, at * glm::rotate(mat4(1), 1.5707963f, vec3(0, 1, 0)) * park * sc, light);
      }
    });
    if (begun) VR.end();
  }
};

} // namespace

void registerTorchLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<TorchLayer>(); }); }

} // namespace dq
