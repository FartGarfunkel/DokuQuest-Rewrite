// render/torch_layer.cpp — a torch near the player is a stick of coloured voxels, the same window of its picture
// the cutout box used to show. The flame keeps changing frame. Past that distance the chunk mesh still draws it.
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
    if (begun) VR.end();
  }
};

} // namespace

void registerTorchLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<TorchLayer>(); }); }

} // namespace dq
