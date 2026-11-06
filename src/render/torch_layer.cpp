// render/torch_layer.cpp — a torch or candle near the player is coloured voxels, the same crop of its picture
// the cutout box used to show. Flames keep changing frame. Past that distance the chunk mesh still draws them.
#include "client/session.h"
#include "client/app.h"
#include "assets/voxel.h"
#include "assets/voxel_paint.h"
#include "assets/tileset.h"
#include "world/shapes.h"
#include "common/noise.h"
#include <unordered_map>
#include <cmath>

namespace dq {

const VoxelMeshGL* doorPlateMesh(const TileSet& ts, int layer);
mat4 doorPanelModel(vec3 cellMin, int facing, bool trap, bool right, float open, int grid);

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
  u32 tint = 0;     // climate colour baked into alpha-253 texels (a chiseled grass corner). 0 leaves the picture.
  u16 season = 0;
  bool operator==(const WaxKey& o) const {
    return tile == o.tile && top == o.top && bot == o.bot && nx == o.nx && pz == o.pz && nz == o.nz && faceRot == o.faceRot
        && glow == o.glow && only == o.only && tint == o.tint && season == o.season
        && b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2] && b[3] == o.b[3] && b[4] == o.b[4] && b[5] == o.b[5];
  }
};
struct WaxHash {
  size_t operator()(const WaxKey& k) const {
    size_t h = k.tile * 131u + k.top;
    h = h * 131u + k.bot + (size_t)k.glow * 17u + (size_t)k.only;
    h = h * 131u + k.nx; h = h * 131u + k.pz; h = h * 131u + k.nz;
    h = h * 131u + k.faceRot;
    h = h * 131u + k.tint; h = h * 131u + k.season;
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
const VoxelMeshGL* candleMesh(const TileSet& ts, int layer, const i16 b[6], int topL, int botL, bool glow = false, int nxL = -1, int pzL = -1, int nzL = -1, u8 only = 63, u16 faceRot = 0, u32 tint = 0, u16 season = 0) {
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
  key.tint = tint; key.season = season;
  for (int i = 0; i < 6; i++) key.b[i] = b[i];
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  int nx, ny, nz; waxCounts(b, nx, ny, nz);
  const int S = ts.size > 0 ? ts.size : 32;
  const float x0 = b[0], y0 = b[1], z0 = b[2], x1 = b[3], y1 = b[4], z1 = b[5];
  VoxelGrid g(ivec3(nx, ny, nz));
  bool hole = false;
  // Alpha 253 takes the climate, then the season, the same mix the chunk picture uses. Alpha 255 (dirt on a
  // grass side) stays its own colour. A box with no climate keeps every texel as the picture painted it.
  vec3 tv(1.f);
  // Bit 24 marks a climate bake, so a black climate still multiplies. Callers that omit it leave the picture.
  const bool doTint = (key.tint & 0x01000000u) != 0;
  if (doTint) {
    tv = vec3((key.tint & 255) / 255.f, ((key.tint >> 8) & 255) / 255.f, ((key.tint >> 16) & 255) / 255.f);
    tv *= 1.3f;
    const float sa = (key.season & 15) / 8.f;
    if (sa > 0.f) {
      vec3 sc(((key.season >> 4) & 15) / 8.f, ((key.season >> 8) & 15) / 8.f, ((key.season >> 12) & 15) / 8.f);
      tv = glm::mix(tv, sc * (glm::dot(tv, vec3(0.299f, 0.587f, 0.114f)) * 1.6f), sa);
    }
  }
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
    if (!doTint) return voxRGBA(p[0], p[1], p[2], glow ? VOXEL_GLOW : VOXEL_SOLID);
    const float w = clampv((1.f - p[3] / 255.f) * 127.5f, 0.f, 1.f);
    auto mix1 = [&](int ch, float t) { return clampv((int)std::lround(ch * (1.f + (t - 1.f) * w)), 0, 255); };
    return voxRGBA(mix1(p[0], tv.r), mix1(p[1], tv.g), mix1(p[2], tv.b), glow ? VOXEL_GLOW : VOXEL_SOLID);
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

// The voxel whose top sits on the rail quad. lift is one sixteenth, plus the slab drop.
int railY0(float sink, int S) {
  const float liftS = (sink / 16.f + 1.f / 16.f) * (float)S;
  return (int)std::ceil(liftS - 1e-4f) - 1;
}

// A minecart rail. The same picture and the same turn as the flat quad. A slope climbs one block
// across the cell. The sheet is one voxel thick, with its top on the quad, including a slab drop.
const VoxelMeshGL* railMesh(const TileSet& ts, int layer, int shape, float sink) {
  struct Key {
    u16 layer; u8 shape; i8 sink;
    bool operator==(const Key& o) const { return layer == o.layer && shape == o.shape && sink == o.sink; }
  };
  struct Hash {
    size_t operator()(const Key& k) const {
      return ((size_t)k.layer * 131u + k.shape) * 131u + (u8)k.sink;
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
  if (shape < 0 || shape > 9) shape = 0;
  Key key; key.layer = (u16)std::max(layer, 0); key.shape = (u8)shape; key.sink = (i8)std::lround(sink);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  const float liftS = (sink / 16.f + 1.f / 16.f) * (float)S;
  const int y0 = railY0(sink, S);
  const bool slope = shape >= 2 && shape <= 5;
  VoxelGrid g(ivec3(S, slope ? S + 2 : 1, S));
  if (layer >= 0 && layer < ts.count) {
    for (int iz = 0; iz < S; iz++) for (int ix = 0; ix < S; ix++) {
      float fx = (ix + 0.5f) / (float)S, fz = (iz + 0.5f) / (float)S;
      float u, v, rise = 0.f;
      if (shape == 1 || shape == 2 || shape == 3) { u = fz; v = 1.f - fx; }
      else if (shape == 7) { u = 1.f - fx; v = fz; }
      else if (shape == 8) { u = 1.f - fx; v = 1.f - fz; }
      else if (shape == 9) { u = fx; v = 1.f - fz; }
      else { u = fx; v = fz; }
      if (shape == 2) rise = fx;
      else if (shape == 3) rise = 1.f - fx;
      else if (shape == 4) rise = 1.f - fz;
      else if (shape == 5) rise = fz;
      int col = clampv((int)std::floor(u * (float)S), 0, S - 1);
      int row = clampv((int)std::floor(v * (float)S), 0, S - 1);
      const u8* p = ts.tile(layer) + ((size_t)row * (size_t)S + col) * 4;
      if (p[3] < 128) continue;
      int iy = (int)std::ceil(liftS + rise * (float)S - 1e-4f) - 1 - y0;
      g.set(ix, iy, iz, voxRGBA(p[0], p[1], p[2]));
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

// A pot plant whose picture is marked for the climate. Alpha 253 takes the grass or leaf colour,
// then the season, the same mix the chunk picture uses. A stem stays its own colour. Holes stay empty.
const VoxelMeshGL* tintedSpritePlate(const TileSet& ts, int layer, u32 tint, u16 season) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<u64, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  const u64 key = (u64)std::max(layer, 0) | ((u64)(tint & 0xFFFFFFu) << 16) | ((u64)season << 40);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  VoxelGrid g(ivec3(S, S, 1));
  vec3 tv((tint & 255) / 255.f, ((tint >> 8) & 255) / 255.f, ((tint >> 16) & 255) / 255.f);
  tv *= 1.3f;
  const float sa = (season & 15) / 8.f;
  if (sa > 0.f) {
    vec3 sc(((season >> 4) & 15) / 8.f, ((season >> 8) & 15) / 8.f, ((season >> 12) & 15) / 8.f);
    tv = glm::mix(tv, sc * (glm::dot(tv, vec3(0.299f, 0.587f, 0.114f)) * 1.6f), sa);
  }
  if (layer >= 0 && layer < ts.count) {
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
      const u8* p = ts.tile(layer) + ((size_t)(S - 1 - y) * S + x) * 4;
      if (p[3] < 128) continue;
      const float w = clampv((1.f - p[3] / 255.f) * 127.5f, 0.f, 1.f);
      auto mix1 = [&](int c, float t) { return clampv((int)std::lround(c * (1.f + (t - 1.f) * w)), 0, 255); };
      g.set(x, y, 0, voxRGBA(mix1(p[0], tv.r), mix1(p[1], tv.g), mix1(p[2], tv.b)));
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

// A torchflower's cup, the same stem, leaves and five petals the chunk flower builds.
// The hash is the plant's own, so each one leans and turns the way that flower does.
const VoxelMeshGL* flowerCupMesh(const TileSet& ts, int layer, u32 hsh, int kind) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<u64, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  const u64 key = ((u64)(kind & 3) << 62) | ((u64)(u32)std::max(layer, 0) << 32) | hsh;
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  struct Px { int i, j, r, g, b; float lum; };
  std::vector<Px> px;
  if (layer >= 0 && layer < ts.count) {
    for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
      const u8* p = ts.tile(layer) + ((size_t)j * S + i) * 4;
      if (p[3] < 128) continue;
      px.push_back({i, j, p[0], p[1], p[2], 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2]});
    }
  }
  auto mesh = std::make_unique<VoxelMeshGL>();
  if (!px.empty()) {
    auto green = [](const Px& p) { return p.g > p.r + 10 && p.g >= p.b - 8; };
    auto common = [](const std::vector<Px>& list, Px& out) {
      std::unordered_map<int, int> n; int bn = 0; bool found = false;
      for (auto& p : list) { int k = (p.r >> 4) << 8 | (p.g >> 4) << 4 | (p.b >> 4); int c = ++n[k]; if (c > bn) { bn = c; out = p; found = true; } }
      return found;
    };
    std::vector<Px> G, F;
    for (auto& p : px) { if (green(p)) G.push_back(p); else if (p.lum > 25) F.push_back(p); }
    Px stem = px[0]; common(G, stem);
    Px leaf = stem; { float bl = -1; for (auto& p : G) if (p.lum > bl) { bl = p.lum; leaf = p; } }
    Px petal = stem; common(F, petal);
    std::vector<Px> far;
    for (auto& p : F) if (std::abs(p.r - petal.r) + std::abs(p.g - petal.g) + std::abs(p.b - petal.b) > 70) far.push_back(p);
    Px center = petal;
    if (!common(far, center)) { float bl = 1e9f; for (auto& p : F) if (p.lum < bl) { bl = p.lum; center = p; } }
    int top = S; for (auto& p : px) top = std::min(top, p.j);
    float headRow = (float)top + 2.f;
    if (!F.empty()) { float s = 0; for (auto& p : F) s += p.j; headRow = s / (float)F.size(); }
    float R = (float)S;
    float h = clampv((R - top) / R, 0.42f, 0.95f);
    float head = clampv(1.f - (headRow + 0.5f) / R, 0.3f, 0.9f);
    float r1 = (hsh & 255) / 255.f, r2 = ((hsh >> 8) & 255) / 255.f, r3 = ((hsh >> 16) & 255) / 255.f;
    const float TAU = 6.2831853f, PI = 3.14159265f;
    float cx = 0.5f + (r1 - 0.5f) * 0.3f, cz = 0.5f + (r2 - 0.5f) * 0.3f, rot = r3 * TAU;
    float H = h * (0.88f + r1 * 0.2f), hy = std::min(H, head + 0.08f) - 0.02f;
    if (hy < 0.12f) hy = 0.12f;
    float mid = hy * 0.5f, lean = 0.035f, lx = std::cos(rot) * lean, lz = std::sin(rot) * lean;
    float s1 = 1.f / 32.f;
    float hx = cx + lx * 0.6f, hz = cz + lz * 0.6f;
    VoxelPainter painter(ivec3(S, S * 2, S), vec3(0), 1.f / (float)S);
    auto paint = [](int r, int g, int b) {
      u32 c = voxRGBA(r, g, b);
      return [c](const PaintPt&) { return c; };
    };
    painter.box(vec3(cx - s1, 0.f, cz - s1), vec3(cx + s1, mid, cz + s1), paint(stem.r, stem.g, stem.b));
    painter.box(vec3(cx + lx * 0.5f - s1, mid - 0.01f, cz + lz * 0.5f - s1), vec3(cx + lx * 0.5f + s1, hy, cz + lz * 0.5f + s1), paint(stem.r, stem.g, stem.b));
    for (int k = 0; k < 2; k++) {
      float a = rot + k * PI + 0.6f, lyy = 0.06f + k * 0.08f, len = 0.17f + r2 * 0.06f;
      painter.rod(vec3(cx, lyy, cz), vec3(cx + std::cos(a) * len, lyy + 0.13f, cz + std::sin(a) * len),
                  vec2(0.075f, 0.02f), vec2(0.075f, 0.02f), paint(leaf.r, leaf.g, leaf.b));
    }
    if (kind == 0) {
      int n = 5 + (int)(hsh & 1);
      float plen = 0.13f + r3 * 0.03f;
      for (int k = 0; k < n; k++) {
        float a = rot + (float)k / n * TAU;
        painter.rod(vec3(hx + std::cos(a) * 0.025f, hy + 0.02f, hz + std::sin(a) * 0.025f),
                    vec3(hx + std::cos(a) * plen, hy + 0.06f, hz + std::sin(a) * plen),
                    vec2(0.085f, 0.022f), vec2(0.085f, 0.022f), paint(petal.r, petal.g, petal.b));
      }
      painter.box(vec3(hx - 0.04f, hy + 0.01f, hz - 0.04f), vec3(hx + 0.04f, hy + 0.07f, hz + 0.04f), paint(center.r, center.g, center.b));
    } else {
      painter.box(vec3(hx - 0.04f, hy - 0.01f, hz - 0.04f), vec3(hx + 0.04f, hy + 0.06f, hz + 0.04f), paint(center.r, center.g, center.b));
      for (int k = 0; k < 5; k++) {
        float a = rot + k / 5.f * TAU;
        painter.rod(vec3(hx + std::cos(a) * 0.03f, hy, hz + std::sin(a) * 0.03f),
                    vec3(hx + std::cos(a) * 0.085f, hy + 0.15f, hz + std::sin(a) * 0.085f),
                    vec2(0.075f, 0.025f), vec2(0.075f, 0.025f), paint(petal.r, petal.g, petal.b));
      }
    }
    VoxelMeshData md; buildVoxelMesh(painter.g, md);
    mesh->upload(md);
  }
  const VoxelMeshGL* out = mesh.get();
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// A flag, a banner, bunting, a washing line or a shop sign. The boxes are the ones the chunk mesh builds,
// two voxels to a sixteenth, so the wool and the rope keep their picture. A shop sign's chain is two
// link strips. The spare byte on each vertex is the same wind code, and the free edge still flaps.
const VoxelMeshGL* clothMesh(const TileSet& ts, BlockId id, u8 meta, float reachPx) {
  struct Key {
    u16 id; u8 meta; u8 reach;
    bool operator==(const Key& o) const { return id == o.id && meta == o.meta && reach == o.reach; }
  };
  struct Hash { size_t operator()(const Key& k) const { return ((size_t)k.id << 16) ^ ((size_t)k.meta << 8) ^ k.reach; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  const int reach = std::max(0, (int)std::lround(reachPx));
  Key key{id, meta, (u8)reach};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  auto mesh = std::make_unique<VoxelMeshGL>();
  const VoxelMeshGL* out = mesh.get();
  const BlockTraits& T = traits(id);
  const int rf = T.facingModel ? (int)(meta & 3) : 0;
  std::vector<ShapeBox> boxes;
  clothBoxes(id, meta, boxes);
  if (boxes.empty()) { cache.m.emplace(key, std::move(mesh)); return out; }
  const float reachF = (float)reach;
  static int chainTile = -2;
  if (chainTile == -2) chainTile = reg().tile("chain");
  const int BIAS = 32;
  auto gOf = [](float px) {
    int g = (int)std::lround(px * 2.f) + BIAS;
    if (g < 0) g = 0;
    if (g > 127) g = 127;
    return g;
  };
  struct Span { ShapeBox geo; int x0, y0, z0, x1, y1, z1; };
  std::vector<Span> spans;
  std::vector<ShapeBox> links;
  int xlo = 999, ylo = 999, zlo = 999, xhi = 0, yhi = 0, zhi = 0;
  for (auto& b0 : boxes) {
    ShapeBox b = b0;
    if (reachF > 0.f && b.kind == 1) { b.b[2] += reachF; b.b[5] += reachF; }
    rotBox(b, rf);
    if (chainTile >= 0 && b.side == chainTile) { links.push_back(b); continue; }
    Span s;
    s.geo = b;
    s.x0 = gOf(b.b[0]); s.x1 = gOf(b.b[3]);
    s.y0 = gOf(b.b[1] + b.lift); s.y1 = gOf(b.b[4] + b.lift);
    s.z0 = gOf(b.b[2]); s.z1 = gOf(b.b[5]);
    if (s.x1 <= s.x0) s.x1 = std::min(127, s.x0 + 1);
    if (s.y1 <= s.y0) s.y1 = std::min(127, s.y0 + 1);
    if (s.z1 <= s.z0) s.z1 = std::min(127, s.z0 + 1);
    xlo = std::min(xlo, s.x0); ylo = std::min(ylo, s.y0); zlo = std::min(zlo, s.z0);
    xhi = std::max(xhi, s.x1); yhi = std::max(yhi, s.y1); zhi = std::max(zhi, s.z1);
    spans.push_back(s);
  }
  const int DX = std::max(1, xhi - xlo), DY = std::max(1, yhi - ylo), DZ = std::max(1, zhi - zlo);
  if ((size_t)DX * DY * DZ > 200000u) { cache.m.emplace(key, std::move(mesh)); return out; }
  struct Cell { u32 col = 0; u8 sway = 0; };
  std::vector<Cell> grid((size_t)DX * DY * DZ);
  auto at = [&](int x, int y, int z) -> Cell& {
    return grid[((size_t)(z - zlo) * DY + (size_t)(y - ylo)) * DX + (size_t)(x - xlo)];
  };
  const int S = ts.size > 0 ? ts.size : 32;
  auto swayOf = [&](u8 kind, float vx, float vy, float vz) -> u8 {
    if (kind == 0) return 0;
    if (kind == 1) {
      float dd = (rf == 0 ? 1.f - vz : rf == 1 ? vx : rf == 2 ? vz : 1.f - vx) + reachF / 16.f;
      return (u8)(((rf & 1) ? 48 : 64) + std::lround(clampv(dd, 0.f, 1.f) * 15.f));
    }
    if (kind == 2) {
      float dd = (meta & 4) ? 0.45f + (1.f - vy) * 0.55f : (1.f - vy) * 0.45f;
      return (u8)(((rf & 1) ? 64 : 48) + std::lround(clampv(dd, 0.f, 1.f) * 15.f));
    }
    if (kind == 3) {
      float dd = clampv((14.f / 16.f - vy) / 0.55f, 0.f, 1.f) * 0.85f + 0.1f;
      return (u8)(((rf & 1) ? 64 : 48) + std::lround(dd * 15.f));
    }
    if (kind == 4) return (u8)(Sway::Hang + 1);
    return (u8)(Sway::Hang + std::lround(clampv((1.f - vy) * 1.1f, 0.f, 1.f) * 11.f));
  };
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
  auto pix = [](int g) { return (g + 0.5f - BIAS) / 2.f; };
  for (const Span& s : spans) {
    const ShapeBox& b = s.geo;
    const int side = b.side >= 0 ? b.side : 0;
    const int end = b.end >= 0 ? b.end : side;
    const float lift = b.lift;
    auto put = [&](int x, int y, int z, int f, float X, float Y, float Z) {
      if (x < xlo || y < ylo || z < zlo || x >= xhi || y >= yhi || z >= zhi) return;
      u32 c = sample(f == 2 || f == 3 ? end : side, f, X, Y, Z);
      if (!c) return;
      Cell& cell = at(x, y, z);
      cell.col = c;
      cell.sway = swayOf(b.kind, pix(x) / 16.f, pix(y) / 16.f, pix(z) / 16.f);
    };
    for (int iz = s.z0; iz < s.z1; iz++) for (int iy = s.y0; iy < s.y1; iy++) {
      float Y = pix(iy) - lift, Z = pix(iz);
      put(s.x1 - 1, iy, iz, 0, b.b[3], Y, Z);
      put(s.x0, iy, iz, 1, b.b[0], Y, Z);
    }
    for (int iz = s.z0; iz < s.z1; iz++) for (int ix = s.x0; ix < s.x1; ix++) {
      float X = pix(ix), Z = pix(iz);
      put(ix, s.y1 - 1, iz, 2, X, b.b[4], Z);
      put(ix, s.y0, iz, 3, X, b.b[1], Z);
    }
    for (int iy = s.y0; iy < s.y1; iy++) for (int ix = s.x0; ix < s.x1; ix++) {
      float X = pix(ix), Y = pix(iy) - lift;
      put(ix, iy, s.z1 - 1, 4, X, Y, b.b[5]);
      put(ix, iy, s.z0, 5, X, Y, b.b[2]);
    }
  }
  static const int C[6][4][3] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
  };
  static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  auto solid = [&](int x, int y, int z) {
    if (x < xlo || y < ylo || z < zlo || x >= xhi || y >= yhi || z >= zhi) return false;
    return at(x, y, z).col != 0;
  };
  VoxelMeshData md;
  for (int z = zlo; z < zhi; z++) for (int y = ylo; y < yhi; y++) for (int x = xlo; x < xhi; x++) {
    const Cell& cell = at(x, y, z);
    if (!cell.col) continue;
    for (int f = 0; f < 6; f++) {
      if (solid(x + N[f][0], y + N[f][1], z + N[f][2])) continue;
      for (int k = 0; k < 4; k++) {
        VoxelVertex vv;
        vv.x = (u16)(x + C[f][k][0]);
        vv.y = (u16)(y + C[f][k][1]);
        vv.z = (u16)(z + C[f][k][2]);
        vv.nao = (u8)(f | (3 << 3));
        vv.pad = cell.sway;
        vv.rgba = cell.col;
        md.verts.push_back(vv);
      }
    }
  }
  // A chain box is two crossed strips. A thin box of that picture shows only the empty middle.
  for (const ShapeBox& link : links) {
    float ext[3] = {link.b[3] - link.b[0], link.b[4] - link.b[1], link.b[5] - link.b[2]};
    int ax = ext[1] >= ext[0] && ext[1] >= ext[2] ? 1 : ext[0] >= ext[2] ? 0 : 2;
    int p = (ax + 1) % 3, q = (ax + 2) % 3;
    float b0[3] = {link.b[0], link.b[1], link.b[2]}, b1[3] = {link.b[3], link.b[4], link.b[5]};
    float cp = (b0[p] + b1[p]) * 0.5f, cq = (b0[q] + b1[q]) * 0.5f;
    float a0 = b0[ax], a1 = b1[ax];
    if (a1 < a0) std::swap(a0, a1);
    if (a1 - a0 < 0.05f || chainTile < 0 || chainTile >= ts.count) continue;
    const float hw = 1.5f;
    const float vbase = std::floor(a0 / 16.f) * 16.f;
    const int W = std::max(1, S * 3 / 16);
    const int H = std::max(1, (int)std::lround((a1 - a0) * (float)S / 16.f));
    for (int strip = 0; strip < 2; strip++) {
      const float sq = strip ? -1.f : 1.f;
      const float u0 = strip ? 3.f : 0.f;
      const int face = strip ? 0 : 4;
      for (int iy = 0; iy < H; iy++) for (int ix = 0; ix < W; ix++) {
        const float along0 = a0 + (float)iy / H * (a1 - a0);
        const float along1 = a0 + (float)(iy + 1) / H * (a1 - a0);
        const float alongC = (along0 + along1) * 0.5f;
        const float t0 = (float)ix / W * 2.f - 1.f;
        const float t1 = (float)(ix + 1) / W * 2.f - 1.f;
        float u = (u0 + (ix + 0.5f) / (float)W * 3.f) / 16.f;
        float uvY = (16.f - (alongC - vbase)) / 16.f;
        uvY -= std::floor(uvY);
        int col = clampv((int)std::floor(clampv(u, 0.f, 1.f) * S), 0, S - 1);
        int row = clampv((int)std::floor(clampv(uvY, 0.f, 0.999f) * S), 0, S - 1);
        const u8* px = ts.tile(chainTile) + ((size_t)row * S + col) * 4;
        if (px[3] < 128) continue;
        const u32 rgba = voxRGBA(px[0], px[1], px[2]);
        auto atP = [&](float along, float t, float* P) {
          P[ax] = along;
          P[p] = cp + t * hw;
          P[q] = cq + t * hw * sq;
        };
        float P0[3], P1[3], P2[3], P3[3];
        atP(along0, t0, P0); atP(along0, t1, P1); atP(along1, t1, P2); atP(along1, t0, P3);
        float mid[3] = {(P0[0] + P2[0]) * 0.5f, (P0[1] + P2[1]) * 0.5f, (P0[2] + P2[2]) * 0.5f};
        const u8 sway = swayOf(link.kind, mid[0] / 16.f, (mid[1] + link.lift) / 16.f, mid[2] / 16.f);
        float* cs[4] = {P0, P1, P2, P3};
        auto vert = [&](const float* c) {
          VoxelVertex vv;
          vv.x = (u16)gOf(c[0]); vv.y = (u16)gOf(c[1]); vv.z = (u16)gOf(c[2]);
          vv.nao = (u8)(face | (3 << 3));
          vv.pad = sway;
          vv.rgba = rgba;
          md.verts.push_back(vv);
        };
        for (int k = 0; k < 4; k++) vert(cs[k]);
        for (int k = 3; k >= 0; k--) vert(cs[k]);
      }
    }
  }
  mesh->upload(md);
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// A hanging lantern. The lamp is the same coloured boxes a standing one uses. The chain is two
// crossed strips of the link picture, and each voxel keeps the hang the chunk shader used.
const VoxelMeshGL* lanternHangMesh(const TileSet& ts, int model, u8 meta, bool stretch, float liftPx) {
  struct Key {
    u16 model; u8 extra; u8 stretch; i8 lift;
    bool operator==(const Key& o) const { return model == o.model && extra == o.extra && stretch == o.stretch && lift == o.lift; }
  };
  struct Hash { size_t operator()(const Key& k) const { return ((size_t)k.model << 16) ^ ((size_t)k.extra << 8) ^ ((size_t)k.stretch << 7) ^ (u8)k.lift; } };
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<Key, std::unique_ptr<VoxelMeshGL>, Hash> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  if (model < 0) return nullptr;
  const int extra = (meta >> 2) & 31;
  int lift = (int)std::lround(liftPx);
  if (lift < -32) lift = -32;
  if (lift > 32) lift = 32;
  Key key{(u16)model, (u8)extra, stretch ? (u8)1 : (u8)0, (i8)lift};
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  auto mesh = std::make_unique<VoxelMeshGL>();
  const VoxelMeshGL* out = mesh.get();
  const std::vector<ShapeBox>* src = modelShape(model);
  if (!src || src->empty()) { cache.m.emplace(key, std::move(mesh)); return out; }
  static int chainTile = -2;
  if (chainTile == -2) chainTile = reg().tile("chain");
  const float liftF = (float)lift;
  auto hangSway = [&](float pixY) -> u8 {
    float dd = clampv(1.f - (pixY + liftF) / 16.f, 0.f, 1.f);
    return (u8)(Sway::Hang + std::lround(dd * 11.f));
  };
  auto vox = [](float a) {
    int v = (int)std::lround(a * 2.f);
    if (v < 0) v = 0;
    if (v > 48) v = 48;
    return v;
  };
  int gx = 1, gy = 1, gz = 1;
  bool anyBody = false;
  for (const ShapeBox& b0 : *src) {
    if (!b0.only) continue;
    if (chainTile >= 0 && b0.side == chainTile) continue;
    anyBody = true;
    gx = std::max(gx, vox(b0.b[3]));
    gy = std::max(gy, vox(b0.b[4]));
    gz = std::max(gz, vox(b0.b[5]));
  }
  VoxelGrid g(ivec3(gx, gy, gz));
  const int S = ts.size > 0 ? ts.size : 32;
  if (anyBody) {
    for (const ShapeBox& b : *src) {
      if (!b.only) continue;
      if (chainTile >= 0 && b.side == chainTile) continue;
      int x0 = vox(b.b[0]), y0 = vox(b.b[1]), z0 = vox(b.b[2]);
      int x1 = vox(b.b[3]), y1 = vox(b.b[4]), z1 = vox(b.b[5]);
      if (x1 <= x0) x1 = x0 + 1;
      if (y1 <= y0) y1 = y0 + 1;
      if (z1 <= z0) z1 = z0 + 1;
      if (x0 >= gx || y0 >= gy || z0 >= gz) continue;
      x1 = std::min(x1, gx); y1 = std::min(y1, gy); z1 = std::min(z1, gz);
      const int side = b.side >= 0 ? b.side : 0;
      const int end = b.end >= 0 ? b.end : side;
      auto sample = [&](int f, float X, float Y, float Z) -> u32 {
        int tile = (f == 2 || f == 3) ? end : side;
        if (tile < 0 || tile >= ts.count) return 0;
        vec2 st = shapeST(f, X, Y, Z);
        st.x = clampv(st.x, 0.f, 16.f); st.y = clampv(st.y, 0.f, 16.f);
        int col = clampv((int)std::floor(st.x / 16.f * S), 0, S - 1);
        int row = clampv((int)std::floor(st.y / 16.f * S), 0, S - 1);
        const u8* p = ts.tile(tile) + ((size_t)row * S + col) * 4;
        if (p[3] < 128) return 0;
        return voxRGBA(p[0], p[1], p[2]);
      };
      const float X0 = b.b[0], Y0 = b.b[1], Z0 = b.b[2], X1 = b.b[3], Y1 = b.b[4], Z1 = b.b[5];
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
        for (int z = z0 + 1; z < z1 - 1; z++) for (int y = y0 + 1; y < y1 - 1; y++) for (int x = x0 + 1; x < x1 - 1; x++)
          if (!g.at(x, y, z)) g.set(x, y, z, fill);
      }
    }
  }
  static const int C[6][4][3] = {
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
    {{1, 0, 1}, {0, 0, 1}, {0, 0, 0}, {1, 0, 0}},
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
  };
  static const int N[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  VoxelMeshData md;
  auto push = [&](int x, int y, int z, int f, int k, u8 sway, u32 col) {
    VoxelVertex vv;
    vv.x = (u16)(x + C[f][k][0]);
    vv.y = (u16)(y + C[f][k][1]);
    vv.z = (u16)(z + C[f][k][2]);
    vv.nao = (u8)(f | (3 << 3));
    vv.pad = sway;
    vv.rgba = col;
    md.verts.push_back(vv);
  };
  if (anyBody) {
    for (int z = 0; z < gz; z++) for (int y = 0; y < gy; y++) for (int x = 0; x < gx; x++) {
      const u32 col = g.at(x, y, z);
      if (!col) continue;
      const u8 sway = hangSway((y + 0.5f) * 0.5f);
      for (int f = 0; f < 6; f++) {
        if (g.solid(x + N[f][0], y + N[f][1], z + N[f][2])) continue;
        for (int k = 0; k < 4; k++) push(x, y, z, f, k, sway, col);
      }
    }
  }
  auto gOf = [](float px) {
    int v = (int)std::lround(px * 2.f);
    if (v < 0) v = 0;
    if (v > 4000) v = 4000;
    return v;
  };
  auto emitLinks = [&](float x0, float y0, float z0, float x1, float y1, float z1, int tile) {
    if (tile < 0 || tile >= ts.count) return;
    float ext[3] = {x1 - x0, y1 - y0, z1 - z0};
    int ax = ext[1] >= ext[0] && ext[1] >= ext[2] ? 1 : ext[0] >= ext[2] ? 0 : 2;
    int p = (ax + 1) % 3, q = (ax + 2) % 3;
    float b0[3] = {x0, y0, z0}, b1[3] = {x1, y1, z1};
    float cp = (b0[p] + b1[p]) * 0.5f, cq = (b0[q] + b1[q]) * 0.5f;
    float a0 = b0[ax], a1 = b1[ax];
    if (a1 < a0) std::swap(a0, a1);
    if (a1 - a0 < 0.05f) return;
    const float hw = 1.5f;
    const float vbase = std::floor(a0 / 16.f) * 16.f;
    const int W = std::max(1, S * 3 / 16);
    const int H = std::max(1, (int)std::lround((a1 - a0) * S / 16.f));
    for (int strip = 0; strip < 2; strip++) {
      const float sq = strip ? -1.f : 1.f;
      const float u0 = strip ? 3.f : 0.f;
      const int face = strip ? 0 : 4;
      for (int iy = 0; iy < H; iy++) for (int ix = 0; ix < W; ix++) {
        const float along0 = a0 + (float)iy / H * (a1 - a0);
        const float along1 = a0 + (float)(iy + 1) / H * (a1 - a0);
        const float alongC = (along0 + along1) * 0.5f;
        const float t0 = (float)ix / W * 2.f - 1.f;
        const float t1 = (float)(ix + 1) / W * 2.f - 1.f;
        float u = (u0 + (ix + 0.5f) / W * 3.f) / 16.f;
        float uvY = (16.f - (alongC - vbase)) / 16.f;
        uvY -= std::floor(uvY);
        int col = clampv((int)std::floor(clampv(u, 0.f, 1.f) * S), 0, S - 1);
        int row = clampv((int)std::floor(clampv(uvY, 0.f, 0.999f) * S), 0, S - 1);
        const u8* px = ts.tile(tile) + ((size_t)row * S + col) * 4;
        if (px[3] < 128) continue;
        const u32 rgba = voxRGBA(px[0], px[1], px[2]);
        float py = ax == 1 ? alongC : (b0[1] + b1[1]) * 0.5f;
        const u8 sway = hangSway(py);
        auto atP = [&](float along, float t, int& ox, int& oy, int& oz) {
          float P[3];
          P[ax] = along;
          P[p] = cp + t * hw;
          P[q] = cq + t * hw * sq;
          ox = gOf(P[0]); oy = gOf(P[1]); oz = gOf(P[2]);
        };
        int c0[3], c1[3], c2[3], c3[3];
        atP(along0, t0, c0[0], c0[1], c0[2]);
        atP(along0, t1, c1[0], c1[1], c1[2]);
        atP(along1, t1, c2[0], c2[1], c2[2]);
        atP(along1, t0, c3[0], c3[1], c3[2]);
        const int* cs[4] = {c0, c1, c2, c3};
        auto vert = [&](const int* c) {
          VoxelVertex vv;
          vv.x = (u16)c[0]; vv.y = (u16)c[1]; vv.z = (u16)c[2];
          vv.nao = (u8)(face | (3 << 3));
          vv.pad = sway;
          vv.rgba = rgba;
          md.verts.push_back(vv);
        };
        for (int k = 0; k < 4; k++) vert(cs[k]);
        for (int k = 3; k >= 0; k--) vert(cs[k]);
      }
    }
  };
  for (const ShapeBox& b0 : *src) {
    if (!b0.only || chainTile < 0 || b0.side != chainTile) continue;
    float y1 = b0.b[4];
    const bool topLink = b0.b[4] >= 15.5f;
    if (stretch && topLink && !extra) y1 += 8.f;
    emitLinks(b0.b[0], b0.b[1], b0.b[2], b0.b[3], y1, b0.b[5], chainTile);
    if (!extra) continue;
    for (float a = 16.f; a < 16.f + extra * 8.f; a += 16.f) {
      float s0 = a, s1 = std::min(16.f + extra * 8.f, a + 16.f);
      if (stretch && s1 >= 16.f + extra * 8.f - 0.01f) s1 += 8.f;
      emitLinks(b0.b[0], s0, b0.b[2], b0.b[3], s1, b0.b[5], chainTile);
    }
  }
  mesh->upload(md);
  cache.m.emplace(key, std::move(mesh));
  return out;
}

// Sugar cane near you: the same stalks and leaves the chunk picture builds, one voxel per step of that
// picture. Alpha 253 keeps the grass colour of this shore, then the season. A hole in the picture stays a hole.
const VoxelMeshGL* sugarCaneMesh(const TileSet& ts, int layer, int sx, int sz, bool top, bool tinted, u32 tint, u16 season) {
  struct Cache {
    const u8* px = nullptr; int count = -1, size = -1;
    std::unordered_map<u64, std::unique_ptr<VoxelMeshGL>> m;
  };
  static Cache cache;
  if (cache.px != ts.rgba.data() || cache.count != ts.count || cache.size != ts.size) {
    cache.m.clear(); cache.px = ts.rgba.data(); cache.count = ts.count; cache.size = ts.size;
  }
  const u64 key = (u64)(sx & 15) | ((u64)(sz & 15) << 4) | ((u64)top << 8) | ((u64)tinted << 9)
      | ((u64)std::max(layer, 0) << 10) | ((u64)(tint & 0xFFFFFFu) << 22) | ((u64)season << 46);
  auto it = cache.m.find(key);
  if (it != cache.m.end()) return it->second.get();
  const int S = ts.size > 0 ? ts.size : 32;
  const int BIAS = 12;
  const int N = S + BIAS * 2;
  VoxelGrid g(ivec3(N, S, N));
  auto meshOut = [&]() {
    VoxelMeshData md; buildVoxelMesh(g, md);
    auto mesh = std::make_unique<VoxelMeshGL>();
    mesh->upload(md);
    const VoxelMeshGL* out = mesh.get();
    cache.m.emplace(key, std::move(mesh));
    return out;
  };
  if (layer < 0 || layer >= ts.count) return meshOut();
  vec3 tv(1.f);
  if (tinted) {
    tv = vec3((tint & 255) / 255.f, ((tint >> 8) & 255) / 255.f, ((tint >> 16) & 255) / 255.f) * 1.3f;
    const float sa = (season & 15) / 8.f;
    if (sa > 0.f) {
      vec3 sc(((season >> 4) & 15) / 8.f, ((season >> 8) & 15) / 8.f, ((season >> 12) & 15) / 8.f);
      tv = glm::mix(tv, sc * (glm::dot(tv, vec3(0.299f, 0.587f, 0.114f)) * 1.6f), sa);
    }
  }
  auto colour = [&](int r, int gg, int b, int a) -> u32 {
    if (a < 128) return 0;
    if (tinted) {
      const float w = clampv((1.f - a / 255.f) * 127.5f, 0.f, 1.f);
      auto mix1 = [&](int c, float t) { return clampv((int)std::lround(c * (1.f + (t - 1.f) * w)), 0, 255); };
      r = mix1(r, tv.r); gg = mix1(gg, tv.g); b = mix1(b, tv.b);
    }
    return voxRGBA(r, gg, b);
  };
  const u8* base = ts.tile(layer);
  const int R = S;
  std::vector<int> colCount(R, 0);
  struct Pix { int i, j, r, g, b, a; };
  std::vector<Pix> px;
  px.reserve(R);
  for (int j = 0; j < R; j++) for (int i = 0; i < R; i++) {
    const u8* p = base + ((size_t)j * R + i) * 4;
    if (p[3] < 128) continue;
    colCount[i]++;
    px.push_back({i, j, p[0], p[1], p[2], p[3]});
  }
  std::vector<std::pair<int, int>> stalks;
  for (int i = 0; i < R; i++) {
    if (colCount[i] < R * 0.6f) continue;
    if (!stalks.empty() && stalks.back().second == i - 1) stalks.back().second = i;
    else stalks.push_back({i, i});
  }
  struct Leaf { int j, r, g, b, a, k, side, out; };
  std::vector<Leaf> leaves;
  const int near = 3 * R / 16;
  auto inStalk = [&](int i) {
    for (size_t n = 0; n < stalks.size(); n++) if (i >= stalks[n].first && i <= stalks[n].second) return (int)n;
    return -1;
  };
  for (const Pix& p : px) {
    if (inStalk(p.i) >= 0 || stalks.empty()) continue;
    int k = 0, bd = 99;
    for (size_t n = 0; n < stalks.size(); n++) {
      int d = std::min(std::abs(p.i - stalks[n].first), std::abs(p.i - stalks[n].second));
      if (d < bd) { bd = d; k = (int)n; }
    }
    if (bd <= near) leaves.push_back({p.j, p.r, p.g, p.b, p.a, k, p.i > stalks[k].second ? 1 : -1, bd});
  }
  struct Cap { int r, g, b, a; bool ok = false; };
  std::vector<Cap> caps;
  for (auto& s : stalks) {
    const Pix* cap = nullptr;
    for (const Pix& p : px) if (p.i == s.first && p.j == R / 2) { cap = &p; break; }
    if (!cap) for (const Pix& p : px) if (p.i == s.first) { cap = &p; break; }
    if (!cap && !px.empty()) cap = &px[0];
    caps.push_back(cap ? Cap{cap->r, cap->g, cap->b, cap->a, true} : Cap{});
  }
  if (stalks.size() > 4) stalks.resize(4);
  const u32 h = hash32((i32)(sx & 15), 0, (i32)(sz & 15), 23);
  const float turn = (h & 3) * 3.14159265f / 2.f;
  static const float spots[4][2] = {{0.3f, 0.3f}, {0.7f, 0.38f}, {0.46f, 0.72f}, {0.74f, 0.76f}};
  const float Rf = (float)R;
  for (size_t k = 0; k < stalks.size(); k++) {
    const int c0 = stalks[k].first, c1 = stalks[k].second;
    const float* sp = spots[k % 4];
    const float jx = (((h >> (8 + k * 4)) & 15) / 15.f - 0.5f) * 0.08f;
    const float jz = (((h >> (10 + k * 4)) & 15) / 15.f - 0.5f) * 0.08f;
    const float ox = sp[0] - 0.5f + jx, oz = sp[1] - 0.5f + jz;
    const float rx = ox * std::cos(turn) - oz * std::sin(turn);
    const float rz = ox * std::sin(turn) + oz * std::cos(turn);
    const float cx = 0.5f + rx, cz = 0.5f + rz;
    const float hw = (c1 - c0 + 1) / Rf / 2.f;
    const float u0 = c0 / Rf, u1 = (c1 + 1) / Rf;
    const float span = std::max(hw * 2.f, 1e-4f);
    int x0 = (int)std::floor((cx - hw) * S) + BIAS;
    int x1 = (int)std::ceil((cx + hw) * S) + BIAS;
    int z0 = (int)std::floor((cz - hw) * S) + BIAS;
    int z1 = (int)std::ceil((cz + hw) * S) + BIAS;
    if (x1 <= x0) x1 = x0 + 1;
    if (z1 <= z0) z1 = z0 + 1;
    auto sample = [&](int row, float f) -> u32 {
      f = clampv(f, 0.f, 1.f);
      const int col = clampv((int)std::floor((u0 + f * (u1 - u0)) * R), 0, R - 1);
      const u8* p = base + ((size_t)row * R + col) * 4;
      return colour(p[0], p[1], p[2], p[3]);
    };
    for (int gy = 0; gy < S; gy++) {
      const float py = (gy + 0.5f) / (float)S;
      const int row = clampv((int)std::floor((1.f - py) * R), 0, R - 1);
      for (int gx = x0; gx < x1; gx++) {
        const float px = (gx - BIAS + 0.5f) / (float)S;
        if (u32 c = sample(row, (px - (cx - hw)) / span)) g.set(gx, gy, z1 - 1, c);
        if (u32 c = sample(row, ((cx + hw) - px) / span)) g.set(gx, gy, z0, c);
      }
      for (int gz = z0; gz < z1; gz++) {
        const float pz = (gz - BIAS + 0.5f) / (float)S;
        if (u32 c = sample(row, ((cz + hw) - pz) / span)) g.set(x1 - 1, gy, gz, c);
        if (u32 c = sample(row, (pz - (cz - hw)) / span)) g.set(x0, gy, gz, c);
      }
    }
    if (top && k < caps.size() && caps[k].ok) {
      const u32 cap = colour(caps[k].r, caps[k].g, caps[k].b, caps[k].a);
      if (cap) for (int gz = z0; gz < z1; gz++) for (int gx = x0; gx < x1; gx++) if (!g.at(gx, S - 1, gz)) g.set(gx, S - 1, gz, cap);
    }
    const float a = turn + k * 1.9f, dx = std::cos(a), dz = std::sin(a);
    for (const Leaf& lf : leaves) {
      if (lf.k != (int)k) continue;
      const u32 col = colour(lf.r, lf.g, lf.b, lf.a);
      if (!col) continue;
      const float yy = 1.f - (lf.j + 1) / Rf;
      const float o0 = hw + (lf.out - 1) / Rf, o1 = o0 + 1.2f / Rf, s = (float)lf.side, t = 0.6f / Rf;
      const float lx0 = cx + dx * s * o0, lz0 = cz + dz * s * o0;
      const float lx1 = cx + dx * s * o1, lz1 = cz + dz * s * o1;
      const float xa = std::min(lx0, lx1) - std::fabs(dz) * t, xb = std::max(lx0, lx1) + std::fabs(dz) * t;
      const float za = std::min(lz0, lz1) - std::fabs(dx) * t, zb = std::max(lz0, lz1) + std::fabs(dx) * t;
      int ix0 = (int)std::floor(std::min(xa, xb) * S) + BIAS, ix1 = (int)std::ceil(std::max(xa, xb) * S) + BIAS;
      int iy0 = (int)std::floor(yy * S), iy1 = (int)std::ceil((yy + 1.f / Rf) * S);
      int iz0 = (int)std::floor(std::min(za, zb) * S) + BIAS, iz1 = (int)std::ceil(std::max(za, zb) * S) + BIAS;
      if (ix1 <= ix0) ix1 = ix0 + 1;
      if (iy1 <= iy0) iy1 = iy0 + 1;
      if (iz1 <= iz0) iz1 = iz0 + 1;
      for (int z = iz0; z < iz1; z++) for (int y = iy0; y < iy1; y++) for (int x = ix0; x < ix1; x++) g.set(x, y, z, col);
    }
  }
  return meshOut();
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
        // A rail on a bottom slab drops the same way, and a slope climbs the cell.
        float reach = c.flame == -7 ? 1.25f : (c.flame == -8 || c.flame == -9 || c.flame == -14 || c.flame == -15 || c.flame == -16) ? 1.35f : 0.9f;
        if (c.flame == -17) reach = 1.35f + ((c.only >> 2) & 31) * 0.5f + ((c.faceRot & 1) ? 0.5f : 0.f);
        if (c.flame == -18) reach = 1.2f;
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
        // -3 is a plant in a pot: two crossed pictures. Grass and ferns carry this place's climate (only is 1,
        // nx and pz hold that colour) and take the season the chunk picture uses.
        if (c.flame == -3) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* plate = nullptr;
          if (layer >= 0 && c.only == 1) {
            const u32 col = (u32)c.nx | ((u32)c.pz << 16);
            const float sum = ((col & 255) + ((col >> 8) & 255) + ((col >> 16) & 255)) / 255.f;
            if (sum < 2.99f) {
              auto q8 = [](float v) { return (int)std::lround(clampv(v, 0.f, 1.f) * 8.f); };
              const u16 season = (u16)(q8(f.season.a) | (q8(f.season.r) << 4) | (q8(f.season.g) << 8) | (q8(f.season.b) << 12));
              plate = tintedSpritePlate(ts, layer, col, season);
            }
          }
          if (!plate) plate = layer >= 0 ? spritePlate(ts, layer, false) : nullptr;
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
        // -12 is an amethyst cluster, a bud, or a spore blossom. Crossed pictures, shifted and turned
        // like the chunk plant, and the whole shape takes the same wind step a plant quad does.
        if (c.flame == -12) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* plate = layer >= 0 ? spritePlate(ts, layer, false) : nullptr;
          if (!plate || !plate->ready()) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
          VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
          const float ang = c.y0, jx = c.x0, jz = c.z0, rad = 0.46f;
          const float cxp = 0.5f + jx, czp = 0.5f + jz;
          const int planes = c.only == 3 ? 3 : 2;
          const float step = planes == 3 ? 1.04719755f : 1.5707963f;
          const vec3 world = origin + vec3((float)c.cx + 0.5f, (float)c.cy, (float)c.cz + 0.5f);
          const float t = (float)std::fmod(f.time, 3600.0);
          const float gust = 0.6f + 0.4f * std::sin(t * 0.35f + world.x * 0.02f + world.z * 0.015f);
          const vec3 wave(
              (std::sin(t * 1.9f + world.x * 0.37f + world.z * 0.23f) + 0.4f * std::sin(t * 3.3f + world.x * 1.1f)) * 0.075f * f.wind * gust,
              0.f,
              std::cos(t * 1.5f + world.z * 0.31f + world.x * 0.11f) * 0.055f * f.wind * gust);
          const float yBase = cell.y + c.sink / 16.f;
          auto cross = [&](vec3 along, vec3 from) {
            vec3 n = glm::cross(along, vec3(0, 1, 0));
            float nl = glm::length(n);
            if (nl < 1e-5f) return;
            n = n / nl * (1.f / 32.f);
            mat4 m(1.f);
            m[0] = vec4(along / (float)PS, 0.f);
            m[1] = vec4(0.f, 1.f / (float)PS, 0.f, 0.f);
            m[2] = vec4(n, 0.f);
            m[3] = vec4(vec3(cell.x, yBase, cell.z) + from + wave - n * 0.5f, 1.f);
            VR.draw(*plate, m, light);
          };
          for (int k = 0; k < planes; k++) {
            float a = ang + k * step, ca = std::cos(a) * rad, sa = std::sin(a) * rad;
            cross(vec3(2.f * ca, 0.f, 2.f * sa), vec3(cxp - ca, 0.f, czp - sa));
          }
          continue;
        }
        // -13 is a torchflower cup, or a blue orchid fan when only is 0. The flower is coloured voxels and takes the plant's wind step.
        if (c.flame == -13) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const int wy = (int)origin.y + c.cy;
          const u32 hsh = hash32((i32)c.cx, (i32)wy, (i32)c.cz, 5);
          const int kind = c.only == 0 ? 0 : 1;
          const VoxelMeshGL* cup = layer >= 0 ? flowerCupMesh(ts, layer, hsh, kind) : nullptr;
          if (!cup || !cup->ready()) continue;
          if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
          const int PS = ts.size > 0 ? ts.size : 32;
          const u8 lit = w.getLight((int)origin.x + c.cx, wy, (int)origin.z + c.cz);
          VoxelLight light{(lit >> 4) / 15.f, (lit & 15) / 15.f};
          const vec3 world = origin + vec3((float)c.cx + 0.5f, (float)c.cy, (float)c.cz + 0.5f);
          const float t = (float)std::fmod(f.time, 3600.0);
          const float gust = 0.6f + 0.4f * std::sin(t * 0.35f + world.x * 0.02f + world.z * 0.015f);
          const vec3 wave(
              (std::sin(t * 1.9f + world.x * 0.37f + world.z * 0.23f) + 0.4f * std::sin(t * 3.3f + world.x * 1.1f)) * 0.075f * f.wind * gust,
              0.f,
              std::cos(t * 1.5f + world.z * 0.31f + world.x * 0.11f) * 0.055f * f.wind * gust);
          const float yBase = cell.y + c.sink / 16.f;
          mat4 m(1.f);
          m[0] = vec4(1.f / (float)PS, 0.f, 0.f, 0.f);
          m[1] = vec4(0.f, 1.f / (float)PS, 0.f, 0.f);
          m[2] = vec4(0.f, 0.f, 1.f / (float)PS, 0.f);
          m[3] = vec4(vec3(cell.x, yBase, cell.z) + wave, 1.f);
          VR.draw(*cup, m, light);
          continue;
        }
        // -14 is a door, -15 a trapdoor. The plate is coloured wood, and the swing turns this same plate.
        if (c.flame == -14 || c.flame == -15) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          const VoxelMeshGL* plate = layer >= 0 ? doorPlateMesh(ts, layer) : nullptr;
          if (plate && plate->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const int PS = ts.size > 0 ? ts.size : 32;
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            const bool trap = c.flame == -15;
            const bool right = (c.only & 8) != 0;
            const float open = (c.only & 4) ? 1.f : 0.f;
            const mat4 model = doorPanelModel(cell, c.only & 3, trap, right, open, PS);
            VR.draw(*plate, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          }
          continue;
        }
        // -16 is a flag, a banner, bunting, a washing line or a shop sign. tile is the block, only is its meta,
        // x0 is how far a flag reaches to its pole. The wind is in the mesh.
        if (c.flame == -16) {
          const VoxelMeshGL* mesh = clothMesh(ts, (BlockId)c.tile, c.only, c.x0);
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            const mat4 model = glm::translate(mat4(1), cell - vec3(1.f)) * glm::scale(mat4(1), vec3(1.f / 32.f));
            VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          }
          continue;
        }
        // -17 is a hanging lantern. tile is the block, only is its meta, faceRot bit 0 stretches the top link.
        // The chain and the lamp share one mesh, and the lower end still swings.
        if (c.flame == -17) {
          const BlockTraits& T = traits((BlockId)c.tile);
          const int mi = T.modelHang >= 0 ? T.modelHang : T.model;
          const VoxelMeshGL* mesh = lanternHangMesh(ts, mi, c.only, (c.faceRot & 1) != 0, c.sink);
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            const mat4 model = glm::translate(mat4(1), cell + vec3(c.x0, c.sink / 16.f, c.z0)) * glm::scale(mat4(1), vec3(1.f / 32.f));
            VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          }
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
        // -9 is a minecart rail. The sheet's top sits on the quad, and it climbs when the track does.
        if (c.flame == -9) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          const VoxelMeshGL* mesh = layer >= 0 ? railMesh(ts, layer, c.only & 15, c.sink) : nullptr;
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const int PS = ts.size > 0 ? ts.size : 32;
            const int y0 = railY0(c.sink, PS);
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            int bl = lit & 15;
            if (c.faceRot) bl = std::max(bl, 4);
            const mat4 model = glm::translate(mat4(1), cell + vec3(0.f, y0 / (float)PS, 0.f)) * glm::scale(mat4(1), vec3(1.f / PS));
            VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, bl / 15.f});
          }
          continue;
        }
        // -19 is a chiseled grass corner. sink is the climate colour (an exact integer), not a drop.
        // Marked texels take that colour and the season. Dirt on the sides stays its own colour.
        if (c.flame == -19) {
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
          auto useFrame = [&](int L) {
            if (L < 0 || L >= (int)anim.size()) return L;
            return anim[L] >= 0 ? anim[L] : L;
          };
          layer = useFrame(layer); topL = useFrame(topL); botL = useFrame(botL);
          nxL = useFrame(nxL); pzL = useFrame(pzL); nzL = useFrame(nzL);
          const u32 col = ((u32)std::lround(c.sink) & 0xFFFFFFu) | 0x01000000u;
          auto q8 = [](float v) { return (int)std::lround(clampv(v, 0.f, 1.f) * 8.f); };
          const u16 season = (u16)(q8(f.season.a) | (q8(f.season.r) << 4) | (q8(f.season.g) << 8) | (q8(f.season.b) << 12));
          const VoxelMeshGL* mesh = layer >= 0 ? candleMesh(ts, layer, box, topL, botL, false, nxL, pzL, nzL, c.only, c.faceRot, col, season) : nullptr;
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            float dx = std::max(c.x1 - c.x0, 0.01f), dy = std::max(c.y1 - c.y0, 0.01f), dz = std::max(c.z1 - c.z0, 0.01f);
            vec3 corner = cell + vec3(c.x0, c.y0, c.z0) / 16.f;
            const mat4 model = glm::translate(mat4(1), corner) * glm::scale(mat4(1), vec3(dx / (nx * 16.f), dy / (ny * 16.f), dz / (nz * 16.f)));
            VR.draw(*mesh, model, VoxelLight{(lit >> 4) / 15.f, (lit & 15) / 15.f});
          }
          continue;
        }
        // -18 is sugar cane. The stalks share a turn down the column. only bit 0 is the top of the plant,
        // and bit 1 carries this shore's grass colour in nx and pz.
        if (c.flame == -18) {
          int layer = c.tile < ts.count ? (int)c.tile : -1;
          if (layer >= 0 && anim[layer] >= 0) layer = anim[layer];
          bool tinted = (c.only & 2) != 0;
          u32 col = tinted ? ((u32)c.nx | ((u32)c.pz << 16)) : 0u;
          u16 season = 0;
          if (tinted) {
            const float sum = ((col & 255) + ((col >> 8) & 255) + ((col >> 16) & 255)) / 255.f;
            if (sum >= 2.99f) tinted = false;
            else {
              auto q8 = [](float v) { return (int)std::lround(clampv(v, 0.f, 1.f) * 8.f); };
              season = (u16)(q8(f.season.a) | (q8(f.season.r) << 4) | (q8(f.season.g) << 8) | (q8(f.season.b) << 12));
            }
          }
          const VoxelMeshGL* mesh = layer >= 0 ? sugarCaneMesh(ts, layer, c.cx, c.cz, (c.only & 1) != 0, tinted, col, season) : nullptr;
          if (mesh && mesh->ready()) {
            if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
            const int PS = ts.size > 0 ? ts.size : 32;
            const u8 lit = w.getLight((int)origin.x + c.cx, (int)origin.y + c.cy, (int)origin.z + c.cz);
            const mat4 model = glm::translate(mat4(1), cell + vec3(-12.f / PS, 0.f, -12.f / PS)) * glm::scale(mat4(1), vec3(1.f / PS));
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
        // -11 is a glowing crop (a lit repeater torch) with no flame plate. -2 is scaffolding.
        // Pictures from -12 downward are their own meshes and have already continued.
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

// A door's picture as one plate, three sixteenths thick, with a hole where the picture is clear.
// Shut, the wood stands on z 13..16. The swing turns this same plate.
const VoxelMeshGL* doorPlateMesh(const TileSet& ts, int layer) {
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
  const int D = std::max(1, S * 3 / 16);
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

// Shut pose in the cell, then the hinge, then the facing turn. `right` is meta bit 8:
// a door hinged on the right, or a trapdoor on the ceiling. `open` runs from 0 shut to 1 open.
mat4 doorPanelModel(vec3 cellMin, int facing, bool trap, bool right, float open, int grid) {
  const float S = (float)(grid > 0 ? grid : 32);
  auto aboutY = [](float px, float pz, float ang) {
    mat4 q = glm::translate(mat4(1), vec3(px, 0, pz) / 16.f);
    q = glm::rotate(q, ang, vec3(0, 1, 0));
    return glm::translate(q, -vec3(px, 0, pz) / 16.f);
  };
  auto aboutX = [](float py, float pz, float ang) {
    mat4 q = glm::translate(mat4(1), vec3(0, py, pz) / 16.f);
    q = glm::rotate(q, ang, vec3(1, 0, 0));
    return glm::translate(q, -vec3(0, py, pz) / 16.f);
  };
  mat4 pose(1.f);
  if (!trap) {
    pose[0] = vec4(1.f / S, 0, 0, 0);
    pose[1] = vec4(0, 1.f / S, 0, 0);
    pose[2] = vec4(0, 0, 1.f / S, 0);
    pose[3] = vec4(0, 0, 13.f / 16.f, 1);
  } else if (right) {
    pose[0] = vec4(1.f / S, 0, 0, 0);
    pose[1] = vec4(0, 0, -1.f / S, 0);
    pose[2] = vec4(0, 1.f / S, 0, 0);
    pose[3] = vec4(0, 13.f / 16.f, 1.f, 1);
  } else {
    pose[0] = vec4(1.f / S, 0, 0, 0);
    pose[1] = vec4(0, 0, -1.f / S, 0);
    pose[2] = vec4(0, 1.f / S, 0, 0);
    pose[3] = vec4(0, 0, 1.f, 1);
  }
  mat4 hinge(1.f);
  if (trap) hinge = right ? aboutX(14.5f, 1.5f, PI / 2 * open) : aboutX(1.5f, 1.5f, -PI / 2 * open);
  else hinge = aboutY(right ? 14.5f : 1.5f, 14.5f, (right ? -1.f : 1.f) * PI / 2 * open);
  const float turn = -(facing & 3) * PI / 2;
  return glm::translate(mat4(1), cellMin + vec3(0.5f, 0, 0.5f))
      * glm::rotate(mat4(1), turn, vec3(0, 1, 0))
      * glm::translate(mat4(1), vec3(-0.5f, 0, -0.5f))
      * hinge * pose;
}

void registerTorchLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<TorchLayer>(); }); }

} // namespace dq
