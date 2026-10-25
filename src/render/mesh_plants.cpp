// render/mesh_plants.cpp — plants with real depth (port of world/plants3d.js + emitCross of
// world/mesher.js + emitCrossFlat of game/perf.js), extended to the 32x32 micro-voxel grid:
//   * a plant's picture is pushed out into a voxel slab one to three micro-voxels thick each side,
//     as thick as each part is wide (a stem stays a thin stalk, a flower head is chunky), with the
//     steps between thicknesses filled in, all coloured pixel-for-voxel from the tile (any pack);
//   * flowers are built in 3D (stalk, leaves, a fan/cup/ball/bells of petals), short grass and ferns
//     are clumps of thick blades, sugar cane real stalks, kelp a swaying stem with leaves;
//   * vines and lily pads are voxel plates; settings.plantDetail decides near/all/off (flat planes).
#include "render/mesh_ctx.h"
#include "common/noise.h"
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace dq {

namespace {
struct PQ { float t, h, s, u, v, sh; };
struct PlantTemplate { std::vector<PQ> q; };   // 4 per quad, frame: t along 0..1, h up 0..1, s through (1/32 block), u v tile fractions
struct Px { int i, j; int r, g, b; float lum; vec2 uv; };
struct Sprite { int R = 32; std::vector<Px> px; };
struct FlowerLook { bool none = false; Px stem, leaf, petal, center; int kind = 0; float h = 0.5f, head = 0.5f; };   // kind 0 fan 1 cup 2 ball 3 bells
struct CaneLook { bool none = true; int R = 32; std::vector<std::pair<int, int>> stalks; struct Leaf { Px p; int k, side, out; }; std::vector<Leaf> leaves; std::vector<Px> cap; };
struct KelpLook { bool none = true; Px stem, leaf; };

std::mutex g_mx;
u64 g_gen = ~0ull;
std::unordered_map<int, std::shared_ptr<PlantTemplate>> g_tpl;
std::unordered_map<int, std::shared_ptr<FlowerLook>> g_flower;
std::unordered_map<int, std::shared_ptr<CaneLook>> g_cane;
std::unordered_map<int, std::shared_ptr<KelpLook>> g_kelp;
void checkGen(const MeshTiles* t) { if (t && t->gen != g_gen) { g_gen = t->gen; g_tpl.clear(); g_flower.clear(); g_cane.clear(); g_kelp.clear(); } }

Sprite spriteInfo(const MeshTiles& T, int tile) {
  Sprite s; s.R = T.size;
  if (tile < 0 || tile >= T.count) return s;
  for (int j = 0; j < s.R; j++) for (int i = 0; i < s.R; i++) {
    const u8* p = T.px(tile, i, j);
    if (p[3] < 128) continue;
    s.px.push_back({i, j, p[0], p[1], p[2], 0.3f * p[0] + 0.59f * p[1] + 0.11f * p[2], vec2((i + 0.5f) / s.R, (j + 0.5f) / s.R)});
  }
  return s;
}

// how thick each micro-voxel of the picture is: half-thickness in 1/32 block (1..3)
std::vector<u8> plantDepth(const std::vector<u8>& M, int S) {
  const int RAD = S * 6 / 16;
  std::vector<float> rad(S * S, 0), loc(S * S, 0); std::vector<u8> out(S * S, 0);
  auto at = [&](int i, int j) { return i >= 0 && j >= 0 && i < S && j < S && M[j * S + i]; };
  float k = S / 16.f;
  for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
    if (!at(i, j)) continue;
    float best = (float)RAD;
    for (int dj = -RAD; dj <= RAD; dj++) for (int di = -RAD; di <= RAD; di++) {
      float d = std::sqrt((float)(di * di + dj * dj));
      if (d < best && (di || dj) && !at(i + di, j + dj)) best = d;
    }
    rad[j * S + i] = std::max(0.65f * k, best - 0.35f * k);
  }
  for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
    float r = rad[j * S + i]; if (!r) continue;
    int e = (int)std::ceil(r + 0.5f);
    for (int dj = -e; dj <= e; dj++) for (int di = -e; di <= e; di++) {
      int ii = i + di, jj = j + dj;
      if (!at(ii, jj) || std::sqrt((float)(di * di + dj * dj)) > r + 0.5f) continue;
      if (loc[jj * S + ii] < r) loc[jj * S + ii] = r;
    }
  }
  for (int q = 0; q < S * S; q++) {
    if (!rad[q]) continue;
    float L = loc[q], r = rad[q], h = std::sqrt(std::max(0.f, L * L - (L - r) * (L - r))) * 0.8f;   // half-thickness in pixels of this picture
    out[q] = (u8)clampv((int)std::lround(h * 32.f / S), 1, 3);
  }
  return out;
}

std::shared_ptr<PlantTemplate> buildTemplate(const MeshTiles& T, int tile) {
  auto P = std::make_shared<PlantTemplate>();
  const int S = T.size;
  if (tile < 0 || tile >= T.count) return P;
  std::vector<u8> M(S * S);
  for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) M[j * S + i] = T.px(tile, i, j)[3] >= 128;
  std::vector<u8> D = plantDepth(M, S);
  auto th = [&](int i, int j) -> int { return (i < 0 || j < 0 || i >= S || j >= S) ? 0 : D[j * S + i]; };
  auto& Q = P->q;
  const float inv = 1.f / S;
  auto face = [&](PQ v[4], vec3 N) {
    vec3 a(v[0].t, v[0].h, v[0].s * 0.03f), b(v[1].t, v[1].h, v[1].s * 0.03f), c(v[2].t, v[2].h, v[2].s * 0.03f);
    bool ok = glm::dot(glm::cross(b - a, c - a), N) >= 0;
    if (ok) for (int k = 0; k < 4; k++) Q.push_back(v[k]);
    else { Q.push_back(v[0]); Q.push_back(v[3]); Q.push_back(v[2]); Q.push_back(v[1]); }
  };
  // the faces you look at: rectangles of pixels of one thickness, front and back
  std::vector<u8> done(S * S, 0);
  for (int j = 0; j < S; j++) for (int i = 0; i < S; i++) {
    int s = th(i, j); if (!s || done[j * S + i]) continue;
    int i1 = i; while (i1 + 1 < S && th(i1 + 1, j) == s && !done[j * S + i1 + 1]) i1++;
    int j1 = j;
    for (;;) { if (j1 + 1 >= S) break; bool ok = true; for (int k = i; k <= i1 && ok; k++) ok = th(k, j1 + 1) == s && !done[(j1 + 1) * S + k]; if (!ok) break; j1++; }
    for (int jj = j; jj <= j1; jj++) for (int k = i; k <= i1; k++) done[jj * S + k] = 1;
    float t0 = i * inv, t1 = (i1 + 1) * inv, h0 = 1 - (j1 + 1) * inv, h1 = 1 - j * inv;
    float u0 = i * inv, u1 = (i1 + 1) * inv, v0 = j * inv, v1 = (j1 + 1) * inv, fs = (float)s;
    PQ f[4] = {{t0, h0, fs, u0, v1, 235}, {t1, h0, fs, u1, v1, 235}, {t1, h1, fs, u1, v0, 235}, {t0, h1, fs, u0, v0, 235}};
    face(f, vec3(0, 0, 1));
    PQ bk[4] = {{t1, h0, -fs, u1, v1, 220}, {t0, h0, -fs, u0, v1, 220}, {t0, h1, -fs, u0, v0, 220}, {t1, h1, -fs, u1, v0, 220}};
    face(bk, vec3(0, 0, -1));
  }
  // the steps between columns (and the outline): from the thinner side's depth out to the thicker's
  for (int b = -1; b < S; b++) {
    int j = 0;
    while (j < S) {
      int L = th(b, j), R = th(b + 1, j);
      if (L == R) { j++; continue; }
      int j1 = j; while (j1 + 1 < S && th(b, j1 + 1) == L && th(b + 1, j1 + 1) == R) j1++;
      float t = (b + 1) * inv, hA = 1 - j * inv, hB = 1 - (j1 + 1) * inv, lo = (float)std::min(L, R), hi = (float)std::max(L, R);
      vec3 N = L > R ? vec3(1, 0, 0) : vec3(-1, 0, 0);
      float uc = ((L > R ? b : b + 1) + 0.5f) * inv, sh = L > R ? 200.f : 185.f, va = j * inv, vb = (j1 + 1) * inv;
      PQ f1[4] = {{t, hB, lo, uc, vb, sh}, {t, hB, hi, uc, vb, sh}, {t, hA, hi, uc, va, sh}, {t, hA, lo, uc, va, sh}}; face(f1, N);
      PQ f2[4] = {{t, hB, -hi, uc, vb, sh}, {t, hB, -lo, uc, vb, sh}, {t, hA, -lo, uc, va, sh}, {t, hA, -hi, uc, va, sh}}; face(f2, N);
      j = j1 + 1;
    }
  }
  // the steps between rows (and the tops and bottoms)
  for (int b = -1; b < S; b++) {
    int i = 0;
    while (i < S) {
      int U = th(i, b), Dn = th(i, b + 1);
      if (U == Dn) { i++; continue; }
      int i1 = i; while (i1 + 1 < S && th(i1 + 1, b) == U && th(i1 + 1, b + 1) == Dn) i1++;
      float hy = 1 - (b + 1) * inv, lo = (float)std::min(U, Dn), hi = (float)std::max(U, Dn);
      vec3 N = U > Dn ? vec3(0, -1, 0) : vec3(0, 1, 0);
      float vc = ((U > Dn ? b : b + 1) + 0.5f) * inv, sh = U > Dn ? 150.f : 250.f, ua = i * inv, ub = (i1 + 1) * inv;
      PQ f1[4] = {{i * inv, hy, lo, ua, vc, sh}, {(i1 + 1) * inv, hy, lo, ub, vc, sh}, {(i1 + 1) * inv, hy, hi, ub, vc, sh}, {i * inv, hy, hi, ua, vc, sh}}; face(f1, N);
      PQ f2[4] = {{i * inv, hy, -hi, ua, vc, sh}, {(i1 + 1) * inv, hy, -hi, ub, vc, sh}, {(i1 + 1) * inv, hy, -lo, ub, vc, sh}, {i * inv, hy, -lo, ua, vc, sh}}; face(f2, N);
      i = i1 + 1;
    }
  }
  return P;
}

std::shared_ptr<PlantTemplate> plantTemplate(const MeshTiles* T, int tile) {
  std::lock_guard<std::mutex> lk(g_mx);
  checkGen(T);
  auto it = g_tpl.find(tile);
  if (it != g_tpl.end()) return it->second;
  auto p = buildTemplate(*T, tile);
  g_tpl[tile] = p;
  return p;
}

bool keyHas(const std::string& k, std::initializer_list<const char*> parts) { for (auto p : parts) if (k.find(p) != std::string::npos) return true; return false; }

std::shared_ptr<FlowerLook> flowerLook(const MeshTiles* T, int tile, const std::string& key) {
  std::lock_guard<std::mutex> lk(g_mx);
  checkGen(T);
  auto it = g_flower.find(tile);
  if (it != g_flower.end()) return it->second;
  auto L = std::make_shared<FlowerLook>();
  Sprite info = spriteInfo(*T, tile);
  auto& px = info.px;
  if (px.empty()) { L->none = true; g_flower[tile] = L; return L; }
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
  std::vector<Px> farList; for (auto& p : F) if (std::abs(p.r - petal.r) + std::abs(p.g - petal.g) + std::abs(p.b - petal.b) > 70) farList.push_back(p);
  Px center = petal;
  if (!common(farList, center)) { float bl = 1e9f; bool any = false; for (auto& p : F) if (p.lum < bl) { bl = p.lum; center = p; any = true; } if (!any) center = petal; }
  int top = 99; for (auto& p : px) top = std::min(top, p.j);
  float headRow = top + 2.f; if (!F.empty()) { float s = 0; for (auto& p : F) s += p.j; headRow = s / F.size(); }
  float R = (float)info.R;
  L->stem = stem; L->leaf = leaf; L->petal = petal; L->center = center;
  L->kind = keyHas(key, {"tulip", "flower_yellow", "dandelion", "torchflower"}) ? 1 : keyHas(key, {"allium"}) ? 2 : keyHas(key, {"lily_of_the_valley"}) ? 3 : 0;
  L->h = clampv((R - top) / R, 0.42f, 0.95f); L->head = clampv(1 - (headRow + 0.5f) / R, 0.3f, 0.9f);
  g_flower[tile] = L;
  return L;
}

std::shared_ptr<CaneLook> caneLook(const MeshTiles* T, int tile) {
  std::lock_guard<std::mutex> lk(g_mx);
  checkGen(T);
  auto it = g_cane.find(tile);
  if (it != g_cane.end()) return it->second;
  auto L = std::make_shared<CaneLook>();
  Sprite info = spriteInfo(*T, tile);
  int R = info.R; L->R = R;
  std::vector<int> col(R, 0); for (auto& p : info.px) col[p.i]++;
  for (int i = 0; i < R; i++) {
    if (col[i] < R * 0.6f) continue;
    if (!L->stalks.empty() && L->stalks.back().second == i - 1) L->stalks.back().second = i; else L->stalks.push_back({i, i});
  }
  int near = 3 * R / 16;
  auto inStalk = [&](int i) { for (size_t n = 0; n < L->stalks.size(); n++) if (i >= L->stalks[n].first && i <= L->stalks[n].second) return (int)n; return -1; };
  for (auto& p : info.px) if (inStalk(p.i) < 0 && !L->stalks.empty()) {
    int k = 0, bd = 99;
    for (size_t n = 0; n < L->stalks.size(); n++) { int d = std::min(std::abs(p.i - L->stalks[n].first), std::abs(p.i - L->stalks[n].second)); if (d < bd) { bd = d; k = (int)n; } }
    if (bd <= near) L->leaves.push_back({p, k, p.i > L->stalks[k].second ? 1 : -1, bd});
  }
  for (auto& s : L->stalks) {
    const Px* cap = nullptr;
    for (auto& p : info.px) if (p.i == s.first && p.j == R / 2) { cap = &p; break; }
    if (!cap) for (auto& p : info.px) if (p.i == s.first) { cap = &p; break; }
    if (!cap && !info.px.empty()) cap = &info.px[0];
    L->cap.push_back(cap ? *cap : Px{0, 0, 0, 0, 0, 0, vec2(0.5f)});
  }
  if (L->stalks.size() > 4) L->stalks.resize(4);
  L->none = L->stalks.empty();
  g_cane[tile] = L;
  return L;
}

std::shared_ptr<KelpLook> kelpLook(const MeshTiles* T, int tile) {
  std::lock_guard<std::mutex> lk(g_mx);
  checkGen(T);
  auto it = g_kelp.find(tile);
  if (it != g_kelp.end()) return it->second;
  auto L = std::make_shared<KelpLook>();
  Sprite info = spriteInfo(*T, tile);
  if (!info.px.empty()) {
    std::unordered_map<int, int> n; int bn = 0; L->stem = info.px[0];
    for (auto& p : info.px) { int k = (p.r >> 4) << 8 | (p.g >> 4) << 4 | (p.b >> 4); int c = ++n[k]; if (c > bn) { bn = c; L->stem = p; } }
    L->leaf = L->stem; float bl = -1; for (auto& p : info.px) if (p.lum > bl) { bl = p.lum; L->leaf = p; }
    L->none = false;
  }
  g_kelp[tile] = L;
  return L;
}

// ---- emitting
constexpr u8 PLANT_MAT = Mat::Cutout | Mat::NoRelief;

// one picture as a voxel slab from (ax, az) to (bx, bz) across the cell, h0..h1 up it
void voxelPlane(MeshCtx& c, std::vector<ChunkVertex>& d, const PlantTemplate& P, int tile, float x, float y, float z,
                float ax, float az, float bx, float bz, float h0, float h1, float sk, float bl, bool swayTop, float swayBase = 0, float swayScale = 1) {
  float dx = bx - ax, dz = bz - az, L = std::sqrt(dx * dx + dz * dz); if (L <= 0) L = 1;
  float nx = -dz / L / 32, nz = dx / L / 32, X = x + ax, Z = z + az, H = h1 - h0;
  for (size_t i = 0; i + 3 < P.q.size(); i += 4) {
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const PQ& q = P.q[i + k];
      u8 sw = swayTop && (q.h > 0.001f || swayBase > 0) ? (u8)(16 + std::lround(15 * clampv(swayBase + q.h * swayScale, 0.f, 1.f))) : 0;
      v[k] = {vec3(X + dx * q.t + nx * q.s, y + h0 + H * q.h, Z + dz * q.t + nz * q.s), vec2(q.u, q.v), sk, bl, q.sh / 255.f, sw};
    }
    c.quad(d, v, tile, NORMAL_NONE, PLANT_MAT);
  }
}

// a flat double-sided picture plane (the classic way)
void planeQuad(MeshCtx& c, std::vector<ChunkVertex>& d, int tile, float x, float y, float z, float ax, float az, float bx, float bz, float sk, float bl, float sh, u8 swayTop) {
  MV v[4] = {{vec3(x + ax, y, z + az), {0, 1}, sk, bl, sh, 0}, {vec3(x + bx, y, z + bz), {1, 1}, sk, bl, sh, 0},
             {vec3(x + bx, y + 1, z + bz), {1, 0}, sk, bl, sh, swayTop}, {vec3(x + ax, y + 1, z + az), {0, 0}, sk, bl, sh, swayTop}};
  c.quad2(d, v, tile, NORMAL_NONE, PLANT_MAT);
}

// boxes and leaves of a single colour (a pixel of the picture)
struct Kit {
  MeshCtx& c; std::vector<ChunkVertex>& d; int tile; float sk, bl; std::function<u8(float)> sw;
  void V(MV& v, vec3 p, const Px& P, float sh) { v = {p, P.uv, sk, bl, sh / 255.f, sw(p.y)}; }
  void box(float x0, float y0, float z0, float x1, float y1, float z1, const Px& P, int caps = 3) {
    static const float SH[6] = {205, 195, 250, 140, 225, 215};
    for (int f = 0; f < 6; f++) {
      if ((f == 2 && !(caps & 1)) || (f == 3 && !(caps & 2))) continue;
      MV v[4];
      for (int k = 0; k < 4; k++) { const int* cc = FACES[f].c[k]; V(v[k], vec3(cc[0] ? x1 : x0, cc[1] ? y1 : y0, cc[2] ? z1 : z0), P, SH[f]); }
      c.quad(d, v, tile, NORMAL_NONE, PLANT_MAT);
    }
  }
  // a flat piece from p to q, w wide and t thick (a leaf, a petal); taper < 1 narrows the far end
  void piece(vec3 p, vec3 q, float w, float t, const Px& P, float taper = 1) {
    float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z, Lh = std::sqrt(dx * dx + dz * dz); if (Lh <= 0) Lh = 1e-6f;
    float sx = -dz / Lh * w / 2, sz = dx / Lh * w / 2;
    float L3 = std::sqrt(dx * dx + dy * dy + dz * dz); if (L3 <= 0) L3 = 1e-6f;
    float ux = dx / L3, uy = dy / L3, uz = dz / L3;
    vec3 n(uy * sz, uz * sx - ux * sz, -uy * sx);
    float nl = glm::length(n); if (nl <= 0) nl = 1; n = n / nl * (t / 2);
    vec3 C[4] = {{p.x - sx, p.y, p.z - sz}, {q.x - sx * taper, q.y, q.z - sz * taper}, {q.x + sx * taper, q.y, q.z + sz * taper}, {p.x + sx, p.y, p.z + sz}};
    vec3 up[4], dn[4]; for (int k = 0; k < 4; k++) { up[k] = C[k] + n; dn[k] = C[k] - n; }
    auto quad = [&](vec3 a, vec3 b, vec3 cc, vec3 dd, float sh) { MV v[4]; V(v[0], a, P, sh); V(v[1], b, P, sh); V(v[2], cc, P, sh); V(v[3], dd, P, sh); c.quad2(d, v, tile, NORMAL_NONE, PLANT_MAT); };
    quad(up[0], up[1], up[2], up[3], 245); quad(dn[1], dn[0], dn[3], dn[2], 175);
    quad(up[0], dn[0], dn[1], up[1], 200); quad(up[2], dn[2], dn[3], up[3], 200); quad(up[1], dn[1], dn[2], up[2], 215);
  }
};

u32 hashLocal(int x, int y, int z, int k) { return hash32(x, y, z, k); }

bool emitFlower3D(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z) {
  const BlockDef& self = c.R.block(id);
  int tile = self.faces[0];
  auto L = flowerLook(c.tiles, tile, self.key);
  if (L->none) return false;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]);
  int ly = c.in.sy * 16 + y;
  u32 hsh = hashLocal(x, ly, z, 5);
  float r1 = (hsh & 255) / 255.f, r2 = ((hsh >> 8) & 255) / 255.f, r3 = ((hsh >> 16) & 255) / 255.f;
  float cx = x + 0.5f + (r1 - 0.5f) * 0.3f, cz = z + 0.5f + (r2 - 0.5f) * 0.3f, rot = r3 * TAU;
  Kit K{c, d, tile, sk, bl, [y](float vy) -> u8 { float hy = vy - y; return hy > 0.02f ? (u8)(16 + std::lround(15 * clampv(hy, 0.f, 1.f))) : 0; }};
  float s1 = 1 / 32.f, H = L->h * (0.88f + r1 * 0.2f), hy = y + std::min(H, L->head + 0.08f) - 0.02f;
  float lean = 0.035f, lx = std::cos(rot) * lean, lz = std::sin(rot) * lean;
  float mid = y + (hy - y) * 0.5f;
  K.box(cx - s1, (float)y, cz - s1, cx + s1, mid, cz + s1, L->stem);
  K.box(cx + lx * 0.5f - s1, mid - 0.01f, cz + lz * 0.5f - s1, cx + lx * 0.5f + s1, hy, cz + lz * 0.5f + s1, L->stem);
  float hx = cx + lx * 0.6f, hz = cz + lz * 0.6f;
  for (int k = 0; k < 2; k++) {
    float a = rot + k * PI + 0.6f, lyy = y + 0.06f + k * 0.08f, len = 0.17f + r2 * 0.06f;
    K.piece(vec3(cx, lyy, cz), vec3(cx + std::cos(a) * len, lyy + 0.13f, cz + std::sin(a) * len), 0.075f, 0.02f, L->leaf);
  }
  if (L->kind == 2) {
    float R0 = 0.11f;
    for (int k = 0; k < 3; k++) {
      float yy = hy - 0.02f + k * 0.07f, rr = R0 * (k == 1 ? 1.f : 0.75f);
      K.box(hx - rr, yy, hz - rr * 0.6f, hx + rr, yy + 0.07f, hz + rr * 0.6f, L->petal);
      K.box(hx - rr * 0.6f, yy, hz - rr, hx + rr * 0.6f, yy + 0.07f, hz + rr, L->petal);
    }
    K.box(hx - 0.03f, hy + 0.21f, hz - 0.03f, hx + 0.03f, hy + 0.24f, hz + 0.03f, L->center);
  } else if (L->kind == 1) {
    K.box(hx - 0.04f, hy - 0.01f, hz - 0.04f, hx + 0.04f, hy + 0.06f, hz + 0.04f, L->center);
    for (int k = 0; k < 5; k++) { float a = rot + k / 5.f * TAU; K.piece(vec3(hx + std::cos(a) * 0.03f, hy, hz + std::sin(a) * 0.03f), vec3(hx + std::cos(a) * 0.085f, hy + 0.15f, hz + std::sin(a) * 0.085f), 0.075f, 0.025f, L->petal); }
  } else if (L->kind == 3) {
    for (int k = 0; k < 4; k++) {
      float t = k / 3.f, bx = hx + std::cos(rot) * (0.03f + t * 0.15f), bz = hz + std::sin(rot) * (0.03f + t * 0.15f), by = hy + 0.06f - t * t * 0.12f;
      K.box(bx - 0.012f, by, bz - 0.012f, bx + 0.012f, by + 0.03f, bz + 0.012f, L->stem);
      K.box(bx - 0.035f, by - 0.06f, bz - 0.035f, bx + 0.035f, by, bz + 0.035f, L->petal);
    }
  } else {
    int n = 5 + (hsh & 1); float plen = 0.13f + r3 * 0.03f;
    for (int k = 0; k < n; k++) { float a = rot + (float)k / n * TAU; K.piece(vec3(hx + std::cos(a) * 0.025f, hy + 0.02f, hz + std::sin(a) * 0.025f), vec3(hx + std::cos(a) * plen, hy + 0.06f, hz + std::sin(a) * plen), 0.085f, 0.022f, L->petal); }
    K.box(hx - 0.04f, hy + 0.01f, hz - 0.04f, hx + 0.04f, hy + 0.07f, hz + 0.04f, L->center);
  }
  return true;
}

// a blade of grass with real thickness (one micro-voxel): front, back and its two edges
void blade(MeshCtx& c, std::vector<ChunkVertex>& d, int tile, vec3 A, vec3 B, vec3 Cc, vec3 D, float u0, float u1, float v0, float v1, float sk, float bl, float sh, u8 swB, u8 swT) {
  vec3 dir = B - A; vec3 n(-dir.z, 0, dir.x); float nl = glm::length(n); n = nl > 0 ? n / nl * (0.5f / 32) : vec3(0);
  MV f[4] = {{A + n, {u0, v1}, sk, bl, sh, swB}, {B + n, {u1, v1}, sk, bl, sh, swB}, {Cc + n, {u1, v0}, sk, bl, sh, swT}, {D + n, {u0, v0}, sk, bl, sh, swT}};
  c.quad(d, f, tile, NORMAL_NONE, PLANT_MAT);
  float sb = sh * 0.9f;
  MV b[4] = {{B - n, {u1, v1}, sk, bl, sb, swB}, {A - n, {u0, v1}, sk, bl, sb, swB}, {D - n, {u0, v0}, sk, bl, sb, swT}, {Cc - n, {u1, v0}, sk, bl, sb, swT}};
  c.quad(d, b, tile, NORMAL_NONE, PLANT_MAT);
  float ue0 = u0 + 0.5f / 32, ue1 = u1 - 0.5f / 32, se = sh * 0.78f;
  MV e1[4] = {{A - n, {ue0, v1}, sk, bl, se, swB}, {A + n, {ue0, v1}, sk, bl, se, swB}, {D + n, {ue0, v0}, sk, bl, se, swT}, {D - n, {ue0, v0}, sk, bl, se, swT}};
  c.quad(d, e1, tile, NORMAL_NONE, PLANT_MAT);
  MV e2[4] = {{B + n, {ue1, v1}, sk, bl, se, swB}, {B - n, {ue1, v1}, sk, bl, se, swB}, {Cc - n, {ue1, v0}, sk, bl, se, swT}, {Cc + n, {ue1, v0}, sk, bl, se, swT}};
  c.quad(d, e2, tile, NORMAL_NONE, PLANT_MAT);
}

// short grass and ferns (and both halves of tall grass): a clump of blades, each a strip of the picture standing at its own angle and lean
void emitGrassClump(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta, int tile, int part) {
  const BlockTraits& T = traits(id);
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]);
  if (T.tint) c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  int ly = c.in.sy * 16 + y - (part == 2 ? 1 : 0);   // both halves of a tall plant share their blades
  u32 h = hashLocal(x, ly, z, 9);
  auto rnd = [&]() { h = (u32)((h ^ (h >> 15)) * 0x2c1b3c6du) ^ 0x297a2d39u; h ^= h >> 12; return (h % 10000) / 10000.f; };
  for (int k = 0; k < 6; k++) {
    float px = x + 0.18f + rnd() * 0.64f, pz = z + 0.18f + rnd() * 0.64f, a = rnd() * PI, w = 0.2f + rnd() * 0.12f, hh = 0.62f + rnd() * 0.38f;
    float ca = std::cos(a) * w / 2, sa = std::sin(a) * w / 2, la = rnd() * TAU, lean = 0.06f + rnd() * 0.12f, lx = std::cos(la) * lean, lz = std::sin(la) * lean;
    float u0 = std::floor(rnd() * 10) / 16.f, u1 = u0 + 6 / 16.f, sh = (220 + std::floor(rnd() * 30)) / 255.f;
    if (part == 0) {
      blade(c, d, tile, vec3(px - ca, y, pz - sa), vec3(px + ca, y, pz + sa), vec3(px + ca + lx, y + hh, pz + sa + lz), vec3(px - ca + lx, y + hh, pz - sa + lz), u0, u1, 0, 1, sk, bl, sh, 0, (u8)(16 + std::lround(15 * hh)));
    } else if (part == 1) {   // lower half of a tall plant: straight up into the upper half
      blade(c, d, tile, vec3(px - ca, y, pz - sa), vec3(px + ca, y, pz + sa), vec3(px + ca, y + 1, pz + sa), vec3(px - ca, y + 1, pz - sa), u0, u1, 0, 1, sk, bl, sh, 0, 23);
    } else {                  // upper half: on from the lower half, leaning
      blade(c, d, tile, vec3(px - ca, y, pz - sa), vec3(px + ca, y, pz + sa), vec3(px + ca + lx, y + hh, pz + sa + lz), vec3(px - ca + lx, y + hh, pz - sa + lz), u0, u1, 0, 1, sk, bl, sh, 23, (u8)(23 + std::lround(8 * hh)));
    }
  }
  c.tint = 0xFFFFFF;
}

bool emitCane3D(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z) {
  const BlockTraits& T = traits(id);
  int tile = c.R.block(id).faces[0];
  auto L = caneLook(c.tiles, tile);
  if (L->none) return false;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]);
  if (T.tint) c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  Kit K{c, d, tile, sk, bl, [](float) -> u8 { return 0; }};
  int caps = (c.at(x, y + 1, z) != id ? 1 : 0) | (c.at(x, y - 1, z) != id ? 2 : 0);
  u32 h = hashLocal(x, 0, z, 23); float turn = (h & 3) * PI / 2;
  static const float spots[4][2] = {{0.3f, 0.3f}, {0.7f, 0.38f}, {0.46f, 0.72f}, {0.74f, 0.76f}};
  float R = (float)L->R;
  for (size_t k = 0; k < L->stalks.size(); k++) {
    int c0 = L->stalks[k].first, c1 = L->stalks[k].second;
    const float* sp = spots[k % 4];
    float jx = (((h >> (8 + k * 4)) & 15) / 15.f - 0.5f) * 0.08f, jz = (((h >> (10 + k * 4)) & 15) / 15.f - 0.5f) * 0.08f;
    float ox = sp[0] - 0.5f + jx, oz = sp[1] - 0.5f + jz;
    float rx = ox * std::cos(turn) - oz * std::sin(turn), rz = ox * std::sin(turn) + oz * std::cos(turn);
    float cx = x + 0.5f + rx, cz = z + 0.5f + rz, hw = (c1 - c0 + 1) / R / 2;
    float u0 = c0 / R, u1 = (c1 + 1) / R;
    auto side = [&](float ax, float az, float bx, float bz, float sh) {
      MV v[4] = {{vec3(ax, y, az), {u0, 1}, sk, bl, sh / 255.f, 0}, {vec3(bx, y, bz), {u1, 1}, sk, bl, sh / 255.f, 0},
                 {vec3(bx, y + 1, bz), {u1, 0}, sk, bl, sh / 255.f, 0}, {vec3(ax, y + 1, az), {u0, 0}, sk, bl, sh / 255.f, 0}};
      c.quad(d, v, tile, NORMAL_NONE, PLANT_MAT);
    };
    side(cx - hw, cz + hw, cx + hw, cz + hw, 225); side(cx + hw, cz - hw, cx - hw, cz - hw, 215);
    side(cx + hw, cz + hw, cx + hw, cz - hw, 205); side(cx - hw, cz - hw, cx - hw, cz + hw, 195);
    if (caps & 1) K.box(cx - hw, y + 0.999f, cz - hw, cx + hw, y + 1.f, cz + hw, L->cap[k], 1);
    float a = turn + k * 1.9f, dx = std::cos(a), dz = std::sin(a);
    for (auto& lf : L->leaves) {
      if (lf.k != (int)k) continue;
      float yy = y + 1 - (lf.p.j + 1) / R, o0 = hw + (lf.out - 1) / R, o1 = o0 + 1.2f / R, s = (float)lf.side, t = 0.6f / R;
      float lx0 = cx + dx * s * o0, lz0 = cz + dz * s * o0, lx1 = cx + dx * s * o1, lz1 = cz + dz * s * o1;
      K.box(std::min(lx0, lx1) - std::fabs(dz) * t, yy, std::min(lz0, lz1) - std::fabs(dx) * t, std::max(lx0, lx1) + std::fabs(dz) * t, yy + 1 / R, std::max(lz0, lz1) + std::fabs(dx) * t, lf.p);
    }
  }
  c.tint = 0xFFFFFF;
  return true;
}

bool emitKelp3D(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z) {
  int tile = c.R.block(id).faces[0];
  auto L = kelpLook(c.tiles, tile);
  if (L->none) return false;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]);
  int dd = 0; while (dd < 6 && y - dd - 1 >= MeshInput::Y0 && c.at(x, y - dd - 1, z) == id) dd++;
  Kit K{c, d, tile, sk, bl, [dd, y](float vy) -> u8 { float k = std::min(1.f, (dd + (vy - y)) / 5); return k > 0.01f ? (u8)(16 + std::lround(15 * k)) : 0; }};
  bool top = c.at(x, y + 1, z) != id;
  u32 h = hashLocal(x, 0, z, 31);
  float cx = x + 0.5f + (((h & 255) / 255.f) - 0.5f) * 0.3f, cz = z + 0.5f + ((((h >> 8) & 255) / 255.f) - 0.5f) * 0.3f, s = 1.1f / 16;
  float hy = top ? y + 0.72f : y + 1.f;
  K.box(cx - s, (float)y, cz - s, cx + s, hy, cz + s, L->stem, top ? 1 : 0);
  float base = ((h >> 16) & 255) / 255.f * TAU;
  int ly = c.in.sy * 16 + y;
  for (int k = 0; k < 2; k++) {
    float a = base + (ly * 2 + k) * 2.4f, lyy = y + 0.18f + k * 0.45f, len = 0.26f + (((h >> (k * 5)) & 31) / 31.f) * 0.1f;
    K.piece(vec3(cx, lyy, cz), vec3(cx + std::cos(a) * len, lyy + 0.2f, cz + std::sin(a) * len), 0.16f, 0.03f, L->leaf, 0.35f);
  }
  if (top) for (int k = 0; k < 3; k++) { float a = base + k * TAU / 3 + ly; K.piece(vec3(cx, hy - 0.05f, cz), vec3(cx + std::cos(a) * 0.16f, hy + 0.22f, cz + std::sin(a) * 0.16f), 0.12f, 0.03f, L->leaf, 0.35f); }
  return true;
}

// the classic two crossed pictures (plant detail off, or far away with "near")
void emitCrossFlat(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta, int tile, float yy) {
  const BlockTraits& T = traits(id);
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]), sh = 235 / 255.f;
  u8 sway = T.fire ? 0 : Sway::Plant;
  if (T.tint) c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  if (T.crop) { for (float q : {0.25f, 0.75f}) { planeQuad(c, d, tile, x, yy, z, 0, q, 1, q, sk, bl, sh, sway); planeQuad(c, d, tile, x, yy, z, q, 0, q, 1, sk, bl, sh, sway); } }
  else { planeQuad(c, d, tile, x, yy, z, 0.15f, 0.15f, 0.85f, 0.85f, sk, bl, sh, sway); planeQuad(c, d, tile, x, yy, z, 0.15f, 0.85f, 0.85f, 0.15f, sk, bl, sh, sway); }
  c.tint = 0xFFFFFF;
}
} // namespace

// ---- 3D grass on grass blocks -------------------------------------------------------------------------
namespace { std::atomic<int> g_tufts{0}; }
int meshGrassTufts() { return g_tufts.load(std::memory_order_relaxed); }
void setMeshGrassTufts(int n) { g_tufts.store(std::max(0, n), std::memory_order_relaxed); }

// Blades scattered over the block's top, each a thin tapering strip coloured from the grass top right
// under it (so it matches the ground, any pack, tinted by the climate like the top), darker at its foot,
// leaning its own way and swaying at the tip. The tip's sway code (80 + how much it sways) also tells
// chunk.glsl to sink the blade into the ground with distance, so they fade out long before the detail ring.
void emitGrassTufts(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z) {
  const BlockDef& B = c.R.block(id);
  int tile = B.faces[2];
  float sk = c.skyAt(x, y + 1, z), bl = c.blAt(x, y + 1, z);
  c.tint = c.cornerTint((float)x + 0.5f, (float)z + 0.5f, 0);
  int n = meshGrassTufts();
  u32 h = hashLocal(c.wx0 + x, c.wy0 + y, c.wz0 + z, 31);
  auto rnd = [&]() { h = (u32)((h ^ (h >> 15)) * 0x2c1b3c6du) ^ 0x297a2d39u; h ^= h >> 12; return (h % 10000) / 10000.f; };
  // patchy: some blocks thick with grass, some sparse
  float lush = 0.55f + 0.45f * ((hashLocal((c.wx0 + x) >> 2, 0, (c.wz0 + z) >> 2, 37) & 255) / 255.f);
  int count = std::max(2, (int)std::lround(n * lush));
  float top = (float)y + 1;
  for (int k = 0; k < count; k++) {
    float px = x + 0.06f + rnd() * 0.88f, pz = z + 0.06f + rnd() * 0.88f;
    float a = rnd() * PI, w = 0.045f + rnd() * 0.04f, hh = (0.12f + rnd() * 0.2f) * (0.7f + 0.3f * lush);
    float ca = std::cos(a) * w * 0.5f, sa = std::sin(a) * w * 0.5f;
    float la = rnd() * TAU, lean = 0.02f + rnd() * 0.07f, lx = std::cos(la) * lean, lz = std::sin(la) * lean;
    float tw = 0.18f;   // the tip is a fifth as wide as the foot
    vec2 uv(px - x, pz - z), uvT = uv + vec2(lx, lz) * 0.5f;
    u8 tip = (u8)(80 + std::lround(4 + 7 * hh / 0.32f));
    float sh0 = 0.62f + rnd() * 0.08f, sh1 = 0.95f + rnd() * 0.05f;
    MV v[4] = {{vec3(px - ca, top, pz - sa), uv, sk, bl, sh0, 0}, {vec3(px + ca, top, pz + sa), uv, sk, bl, sh0, 0},
               {vec3(px + ca * tw + lx, top + hh, pz + sa * tw + lz), uvT, sk, bl, sh1, tip}, {vec3(px - ca * tw + lx, top + hh, pz - sa * tw + lz), uvT, sk, bl, sh1, tip}};
    c.quad2(d, v, tile, NORMAL_NONE, Mat::Cutout | Mat::NoRelief);
  }
  c.tint = 0xFFFFFF;
}


void emitCross(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  const BlockDef& self = c.R.block(id);
  const BlockTraits& T = traits(id);
  const std::string& key = self.key;
  float yy = (float)y;
  if (c.R.tShape[c.at(x, y - 1, z)] == ShapeKind::Path) yy -= 1 / 16.f;   // crops sit down into farmland
  int tile = self.faces[0];
  if (T.crop && !T.stageTiles.empty()) tile = T.stageTiles[std::min<int>(meta & 15, (int)T.stageTiles.size() - 1)];
  else if ((T.dbl || T.altTile) && (meta & 1) && T.upperTile >= 0) tile = T.upperTile;
  bool detail = c.opt.detail && c.tiles;
  if (!detail && !T.hangs && key != "kelp" && key != "sugar_cane") { emitCrossFlat(c, d, id, x, y, z, meta, tile, yy); return; }
  bool isGrass = T.tint && keyHas(key, {"grass", "fern"}) && key.find("sea") == std::string::npos;
  if (detail) {
    if ((T.flower || keyHas(key, {"flower_", "tulip", "daisy", "orchid", "allium", "bluet", "cornflower", "lily_of_the", "poppy", "dandelion"})) && !T.dbl && !T.hangs && !T.tint) {
      if (emitFlower3D(c, d, id, x, y, z)) return;
    }
    if (isGrass && !T.dbl) { emitGrassClump(c, d, id, x, y, z, meta, tile, 0); return; }
    if (isGrass && T.dbl) { emitGrassClump(c, d, id, x, y, z, meta, tile, (meta & 1) ? 2 : 1); return; }
    if (key == "sugar_cane" && emitCane3D(c, d, id, x, y, z)) return;
    if (key == "kelp" && emitKelp3D(c, d, id, x, y, z)) return;
  }
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]), sh = 235 / 255.f;
  u8 sway = T.fire ? 0 : Sway::Plant;
  if (T.tint) c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  // seagrass and coral are solid too (an extension of the original, which drew them flat)
  bool voxPlant = !T.fire && !T.tint && key != "cobweb" && key != "kelp";
  int ly = c.in.sy * 16 + y;
  if (voxPlant && detail) {
    auto P = plantTemplate(c.tiles, tile);
    if (T.crop) {
      // crops: the classic # of four pictures, each a solid slab
      for (float q : {0.25f, 0.75f}) {
        voxelPlane(c, d, *P, tile, x, yy, z, 0.002f, q, 0.998f, q, 0, 1, sk, bl, true);
        voxelPlane(c, d, *P, tile, x, yy, z, q, 0.002f, q, 0.998f, 0, 1, sk, bl, true);
      }
    } else {
      // flowers, saplings, mushrooms, bushes: two slabs crossed, each plant a little off-centre and turned its own way
      int hy = T.dbl && (meta & 1) ? ly - 1 : ly;
      u32 hsh = hashLocal(x, hy, z, 5);
      bool top = !T.dbl || (meta & 1);
      float jx = ((hsh & 255) / 255.f - 0.5f) * 0.24f, jz = (((hsh >> 8) & 255) / 255.f - 0.5f) * 0.24f, ang = ((hsh >> 16) & 255) / 255.f * PI / 2 + PI / 4;
      float cx = 0.5f + jx, cz = 0.5f + jz, r = 0.46f, ca = std::cos(ang) * r, sa = std::sin(ang) * r;
      // a double plant's lower half bends a little too, so the two halves stay joined
      float sb = T.dbl ? ((meta & 1) ? 0.5f : 0.f) : 0.f, ss = T.dbl ? 0.5f : 1.f;
      bool sw = top || T.dbl;
      voxelPlane(c, d, *P, tile, x, yy, z, cx - ca, cz - sa, cx + ca, cz + sa, 0, 1, sk, bl, sw, sb, ss);
      voxelPlane(c, d, *P, tile, x, yy, z, cx + sa, cz - ca, cx - sa, cz + ca, 0, 1, sk, bl, sw, sb, ss);
    }
  } else if (T.crop && T.hash && !T.fire) {
    // crops grow as a clump of separate plants, each its own height and turned its own way
    u32 hsh = hashLocal(x, ly, z, 11); bool top = !T.dbl || (meta & 1);
    static const float spots[4][2] = {{0.27f, 0.27f}, {0.73f, 0.3f}, {0.3f, 0.73f}, {0.72f, 0.72f}};
    for (int k = 0; k < 4; k++) {
      u32 h = (hsh >> (k * 7)) & 127; float jx = ((h & 15) / 15.f - 0.5f) * 0.14f, jz = (((h >> 4) & 7) / 7.f - 0.5f) * 0.14f, ang = (h / 127.f) * PI;
      float cx = spots[k][0] + jx, cz = spots[k][1] + jz, r = 0.27f, ca = std::cos(ang) * r, sa = std::sin(ang) * r;
      planeQuad(c, d, tile, x, yy, z, cx - ca, cz - sa, cx + ca, cz + sa, sk, bl, sh, top ? sway : 0);
      planeQuad(c, d, tile, x, yy, z, cx + sa, cz - ca, cx - sa, cz + ca, sk, bl, sh, top ? sway : 0);
    }
  } else if (T.hash || T.fire) {
    // fire (and anything else drawn that way): four planes in a # pattern, like the classic game
    float o = T.fire ? 0.06f : 0.25f; bool top = !T.dbl || (meta & 1);
    for (float q : {o, 1 - o}) { planeQuad(c, d, tile, x, yy, z, 0, q, 1, q, sk, bl, sh, top ? sway : 0); planeQuad(c, d, tile, x, yy, z, q, 0, q, 1, sk, bl, sh, top ? sway : 0); }
  } else if (!T.tint && !T.dbl) {
    // three planes round the stem, so they have some body from every side
    u32 hsh = hashLocal(x, ly, z, 5);
    float jx = ((hsh & 255) / 255.f - 0.5f) * 0.3f, jz = (((hsh >> 8) & 255) / 255.f - 0.5f) * 0.3f, ang = ((hsh >> 16) & 255) / 255.f * PI;
    float cx = 0.5f + jx, cz = 0.5f + jz, r = 0.46f;
    for (int k = 0; k < 3; k++) { float a = ang + k * PI / 3, ca = std::cos(a) * r, sa = std::sin(a) * r; planeQuad(c, d, tile, x, yy, z, cx - ca, cz - sa, cx + ca, cz + sa, sk, bl, sh, sway); }
  } else {
    // each plant stands a little off-centre and turned its own way (the halves of a two-tall plant share theirs)
    int hy = T.dbl && (meta & 1) ? ly - 1 : ly; u32 hsh = hashLocal(x, hy, z, 5);
    float jx = ((hsh & 255) / 255.f - 0.5f) * 0.36f, jz = (((hsh >> 8) & 255) / 255.f - 0.5f) * 0.36f, ang = ((hsh >> 16) & 255) / 255.f * PI / 2 + PI / 4;
    float cx = 0.5f + jx, cz = 0.5f + jz, r = 0.48f, ca = std::cos(ang) * r, sa = std::sin(ang) * r;
    bool top = !T.dbl || (meta & 1);
    planeQuad(c, d, tile, x, yy, z, cx - ca, cz - sa, cx + ca, cz + sa, sk, bl, sh, top ? sway : 0);
    planeQuad(c, d, tile, x, yy, z, cx + sa, cz - ca, cx - sa, cz + ca, sk, bl, sh, top ? sway : 0);
  }
  c.tint = 0xFFFFFF;
}

// vines (against their wall) and lily pads (flat on the water) as voxel plates
bool emitVoxelLeafPlate(MeshCtx& c, std::vector<ChunkVertex>& d, BlockId id, int x, int y, int z, u8 meta) {
  if (!c.tiles) return false;
  const BlockTraits& T = traits(id);
  int tile = c.R.block(id).faces[0];
  auto P = plantTemplate(c.tiles, tile);
  if (P->q.empty()) return false;
  float sk = c.skyAt(x, y, z), bl = std::max<float>(c.blAt(x, y, z), c.R.tLight[id]);
  c.tint = c.cornerTint((float)x + 1, (float)z + 1, T.tint == 2 ? 1 : 0);
  bool flat = c.R.tShape[id] == ShapeKind::Carpet;
  int f = meta & 3;
  auto rot = [f](float lx, float lz) {
    switch (f) { case 1: return vec2(1 - lz, lx); case 2: return vec2(1 - lx, 1 - lz); case 3: return vec2(lz, 1 - lx); default: return vec2(lx, lz); }
  };
  for (size_t i = 0; i + 3 < P->q.size(); i += 4) {
    MV v[4];
    for (int k = 0; k < 4; k++) {
      const PQ& q = P->q[i + k];
      vec3 p;
      if (flat) p = vec3(x + q.t, y + 1.5f / 32 + q.s / 32, z + 1 - q.h);
      else { vec2 r = rot(q.t, 1.5f / 32 + q.s / 32); p = vec3(x + r.x, y + q.h, z + r.y); }
      v[k] = {p, vec2(q.u, q.v), sk, bl, q.sh / 255.f, flat ? (u8)0 : (u8)(Sway::Hang + 2)};
    }
    c.quad(d, v, tile, NORMAL_NONE, PLANT_MAT);
  }
  c.tint = 0xFFFFFF;
  return true;
}

} // namespace dq
