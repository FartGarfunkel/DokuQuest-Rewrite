// render/torch_layer.cpp — a torch or candle near the player is coloured voxels, the same crop of its picture
// the cutout box used to show. Flames keep changing frame. Past that distance the chunk mesh still draws them.
#include "client/session.h"
#include "client/app.h"
#include "assets/voxel.h"
#include "assets/tileset.h"
#include "world/shapes.h"
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
  u16 tile = 0, top = 0, bot = 0, nx = 0, pz = 0, nz = 0;
  u16 faceRot = 0;
  i16 b[6] = {};
  u8 glow = 0, only = 63;
  bool operator==(const WaxKey& o) const {
    return tile == o.tile && top == o.top && bot == o.bot && nx == o.nx && pz == o.pz && nz == o.nz && faceRot == o.faceRot
        && glow == o.glow && only == o.only
        && b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2] && b[3] == o.b[3] && b[4] == o.b[4] && b[5] == o.b[5];
  }
};
struct WaxHash {
  size_t operator()(const WaxKey& k) const {
    size_t h = k.tile * 131u + k.top;
    h = h * 131u + k.bot + (size_t)k.glow * 17u + (size_t)k.only;
    h = h * 131u + k.nx; h = h * 131u + k.pz; h = h * 131u + k.nz;
    h = h * 131u + k.faceRot;
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
// topL and botL are the top and bottom pictures (a cake's icing). They match layer when the box had one picture.
const VoxelMeshGL* candleMesh(const TileSet& ts, int layer, const i16 b[6], int topL, int botL, bool glow = false, int nxL = -1, int pzL = -1, int nzL = -1, u8 only = 63, u16 faceRot = 0) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<WaxKey, std::unique_ptr<VoxelMeshGL>, WaxHash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  if (nxL < 0) nxL = layer;
  if (pzL < 0) pzL = layer;
  if (nzL < 0) nzL = layer;
  WaxKey key; key.tile = (u16)layer; key.top = (u16)std::max(topL, 0); key.bot = (u16)std::max(botL, 0); key.glow = glow ? 1 : 0;
  key.nx = (u16)std::max(nxL, 0); key.pz = (u16)std::max(pzL, 0); key.nz = (u16)std::max(nzL, 0); key.only = only; key.faceRot = faceRot;
  for (int i = 0; i < 6; i++) key.b[i] = b[i];
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  int nx, ny, nz; waxCounts(b, nx, ny, nz);
  const int S = ts.size > 0 ? ts.size : 32;
  const float x0 = b[0], y0 = b[1], z0 = b[2], x1 = b[3], y1 = b[4], z1 = b[5];
  VoxelGrid g(ivec3(nx, ny, nz));
  bool hole = false;
  auto pix = [&](int f, float X, float Y, float Z) -> u32 {
    int L = f == 2 ? topL : f == 3 ? botL : f == 1 ? nxL : f == 4 ? pzL : f == 5 ? nzL : layer;
    if (L < 0 || L >= ts.count) return 0;
    vec2 st = shapeST(f, X, Y, Z);
    // The same quarter-turn emitCube applies with FACE_UV[(k + rot) & 3]. Rot 0 is the picture as shapeST sampled it.
    int r = (faceRot >> (f * 2)) & 3;
    if (r == 1) { float u = st.x; st.x = st.y; st.y = 16.f - u; }
    else if (r == 2) { st.x = 16.f - st.x; st.y = 16.f - st.y; }
    else if (r == 3) { float u = st.x; st.x = 16.f - st.y; st.y = u; }
    st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
    int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(L) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2], glow ? VOXEL_GLOW : VOXEL_SOLID);
  };
  auto put = [&](int ix, int iy, int iz, u32 c) { if (!c) { hole = true; return; } g.set(ix, iy, iz, c); };
  for (int iz = 0; iz < nz; iz++) for (int iy = 0; iy < ny; iy++) {
    float Y = y0 + (iy + 0.5f) * 0.5f, Z = z0 + (iz + 0.5f) * 0.5f;
    if (only & 1) put(nx - 1, iy, iz, pix(0, x1, Y, Z));
    if (only & 2) put(0, iy, iz, pix(1, x0, Y, Z));
  }
  for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
    float X = x0 + (ix + 0.5f) * 0.5f, Z = z0 + (iz + 0.5f) * 0.5f;
    if (only & 4) put(ix, ny - 1, iz, pix(2, X, y1, Z));
    if (only & 8) put(ix, 0, iz, pix(3, X, y0, Z));
  }
  for (int iy = 0; iy < ny; iy++) for (int ix = 0; ix < nx; ix++) {
    float X = x0 + (ix + 0.5f) * 0.5f, Y = y0 + (iy + 0.5f) * 0.5f;
    if (only & 16) put(ix, iy, nz - 1, pix(4, X, Y, z1));
    if (only & 32) put(ix, iy, 0, pix(5, X, Y, z0));
  }
  if (!hole && only == 63) {
    const u32 fill = voxRGBA(1, 1, 1, glow ? VOXEL_GLOW : VOXEL_SOLID);
    for (int z = 1; z < nz - 1; z++) for (int y = 1; y < ny - 1; y++) for (int x = 1; x < nx - 1; x++) g.set(x, y, z, fill);
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// The same poles and plank ShapeKind::Scaffold pushes. A spot with flame -2 draws this instead of a crop.
// The plank's picture has holes, so those texels stay empty.
const VoxelMeshGL* scaffoldMesh(const TileSet& ts, int side, int top, int bot) {
  struct Key {
    u16 side, top, bot;
    bool operator==(const Key& o) const { return side == o.side && top == o.top && bot == o.bot; }
  };
  struct Hash { size_t operator()(const Key& k) const { return ((size_t)k.side * 131u + k.top) * 131u + k.bot; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  Key key{(u16)std::max(side, 0), (u16)std::max(top, 0), (u16)std::max(bot, 0)};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  static const float boxes[][6] = {
    {0, 14, 0, 16, 16, 16},
    {0, 0, 0, 2, 14, 2}, {14, 0, 0, 16, 14, 2}, {0, 0, 14, 2, 14, 16}, {14, 0, 14, 16, 14, 16},
    {0, 1, 0, 16, 2, 2}, {0, 1, 14, 16, 2, 16},
  };
  auto vox = [](float a) {
    int v = (int)std::lround(a * 2.0);
    if (v < 0) v = 0;
    if (v > 48) v = 48;
    return v;
  };
  VoxelGrid g(ivec3(32, 32, 32));
  const int S = ts.size > 0 ? ts.size : 32;
  auto sample = [&](int tile, int f, float X, float Y, float Z) -> u32 {
    if (tile < 0 || tile >= ts.count) return 0;
    vec2 st = shapeST(f, X, Y, Z);
    st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
    int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(tile) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  for (auto& b : boxes) {
    int x0 = vox(b[0]), y0 = vox(b[1]), z0 = vox(b[2]);
    int x1 = vox(b[3]), y1 = vox(b[4]), z1 = vox(b[5]);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (z1 <= z0) z1 = z0 + 1;
    x1 = std::min(x1, 32); y1 = std::min(y1, 32); z1 = std::min(z1, 32);
    if (x1 <= x0 || y1 <= y0 || z1 <= z0) continue;
    bool hole = false;
    auto put = [&](int x, int y, int z, u32 c) {
      if (!c) { hole = true; return; }
      g.set(x, y, z, c);
    };
    for (int iz = z0; iz < z1; iz++) for (int iy = y0; iy < y1; iy++) {
      float Y = b[1] + (iy - y0 + 0.5f) * 0.5f, Z = b[2] + (iz - z0 + 0.5f) * 0.5f;
      put(x1 - 1, iy, iz, sample(side, 0, b[3], Y, Z));
      put(x0, iy, iz, sample(side, 1, b[0], Y, Z));
    }
    for (int iz = z0; iz < z1; iz++) for (int ix = x0; ix < x1; ix++) {
      float X = b[0] + (ix - x0 + 0.5f) * 0.5f, Z = b[2] + (iz - z0 + 0.5f) * 0.5f;
      put(ix, y1 - 1, iz, sample(top, 2, X, b[4], Z));
      put(ix, y0, iz, sample(bot, 3, X, b[1], Z));
    }
    for (int iy = y0; iy < y1; iy++) for (int ix = x0; ix < x1; ix++) {
      float X = b[0] + (ix - x0 + 0.5f) * 0.5f, Y = b[1] + (iy - y0 + 0.5f) * 0.5f;
      put(ix, iy, z1 - 1, sample(side, 4, X, Y, b[5]));
      put(ix, iy, z0, sample(side, 5, X, Y, b[2]));
    }
    if (!hole) {
      const u32 fill = voxRGBA(1, 1, 1);
      for (int z = z0 + 1; z < z1 - 1; z++) for (int y = y0 + 1; y < y1 - 1; y++) for (int x = x0 + 1; x < x1 - 1; x++)
        if (!g.at(x, y, z)) g.set(x, y, z, fill);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// A pot, chair, anvil or campfire: every box of the model, two voxels per sixteenth, turned the way rotBox turns it.
// One mesh per model and facing, so a room of chairs is one draw each. A log end is stretched the way addShapeBox stretches it.
const VoxelMeshGL* propMesh(const TileSet& ts, int model, int facing) {
  struct Key {
    u16 model; u8 facing;
    bool operator==(const Key& o) const { return model == o.model && facing == o.facing; }
  };
  struct Hash { size_t operator()(const Key& k) const { return (size_t)k.model * 4u + k.facing; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  Key key{(u16)model, (u8)(facing & 3)};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const std::vector<ShapeBox>* src = modelShape(model);
  if (!src || src->empty()) return nullptr;
  std::vector<ShapeBox> boxes = *src;
  for (auto& b : boxes) rotBox(b, facing & 3);
  auto vox = [](float a) {
    int v = (int)std::lround(a * 2.0);
    if (v < 0) v = 0;
    if (v > 48) v = 48;
    return v;
  };
  int gx = 1, gy = 1, gz = 1;
  for (auto& b : boxes) { gx = std::max(gx, vox(b.b[3])); gy = std::max(gy, vox(b.b[4])); gz = std::max(gz, vox(b.b[5])); }
  VoxelGrid g(ivec3(gx, gy, gz));
  const int S = ts.size > 0 ? ts.size : 32;
  for (auto& b : boxes) {
    int x0 = vox(b.b[0]), y0 = vox(b.b[1]), z0 = vox(b.b[2]);
    int x1 = vox(b.b[3]), y1 = vox(b.b[4]), z1 = vox(b.b[5]);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (z1 <= z0) z1 = z0 + 1;
    if (x0 >= gx || y0 >= gy || z0 >= gz) continue;
    x1 = std::min(x1, gx); y1 = std::min(y1, gy); z1 = std::min(z1, gz);
    int side = b.side >= 0 ? b.side : 0;
    int end = b.end >= 0 ? b.end : side;
    int xt = b.xt >= 0 ? b.xt : side;
    int xf = (facing & 1) ? 4 : 0;   // addShapeBox: an odd turn puts the x picture on z
    int zf = (facing & 1) ? 0 : 4;
    const bool grainTurn = b.grain && ((b.grain == 2) != !!(facing & 1));
    auto onX = [&](int f) { return f == xf || f == xf + 1; };
    auto onZ = [&](int f) { return b.zt >= 0 && (f == zf || f == zf + 1); };
    auto tileOf = [&](int f) {
      if (f == 2 || f == 3) return end;
      if (onX(f)) return xt;
      if (onZ(f)) return b.zt;
      return side;
    };
    // The same picture choice as addShapeBox: a saw still shifts, a log's top still runs along the log,
    // and a log's small end still shows the whole ring.
    auto styled = [&](int f, float X, float Y, float Z) {
      vec2 st = shapeST(f, X, Y, Z);
      if (b.tv != 0.f && f != 2 && f != 3) st.y += b.tv;
      if (grainTurn && (f == 2 || f == 3)) std::swap(st.x, st.y);
      return st;
    };
    const float X0 = b.b[0], Y0 = b.b[1], Z0 = b.b[2], X1 = b.b[3], Y1 = b.b[4], Z1 = b.b[5];
    auto fitST = [&](int f, float X, float Y, float Z) {
      vec2 st = styled(f, X, Y, Z);
      const bool fitF = b.fit && f != 2 && f != 3 && ((onX(f) && b.xt >= 0) || onZ(f));
      if (!fitF) return st;
      vec2 corner[4];
      auto putC = [&](int i, float cx, float cy, float cz) { corner[i] = styled(f, cx, cy, cz); };
      if (f == 0) { putC(0, X1, Y0, Z0); putC(1, X1, Y0, Z1); putC(2, X1, Y1, Z0); putC(3, X1, Y1, Z1); }
      else if (f == 1) { putC(0, X0, Y0, Z0); putC(1, X0, Y0, Z1); putC(2, X0, Y1, Z0); putC(3, X0, Y1, Z1); }
      else if (f == 4) { putC(0, X0, Y0, Z1); putC(1, X1, Y0, Z1); putC(2, X0, Y1, Z1); putC(3, X1, Y1, Z1); }
      else { putC(0, X0, Y0, Z0); putC(1, X1, Y0, Z0); putC(2, X0, Y1, Z0); putC(3, X1, Y1, Z0); }
      vec2 mn(1e9f), mx(-1e9f);
      for (auto& q : corner) { mn = glm::min(mn, q); mx = glm::max(mx, q); }
      return (st - mn) / glm::max(mx - mn, vec2(1e-4f)) * 16.f;
    };
    auto sample = [&](int f, float X, float Y, float Z) -> u32 {
      int tile = tileOf(f);
      if (tile < 0 || tile >= ts.count) return 0;
      vec2 st = fitST(f, X, Y, Z);
      st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
      int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
      col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
      const u8* p = ts.tile(tile) + ((size_t)row * S + col) * 4;
      if (p[3] < 128) return 0;
      return voxRGBA(p[0], p[1], p[2]);
    };
    bool hole = false;
    auto put = [&](int x, int y, int z, u32 c) {
      if (!c) { hole = true; return; }
      g.set(x, y, z, c);
    };
    for (int iz = z0; iz < z1; iz++) for (int iy = y0; iy < y1; iy++) {
      float Y = Y0 + (iy - y0 + 0.5f) * 0.5f, Z = Z0 + (iz - z0 + 0.5f) * 0.5f;
      put(x1 - 1, iy, iz, sample(0, X1, Y, Z));
      put(x0, iy, iz, sample(1, X0, Y, Z));
    }
    for (int iz = z0; iz < z1; iz++) for (int ix = x0; ix < x1; ix++) {
      float X = X0 + (ix - x0 + 0.5f) * 0.5f, Z = Z0 + (iz - z0 + 0.5f) * 0.5f;
      put(ix, y1 - 1, iz, sample(2, X, Y1, Z));
      put(ix, y0, iz, sample(3, X, Y0, Z));
    }
    for (int iy = y0; iy < y1; iy++) for (int ix = x0; ix < x1; ix++) {
      float X = X0 + (ix - x0 + 0.5f) * 0.5f, Y = Y0 + (iy - y0 + 0.5f) * 0.5f;
      put(ix, iy, z1 - 1, sample(4, X, Y, Z1));
      put(ix, iy, z0, sample(5, X, Y, Z0));
    }
    if (!hole) {
      const u32 fill = voxRGBA(1, 1, 1);
      for (int z = z0 + 1; z < z1 - 1; z++) for (int y = y0 + 1; y < y1 - 1; y++) for (int x = x0 + 1; x < x1 - 1; x++) g.set(x, y, z, fill);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// The end-rod picture is a strip down the left. These are the same shifts emitBox applies, so the
// shaft and the base pick the same texels the chunk mesh used.
const VoxelMeshGL* rodMesh(const TileSet& ts, int layer, int axis) {
  struct Key {
    u16 layer; u8 axis;
    bool operator==(const Key& o) const { return layer == o.layer && axis == o.axis; }
  };
  struct Hash { size_t operator()(const Key& k) const { return (size_t)k.layer * 4u + k.axis; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  Key key{(u16)std::max(layer, 0), (u8)axis};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  struct Part { float x0, y0, z0, x1, y1, z1; u8 only; float tu, tv; bool swap; };
  const u8 FX = 1 | 2, FY = 4 | 8, FZ = 16 | 32;
  Part parts[4];
  int n = 0;
  if (axis == 1) {
    parts[n++] = {0, 7, 7, 16, 9, 9, (u8)(FY | FZ), -7, 0, true};
    parts[n++] = {0, 7, 7, 16, 9, 9, FX, -5, -7, false};
  } else if (axis == 2) {
    parts[n++] = {7, 7, 0, 9, 9, 16, FX, -7, 0, true};
    parts[n++] = {7, 7, 0, 9, 9, 16, FY, -7, 0, false};
    parts[n++] = {7, 7, 0, 9, 9, 16, FZ, -5, -7, false};
  } else {
    parts[n++] = {7, 1, 7, 9, 16, 9, (u8)(FX | FZ), -7, 0, false};
    parts[n++] = {7, 1, 7, 9, 16, 9, FY, -5, -7, false};
    parts[n++] = {6, 0, 6, 10, 1, 10, (u8)(FX | FZ), -4, -9, false};
    parts[n++] = {6, 0, 6, 10, 1, 10, FY, -4, -4, false};
  }
  auto vox = [](float a) {
    int v = (int)std::lround(a * 2.0);
    if (v < 0) v = 0;
    if (v > 48) v = 48;
    return v;
  };
  int gx = 1, gy = 1, gz = 1;
  for (int i = 0; i < n; i++) { gx = std::max(gx, vox(parts[i].x1)); gy = std::max(gy, vox(parts[i].y1)); gz = std::max(gz, vox(parts[i].z1)); }
  VoxelGrid g(ivec3(gx, gy, gz));
  const int S = ts.size > 0 ? ts.size : 32;
  auto sample = [&](const Part& part, int f, float X, float Y, float Z) -> u32 {
    if (layer < 0 || layer >= ts.count) return 0;
    vec2 st = shapeST(f, X, Y, Z);
    if (part.swap) std::swap(st.x, st.y);
    st += vec2(part.tu, part.tv);
    st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
    int col = (int)std::floor(st.x / 16.f * S), row = (int)std::floor(st.y / 16.f * S);
    col = clampv(col, 0, S - 1); row = clampv(row, 0, S - 1);
    const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  for (int i = 0; i < n; i++) {
    const Part& part = parts[i];
    int x0 = vox(part.x0), y0 = vox(part.y0), z0 = vox(part.z0);
    int x1 = vox(part.x1), y1 = vox(part.y1), z1 = vox(part.z1);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (z1 <= z0) z1 = z0 + 1;
    x1 = std::min(x1, gx); y1 = std::min(y1, gy); z1 = std::min(z1, gz);
    if (x1 <= x0 || y1 <= y0 || z1 <= z0) continue;
    bool hole = false;
    auto put = [&](int x, int y, int z, u32 c) {
      if (!c) { hole = true; return; }
      g.set(x, y, z, c);
    };
    if (part.only & 1) for (int iz = z0; iz < z1; iz++) for (int iy = y0; iy < y1; iy++)
      put(x1 - 1, iy, iz, sample(part, 0, part.x1, part.y0 + (iy - y0 + 0.5f) * 0.5f, part.z0 + (iz - z0 + 0.5f) * 0.5f));
    if (part.only & 2) for (int iz = z0; iz < z1; iz++) for (int iy = y0; iy < y1; iy++)
      put(x0, iy, iz, sample(part, 1, part.x0, part.y0 + (iy - y0 + 0.5f) * 0.5f, part.z0 + (iz - z0 + 0.5f) * 0.5f));
    if (part.only & 4) for (int iz = z0; iz < z1; iz++) for (int ix = x0; ix < x1; ix++)
      put(ix, y1 - 1, iz, sample(part, 2, part.x0 + (ix - x0 + 0.5f) * 0.5f, part.y1, part.z0 + (iz - z0 + 0.5f) * 0.5f));
    if (part.only & 8) for (int iz = z0; iz < z1; iz++) for (int ix = x0; ix < x1; ix++)
      put(ix, y0, iz, sample(part, 3, part.x0 + (ix - x0 + 0.5f) * 0.5f, part.y0, part.z0 + (iz - z0 + 0.5f) * 0.5f));
    if (part.only & 16) for (int iy = y0; iy < y1; iy++) for (int ix = x0; ix < x1; ix++)
      put(ix, iy, z1 - 1, sample(part, 4, part.x0 + (ix - x0 + 0.5f) * 0.5f, part.y0 + (iy - y0 + 0.5f) * 0.5f, part.z1));
    if (part.only & 32) for (int iy = y0; iy < y1; iy++) for (int ix = x0; ix < x1; ix++)
      put(ix, iy, z0, sample(part, 5, part.x0 + (ix - x0 + 0.5f) * 0.5f, part.y0 + (iy - y0 + 0.5f) * 0.5f, part.z0));
    if (!hole) {
      const u32 fill = voxRGBA(1, 1, 1);
      for (int z = z0 + 1; z < z1 - 1; z++) for (int y = y0 + 1; y < y1 - 1; y++) for (int x = x0 + 1; x < x1 - 1; x++)
        if (!g.at(x, y, z)) g.set(x, y, z, fill);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// The lightning rod is a thin stick. Its picture keeps the copper in columns 0-2; the middle of the tile is empty.
const VoxelMeshGL* lightningMesh(const TileSet& ts, int layer, int axis) {
  struct Key {
    u16 layer; u8 axis;
    bool operator==(const Key& o) const { return layer == o.layer && axis == o.axis; }
  };
  struct Hash { size_t operator()(const Key& k) const { return (size_t)k.layer * 4u + k.axis; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  axis &= 3;
  Key key{(u16)std::max(layer, 0), (u8)axis};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  float b[6] = {7, 0, 7, 9, 16, 9};
  if (axis == 1) { b[0] = 0; b[1] = 7; b[2] = 7; b[3] = 16; b[4] = 9; b[5] = 9; }
  else if (axis == 2) { b[0] = 7; b[1] = 7; b[2] = 0; b[3] = 9; b[4] = 9; b[5] = 16; }
  auto vox = [](float a) { return (int)std::lround(a * 2.0); };
  int x0 = vox(b[0]), y0 = vox(b[1]), z0 = vox(b[2]), x1 = vox(b[3]), y1 = vox(b[4]), z1 = vox(b[5]);
  if (x1 <= x0) x1 = x0 + 1;
  if (y1 <= y0) y1 = y0 + 1;
  if (z1 <= z0) z1 = z0 + 1;
  VoxelGrid g(ivec3(x1, y1, z1));
  const int S = ts.size > 0 ? ts.size : 32;
  float ext[3] = {b[3] - b[0], b[4] - b[1], b[5] - b[2]};
  int L = (ext[1] >= ext[0] && ext[1] >= ext[2]) ? 1 : (ext[0] >= ext[2] ? 0 : 2);
  int loV[3] = {x0, y0, z0};
  int hiV[3] = {x1, y1, z1};
  int crossAx = (L + 1) % 3;
  int nAcross = std::max(1, hiV[crossAx] - loV[crossAx]);
  if (layer >= 0 && layer < ts.count) {
    for (int z = z0; z < z1; z++) for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
      int i[3] = {x, y, z};
      float u = (i[crossAx] - loV[crossAx] + 0.5f) / (float)nAcross * 2.f;
      float pix = b[L] + (i[L] - loV[L] + 0.5f) * 0.5f;
      float v16 = 16.f - pix;
      int col = clampv((int)std::floor(u / 16.f * S), 0, S - 1);
      int row = clampv((int)std::floor(v16 / 16.f * S), 0, S - 1);
      const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
      if (p[3] < 128) continue;
      g.set(x, y, z, voxRGBA(p[0], p[1], p[2]));
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// Redstone dust. The line and the dot are grey, and the power paints them the red the chunk shader uses
// (the 204/255 of the tint times the extra 1.25 cancel). Arms are north, east, south, west. A link that
// climbs sits on the cell face. The flat dust sits at the slab drop, a little above the floor.
const VoxelMeshGL* dustMesh(const TileSet& ts, int line, int dot, u8 mask, int power, float sink) {
  struct Key {
    u16 line, dot; u8 mask, power; i8 sink;
    bool operator==(const Key& o) const { return line == o.line && dot == o.dot && mask == o.mask && power == o.power && sink == o.sink; }
  };
  struct Hash {
    size_t operator()(const Key& k) const {
      return ((((size_t)k.line * 131u + k.dot) * 131u + k.mask) * 131u + k.power) * 131u + (u8)k.sink;
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
  Key key; key.line = (u16)std::max(line, 0); key.dot = (u16)std::max(dot, 0); key.mask = mask; key.power = (u8)(power & 15); key.sink = (i8)std::lround(sink);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  const int y0 = (int)std::floor((sink / 16.f + 0.016f) * (float)S);
  const bool up0 = (mask & 16) != 0, up1 = (mask & 32) != 0, up2 = (mask & 64) != 0, up3 = (mask & 128) != 0;
  const bool anyUp = up0 || up1 || up2 || up3;
  const int ny = anyUp ? S - y0 : 1;
  VoxelGrid g(ivec3(S, std::max(ny, 1), S));
  const float pf = (power & 15) / 15.f;
  const float tr = pf * 0.6f + (pf > 0.f ? 0.4f : 0.3f);
  const float tg = clampv(pf * pf * 0.7f - 0.5f, 0.f, 1.f);
  const float tb = clampv(pf * pf * 0.6f - 0.7f, 0.f, 1.f);
  auto paint = [&](int layer, float u, float v) -> u32 {
    if (layer < 0 || layer >= ts.count) return 0;
    int col = clampv((int)std::floor(u * (float)S), 0, S - 1);
    int row = clampv((int)std::floor(v * (float)S), 0, S - 1);
    const u8* p = ts.tile(layer) + ((size_t)row * (size_t)S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA((int)std::lround(p[0] * tr), (int)std::lround(p[1] * tg), (int)std::lround(p[2] * tb));
  };
  const bool a0 = (mask & 1) != 0, a1 = (mask & 2) != 0, a2 = (mask & 4) != 0, a3 = (mask & 8) != 0;
  const bool straight = (a0 && a2 && !a1 && !a3) || (a1 && a3 && !a0 && !a2);
  for (int iz = 0; iz < S; iz++) for (int ix = 0; ix < S; ix++) {
    float fx = (ix + 0.5f) / (float)S, fz = (iz + 0.5f) / (float)S;
    u32 c = 0;
    auto take = [&](int layer, float u, float v) { if (u32 s = paint(layer, u, v)) c = s; };
    if (!straight) take(dot, fx, fz);
    if (a0 && fz < 0.5f) take(line, fx, fz);
    if (a2 && fz >= 0.5f) take(line, fx, fz);
    if (a1 && fx >= 0.5f) take(line, fz, fx);
    if (a3 && fx < 0.5f) take(line, fz, fx);
    if (c) g.set(ix, 0, iz, c);
  }
  if (anyUp) for (int iy = 0; iy < S; iy++) {
    float fy = (iy + 0.5f) / (float)S, u = 1.f - fy;
    for (int i = 0; i < S; i++) {
      float fa = (i + 0.5f) / (float)S;
      auto put = [&](int x, int z, float v) { if (u32 c = paint(line, u, v)) g.set(x, iy - y0, z, c); };
      if (up0) put(i, 0, fa);
      if (up2) put(i, S - 1, 1.f - fa);
      if (up1) put(S - 1, i, fa);
      if (up3) put(0, i, 1.f - fa);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// One voxel per texel of a picture, holes left empty. A flame glows. A pot plant does not.
const VoxelMeshGL* spritePlate(const TileSet& ts, int layer, bool glow) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<u32, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  u32 key = ((u32)std::max(layer, 0) << 1) | (glow ? 1u : 0u);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  VoxelGrid g(ivec3(S, S, 1));
  if (layer >= 0 && layer < ts.count) {
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      const u8* p = ts.tile(layer) + ((size_t)(S - 1 - y) * S + x) * 4;
      if (p[3] < 128) continue;
      g.set(x, y, 0, voxRGBA(p[0], p[1], p[2], glow ? VOXEL_GLOW : VOXEL_SOLID));
    }
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
  return spritePlate(ts, layer, true);
}

// One link strip of the chain picture. Strip 0 is columns 0-3, strip 1 is columns 3-6.
// A stretched chain repeats that picture through the extra half block above.
const VoxelMeshGL* chainStrip(const TileSet& ts, int layer, int strip, bool stretch) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<u32, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  u32 key = ((u32)std::max(layer, 0) << 2) | ((u32)(strip & 1) << 1) | (stretch ? 1u : 0u);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  const int W = std::max(1, S * 3 / 16);
  const int H = stretch ? S + S / 2 : S;
  VoxelGrid g(ivec3(W, H, 1));
  if (layer >= 0 && layer < ts.count) {
    const float u0 = (strip & 1) ? 3.f : 0.f;
    const float span = stretch ? 24.f : 16.f;
    for (int y = 0; y < H; y++) {
      float f = (y + 0.5f) / (float)H;
      float vw = (16.f - f * span) / 16.f;
      vw -= std::floor(vw);
      int row = clampv((int)std::floor(vw * S), 0, S - 1);
      for (int x = 0; x < W; x++) {
        float u = (u0 + (x + 0.5f) / (float)W * 3.f) / 16.f;
        int col = clampv((int)std::floor(u * S), 0, S - 1);
        const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
        if (p[3] < 128) continue;
        g.set(x, y, 0, voxRGBA(p[0], p[1], p[2]));
      }
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// A ladder's picture, one sixteenth thick, holes where the rungs are. Facing is a turn of this mesh.
const VoxelMeshGL* ladderMesh(const TileSet& ts, int layer) {
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
  const int D = std::max(1, S / 16);
  VoxelGrid g(ivec3(S, S, D));
  if (layer >= 0 && layer < ts.count) {
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      const u8* p = ts.tile(layer) + ((size_t)(S - 1 - y) * S + x) * 4;
      if (p[3] < 128) continue;
      u32 c = voxRGBA(p[0], p[1], p[2]);
      for (int z = 0; z < D; z++) g.set(x, y, z, c);
    }
  }
  VoxelMeshData md; buildVoxelMesh(g, md);
  auto mesh = std::make_unique<VoxelMeshGL>();
  mesh->upload(md);
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(layer, std::move(mesh));
  return out;
}

// The lever's stick: the same 0.4..0.6 strip the tilted box showed, two voxels per sixteenth, ten tall.
const VoxelMeshGL* leverStick(const TileSet& ts, int layer) {
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
  const int nx = 4, ny = 20, nz = 4;
  VoxelGrid g(ivec3(nx, ny, nz));
  bool hole = false;
  auto pix = [&](float u, float v) -> u32 {
    if (layer < 0 || layer >= ts.count) return 0;
    int col = clampv((int)std::floor(clampv(u, 0.f, 0.999f) * S), 0, S - 1);
    int row = clampv((int)std::floor(clampv(v, 0.f, 0.999f) * S), 0, S - 1);
    const u8* p = ts.tile(layer) + ((size_t)row * S + col) * 4;
    if (p[3] < 128) return 0;
    return voxRGBA(p[0], p[1], p[2]);
  };
  auto put = [&](int x, int y, int z, u32 c) { if (!c) { hole = true; return; } g.set(x, y, z, c); };
  for (int iy = 0; iy < ny; iy++) for (int iz = 0; iz < nz; iz++) {
    float v = 1.f - (iy + 0.5f) / ny;
    put(nx - 1, iy, iz, pix(0.6f - (iz + 0.5f) / nz * 0.2f, v));
    put(0, iy, iz, pix(0.4f + (iz + 0.5f) / nz * 0.2f, v));
  }
  for (int iy = 0; iy < ny; iy++) for (int ix = 0; ix < nx; ix++) {
    float v = 1.f - (iy + 0.5f) / ny;
    put(ix, iy, nz - 1, pix(0.4f + (ix + 0.5f) / nx * 0.2f, v));
    put(ix, iy, 0, pix(0.6f - (ix + 0.5f) / nx * 0.2f, v));
  }
  for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
    float u = 0.4f + (ix + 0.5f) / nx * 0.2f;
    float vcap = (nz - 0.5f - iz) / nz * 0.12f;
    put(ix, ny - 1, iz, pix(u, vcap));
  }
  if (!hole) {
    const u32 fill = voxRGBA(1, 1, 1);
    for (int z = 1; z < nz - 1; z++) for (int y = 1; y < ny - 1; y++) for (int x = 1; x < nx - 1; x++) g.set(x, y, z, fill);
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
        // A chain can sit on a corner and its top link can reach half a block higher.
        // A chain reaches above the cell. Redstone on a bottom slab drops half a block, and a link climbs the cell.
        float reach = c.flame == -7 ? 1.25f : c.flame == -8 ? 1.35f : 0.9f;
        if (!f.cam.frustum.sphere(cell + vec3(0.5f), reach)) continue;
        // -4 is a ladder. only keeps the 0..3 turn rotBox uses. The picture stands against the same wall.
        if (c.flame == -4) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* plate = layer >= 0 ? ladderMesh(ts, layer) : nullptr;
          if (!plate || !plate->ready()) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
          static const float TURN[4] = {0.f, -1.5707963f, 3.14159265f, 1.5707963f};
          const mat4 model = glm::translate(mat4(1), cell + vec3(0.5f))
              * glm::rotate(mat4(1), TURN[c.only & 3], vec3(0, 1, 0))
              * glm::translate(mat4(1), vec3(-0.5f))
              * glm::scale(mat4(1), vec3(1.f / PS));
          VR.draw(*plate, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          continue;
        }
        // -3 is a flower in a pot: two crossed pictures, coloured, standing in the pot's mouth.
        if (c.flame == -3) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* plate = layer >= 0 ? spritePlate(ts, layer, false) : nullptr;
          if (!plate || !plate->ready()) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
          VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
          const float yBase = cell.y + c.sink / 16.f;
          const vec3 up(0.f, 0.75f / PS, 0.f);
          auto cross = [&](vec3 along, vec3 from) {
            vec3 n = glm::normalize(glm::cross(along, vec3(0, 1, 0))) * (1.f / 32.f);
            mat4 m(1.f);
            m[0] = vec4(along / (float)PS, 0.f);
            m[1] = vec4(up, 0.f);
            m[2] = vec4(n, 0.f);
            m[3] = vec4(vec3(cell.x, yBase, cell.z) + from - n * 0.5f, 1.f);
            VR.draw(*plate, m, light);
          };
          cross(vec3(0.5f, 0.f, 0.5f), vec3(0.25f, 0.f, 0.25f));
          cross(vec3(0.5f, 0.f, -0.5f), vec3(0.25f, 0.f, 0.75f));
          continue;
        }
        // -5 is a lit campfire: four walls inset the way the chunk plates were. -6 is a fire block: two crosses.
        if (c.flame == -5 || c.flame == -6) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* plate = layer >= 0 ? flamePlate(ts, layer) : nullptr;
          if (!plate || !plate->ready()) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
          VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
          const vec3 base = cell + vec3(c.x0, c.sink / 16.f, c.z0);
          auto sheet = [&](vec3 along, vec3 from) {
            vec3 nrm = glm::cross(along, vec3(0, 1, 0));
            float nl = glm::length(nrm);
            if (nl < 1e-5f) return;
            nrm = nrm / nl * (1.f / 32.f);
            mat4 m(1.f);
            m[0] = vec4(along / (float)PS, 0.f);
            m[1] = vec4(0.f, 1.f / PS, 0.f, 0.f);
            m[2] = vec4(nrm, 0.f);
            m[3] = vec4(base + from - nrm * 0.5f, 1.f);
            VR.draw(*plate, m, light);
          };
          if (c.flame == -5) {
            sheet(vec3(1, 0, 0), vec3(0, 0, 0.08f));
            sheet(vec3(1, 0, 0), vec3(0, 0, 0.92f));
            sheet(vec3(0, 0, 1), vec3(0.08f, 0, 0));
            sheet(vec3(0, 0, 1), vec3(0.92f, 0, 0));
          } else {
            const float r = 0.42f;
            sheet(vec3(r * 2, 0, r * 2), vec3(0.5f - r, 0, 0.5f - r));
            sheet(vec3(-r * 2, 0, r * 2), vec3(0.5f + r, 0, 0.5f - r));
          }
          continue;
        }
        // -7 is a chain block: two crossed strips of the link picture, not a box of its empty middle.
        if (c.flame == -7) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const bool stretch = (c.only & 1) != 0;
          const VoxelMeshGL* a = layer >= 0 ? chainStrip(ts, layer, 0, stretch) : nullptr;
          const VoxelMeshGL* b = layer >= 0 ? chainStrip(ts, layer, 1, stretch) : nullptr;
          if ((!a || !a->ready()) && (!b || !b->ready())) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const int W = std::max(1, PS * 3 / 16);
          const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
          VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
          const vec3 base = cell + vec3(c.x0, c.sink / 16.f, c.z0);
          auto sheet = [&](const VoxelMeshGL* plate, vec3 along, vec3 from) {
            if (!plate || !plate->ready()) return;
            vec3 nrm = glm::cross(along, vec3(0, 1, 0));
            float nl = glm::length(nrm);
            if (nl < 1e-5f) return;
            nrm = nrm / nl * (1.f / 32.f);
            mat4 m(1.f);
            m[0] = vec4(along / (float)W, 0.f);
            m[1] = vec4(0.f, 1.f / (float)PS, 0.f, 0.f);
            m[2] = vec4(nrm, 0.f);
            m[3] = vec4(base + from - nrm * 0.5f, 1.f);
            VR.draw(*plate, m, light);
          };
          sheet(a, vec3(3.f / 16.f, 0.f, 3.f / 16.f), vec3(6.5f / 16.f, 0.f, 6.5f / 16.f));
          sheet(b, vec3(-3.f / 16.f, 0.f, 3.f / 16.f), vec3(9.5f / 16.f, 0.f, 6.5f / 16.f));
          continue;
        }
        // -8 is redstone dust. The flat part sits on the slab drop. A link up a block stays on the cell.
        if (c.flame == -8) {
          int line = c.tile < ts.count ? (int)c.tile : -1;
          int dot = c.top < ts.count ? (int)c.top : line;
          if (line >= 0 && anim[line] >= 0) line = anim[line];
          if (dot >= 0 && anim[dot] >= 0) dot = anim[dot];
          const VoxelMeshGL* mesh = line >= 0 ? dustMesh(ts, line, dot, c.only, c.faceRot & 15, c.sink) : nullptr;
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const int PS = ts.size > 0 ? ts.size : 32;
            const int y0 = (int)std::floor((c.sink / 16.f + 0.016f) * (float)PS);
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            const mat4 model = glm::translate(mat4(1), cell + vec3(0.f, y0 / (float)PS, 0.f)) * glm::scale(mat4(1), vec3(1.f / PS));
            VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          }
          continue;
        }
        int layer = c.tile < ts.count ? (int)c.tile : -1;
        i16 box[6] = {
          (i16)std::lround(c.x0), (i16)std::lround(c.y0), (i16)std::lround(c.z0),
          (i16)std::lround(c.x1), (i16)std::lround(c.y1), (i16)std::lround(c.z1)};
        int nx = 1, ny = 1, nz = 1;
        waxCounts(box, nx, ny, nz);
        int topL = c.top < ts.count ? (int)c.top : layer;
        int botL = c.bot < ts.count ? (int)c.bot : layer;
        int nxL = c.nx < ts.count ? (int)c.nx : layer;
        int pzL = c.pz < ts.count ? (int)c.pz : layer;
        int nzL = c.nz < ts.count ? (int)c.nz : layer;
        // A lit furnace's front is another layer each frame. The other faces stay put.
        auto useFrame = [&](int L) {
          if (L < 0 || L >= (int)anim.size()) return L;
          return anim[L] >= 0 ? anim[L] : L;
        };
        layer = useFrame(layer); topL = useFrame(topL); botL = useFrame(botL);
        nxL = useFrame(nxL); pzL = useFrame(pzL); nzL = useFrame(nzL);
        // flame <= -10 is a glowing crop (a lit repeater torch) with no flame plate. -2 is scaffolding.
        bool glow = c.flame <= -10;
        const VoxelMeshGL* mesh = c.flame == -2 ? (layer >= 0 ? scaffoldMesh(ts, layer, topL, botL) : nullptr)
                                                 : (layer >= 0 ? candleMesh(ts, layer, box, topL, botL, glow, nxL, pzL, nzL, c.only, c.faceRot) : nullptr);
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
    s.worldRenderer.forEachModelSection([&](vec3 origin, const std::vector<ModelSpot>& spots) {
      vec3 relO = f.cam.rel(glm::dvec3(origin));
      if (!f.cam.frustum.sphere(relO + vec3(8), 14.f)) return;
      for (const ModelSpot& m : spots) {
        vec3 cell = relO + vec3(m.cx, m.cy, m.cz);
        if (!f.cam.frustum.sphere(cell + vec3(0.5f), 1.4f)) continue;
        const VoxelMeshGL* mesh = propMesh(ts, m.model, m.facing);
        if (!mesh || !mesh->ready()) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        vec3 corner = cell + vec3(m.ox, m.sink / 16.f, m.oz);
        const mat4 model = glm::translate(mat4(1), corner) * glm::scale(mat4(1), vec3(1.f / 32.f));
        const u8 lit = w.getLight((int)origin.x + m.cx, (int)origin.y + m.cy, (int)origin.z + m.cz);
        VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
      }
    });
    s.worldRenderer.forEachRodSection([&](vec3 origin, const std::vector<RodSpot>& spots) {
      vec3 relO = f.cam.rel(glm::dvec3(origin));
      if (!f.cam.frustum.sphere(relO + vec3(8), 14.f)) return;
      for (const RodSpot& r : spots) {
        vec3 cell = relO + vec3(r.cx, r.cy, r.cz);
        if (!f.cam.frustum.sphere(cell + vec3(0.5f), 1.2f)) continue;
        int layer = r.tile < ts.count ? (int)r.tile : -1;
        const bool bolt = (r.axis & 0x80) != 0;
        const VoxelMeshGL* mesh = layer >= 0 ? (bolt ? lightningMesh(ts, layer, r.axis & 3) : rodMesh(ts, layer, r.axis)) : nullptr;
        if (!mesh || !mesh->ready()) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        vec3 corner = cell + vec3(r.ox, r.sink / 16.f, r.oz);
        const mat4 model = glm::translate(mat4(1), corner) * glm::scale(mat4(1), vec3(1.f / 32.f));
        const u8 lit = w.getLight((int)origin.x + r.cx, (int)origin.y + r.cy, (int)origin.z + r.cz);
        VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
      }
    });
    s.worldRenderer.forEachLeverSection([&](vec3 origin, const std::vector<LeverSpot>& spots) {
      vec3 relO = f.cam.rel(glm::dvec3(origin));
      if (!f.cam.frustum.sphere(relO + vec3(8), 14.f)) return;
      for (const LeverSpot& L : spots) {
        vec3 rel = relO + vec3(L.x, L.y, L.z);
        if (!f.cam.frustum.sphere(rel, 0.8f)) continue;
        int layer = L.tile < ts.count ? (int)L.tile : -1;
        const VoxelMeshGL* mesh = layer >= 0 ? leverStick(ts, layer) : nullptr;
        if (!mesh || !mesh->ready()) continue;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        mat4 basis(1.f);
        basis[0] = vec4(L.xx, L.xy, L.xz, 0.f);
        basis[1] = vec4(L.yx, L.yy, L.yz, 0.f);
        basis[2] = vec4(L.zx, L.zy, L.zz, 0.f);
        const mat4 model = glm::translate(mat4(1), rel) * basis
            * glm::translate(mat4(1), vec3(-1.f / 16.f, -5.f / 16.f, -1.f / 16.f)) * glm::scale(mat4(1), vec3(1.f / 32.f));
        const u8 lit = w.getLight((int)origin.x + L.cx, (int)origin.y + L.cy, (int)origin.z + L.cz);
        VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
      }
    });
    if (begun) VR.end();
  }
};

} // namespace

void registerTorchLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<TorchLayer>(); }); }

} // namespace dq
