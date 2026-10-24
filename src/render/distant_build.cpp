// render/distant_build.cpp — the worker side of distant terrain (game/distant.js distSummarize,
// distBuild, distMesh): colours, summaries of real chunks, and a tile's columns turned into a mesh
// (tops and undersides merged into rectangles, walls merged along their edge).
#include "render/distant.h"
#include "render/mesher.h"
#include "assets/tileset.h"

namespace dq::lod {

namespace {
inline u8 q4(float v) { return (u8)std::min(252.f, std::max(0.f, std::round(v / 4) * 4)); }
vec3 unpack(u32 c) { return vec3(c & 255, (c >> 8) & 255, (c >> 16) & 255); }
vec3 tintOf(const ColorTable& ct, BlockId id, u8 temp, u8 humid) {
  u8 k = id < ct.tint.size() ? ct.tint[id] : 0;
  if (k == 3) return vec3(60, 160, 224) / 255.f;   // the water tint the chunk mesher uses
  if (k == 0) return vec3(1);
  return unpack(climateTint(k == 2 ? "foliage" : "grass", temp, humid)) / 255.f * (k == 2 ? 0.9f : 1.f);
}
// a block's colour from above (or from the side) in this climate (distBlockColor)
vec3 blockColor(const ColorTable& ct, BlockId id, u8 temp, u8 humid, bool side, u32 sampleTint = 0xFFFFFFFF) {
  if (id >= ct.top.size()) return vec3(128);
  vec3 base = unpack(side ? ct.side[id] : ct.top[id]);
  u8 k = ct.tint[id];
  if (k == 0 || (side && k == 1)) return base;
  if (k != 3 && sampleTint != 0xFFFFFFFF) return base * unpack(sampleTint) / 255.f * (k == 2 ? 0.9f : 1.f);
  return base * tintOf(ct, id, temp, humid);
}
// water: lighter and greener in the shallows, deep blue far out (distWater)
vec3 waterColor(vec3 w, float depth) {
  float k = clampv((depth - 1) / 14, 0.f, 1.f);
  return vec3(lerpf(w.x * 1.05f + 30, w.x * 0.55f, k), lerpf(w.y * 1.05f + 40, w.y * 0.6f, k), lerpf(w.z * 0.95f + 20, w.z * 0.75f, k));
}
constexpr float VOID = -1e9f, GROUNDED = -1e9f;
} // namespace

std::shared_ptr<ColorTable> makeColorTable(const TileSet& tiles) {
  auto ct = std::make_shared<ColorTable>();
  auto& r = reg();
  size_t n = r.blocks.size();
  ct->top.assign(n, 0x808080); ct->side.assign(n, 0x808080); ct->tint.assign(n, 0); ct->isLog.assign(n, 0);
  for (auto& b : r.blocks) {
    u32 t = tiles.avgColor(b.faces[2]), s = tiles.avgColor(b.faces[0]);
    if (t) ct->top[b.id] = t & 0xFFFFFF;
    if (s) ct->side[b.id] = s & 0xFFFFFF; else ct->side[b.id] = ct->top[b.id];
    std::string tk = b.str("tint");
    ct->tint[b.id] = r.tLiquid[b.id] == 1 ? 3 : tk == "grass" ? 1 : tk == "foliage" ? 2 : 0;
    ct->isLog[b.id] = b.has("isLog");
  }
  ct->water = r.blockId("water"); ct->lava = r.blockId("lava"); ct->dirt = r.blockId("dirt"); ct->ice = r.blockId("ice");
  return ct;
}

// ---- a real chunk's surface, 2x2 columns a cell: ground, canopy, water (distSummarize) ---------------------
std::shared_ptr<ChunkSummary> summarizeChunk(const Chunk& c, const ColorTable& ct) {
  auto S = std::make_shared<ChunkSummary>();
  auto& R = reg();
  bool floaty = c.dim == DIM_AETHER || c.dim == DIM_END;
  int top = std::min(DIM_H - 1, c.maxY + 1);
  for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
    float gBest = -1, ctop = -1, cbot = 999, fbot = -1; BlockId gId = AIR, leaf = AIR; bool trunk = false; int wd = 0;
    for (int dz = 0; dz < 2; dz++) for (int dx = 0; dx < 2; dx++) {
      int x = i * 2 + dx, z = j * 2 + dz; bool sawLeaf = false;
      for (int y = top; y >= 0; y--) {
        BlockId id = c.get(x, y, z); if (!id) continue;
        if (R.tLeaves[id]) { sawLeaf = true; ctop = std::max(ctop, (float)y + 1); cbot = std::min(cbot, (float)y); if (!leaf) leaf = id; continue; }
        if (ct.isLog[id] && sawLeaf) { trunk = true; continue; }
        if (R.tOpaque[id] || R.tLiquid[id] || (R.tSolid[id] && R.tRender[id] != (u8)Render::Cross)) {
          float gy = y + (R.tLiquid[id] ? 0.875f : 1.f);
          if (gy > gBest) {
            gBest = gy; gId = id;
            wd = 0;
            if (R.tLiquid[id] == 1) for (int yy = y; yy > 0 && R.tLiquid[c.get(x, yy, z)] == 1 && wd < 30; yy--) wd++;
            fbot = -1;
            if (floaty) { int yy = y; while (yy > 0 && c.get(x, yy - 1, z) != AIR) yy--; fbot = yy > 0 ? (float)yy : -1.f; }
          }
          break;
        }
      }
    }
    int k = j * 8 + i;
    S->fb[k] = -1;
    if (gBest < 0) { S->g[k] = 0; S->ct[k] = 0; S->cb[k] = 0; S->f[k] = 0; S->wd[k] = 0; continue; }
    int ci = ((j * 2) * 16 + i * 2) * 2;
    u8 tb = c.climate[ci], hb = c.climate[ci + 1];   // as the chunk mesher tints it
    vec3 gc = blockColor(ct, gId, tb, hb, false), sc = blockColor(ct, gId, tb, hb, true);
    if (R.tLiquid[gId] == 1) { gc = waterColor(gc, (float)wd); sc = gc; }
    S->g[k] = (i16)std::lround(gBest * 4);
    S->fb[k] = fbot < 0 ? (i16)-1 : (i16)std::lround(fbot * 4);
    for (int q = 0; q < 3; q++) { S->gc[k * 3 + q] = q4(gc[q]); S->sc[k * 3 + q] = q4(sc[q]); }
    S->f[k] = (R.tLiquid[gId] == 1 ? 1 : 0) | (R.tLiquid[gId] == 2 ? 32 : 0);
    S->wd[k] = (u8)wd;
    S->ct[k] = 0; S->cb[k] = 0;
    if (leaf && ctop > gBest + 0.5f) {
      S->ct[k] = (i16)std::lround(ctop * 4); S->cb[k] = (i16)std::lround(std::max(cbot, gBest) * 4);
      vec3 lc = blockColor(ct, leaf, tb, hb, false);
      for (int q = 0; q < 3; q++) S->cc[k * 3 + q] = q4(lc[q]);
      if (trunk) S->f[k] |= 2;
    }
  }
  return S;
}

// ---- a tile: its columns, then its mesh ---------------------------------------------------------------
namespace {
struct Grid {
  int W;
  std::vector<float> G, B, CT, CB;
  std::vector<u8> GC, SC, CC, FL;
  explicit Grid(int w) : W(w), G(w * w, VOID), B(w * w, GROUNDED), CT(w * w, 0), CB(w * w, 999), GC(w * w * 3, 0), SC(w * w * 3, 0), CC(w * w * 3, 0), FL(w * w, 0) {}
  void setC(std::vector<u8>& A, int k, vec3 c) { A[k * 3] = q4(c.x); A[k * 3 + 1] = q4(c.y); A[k * 3 + 2] = q4(c.z); }
  vec3 col(const std::vector<u8>& A, int k) const { return vec3(A[k * 3], A[k * 3 + 1], A[k * 3 + 2]); }
  bool same(const std::vector<u8>& A, int a, int b) const { return A[a * 3] == A[b * 3] && A[a * 3 + 1] == A[b * 3 + 1] && A[a * 3 + 2] == A[b * 3 + 2]; }
};

struct MeshOut {
  std::vector<LodVertex>& v; float size; float minY = 1e9f, maxY = -1e9f;
  void vert(vec3 p, vec3 c, int sh, u16 flags) {
    LodVertex o;
    o.x = (u16)clampv((int)std::lround(p.x / size * 32768.f), 0, 65535);
    o.z = (u16)clampv((int)std::lround(p.z / size * 32768.f), 0, 65535);
    o.y = (i16)clampv((int)std::lround(p.y * 8), -32768, 32767);
    o.r = (u8)clampv((int)c.x, 0, 255); o.g = (u8)clampv((int)c.y, 0, 255); o.b = (u8)clampv((int)c.z, 0, 255);
    o.shade = (u8)clampv(sh, 0, 255); o.flags = flags;
    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    v.push_back(o);
  }
  // a quad facing `n` (corners in any order around the edge), shades per corner
  void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, vec3 col, int sa, int sb, int sc, int sd, u16 fl = 0, vec3 colLow = vec3(-1)) {
    vec3 lo = colLow.x < 0 ? col : colLow;
    if (glm::dot(glm::cross(b - a, c - a), n) < 0) { std::swap(b, d); std::swap(sb, sd); }
    float top = std::max(std::max(a.y, b.y), std::max(c.y, d.y)) - 1e-4f;
    vert(a, a.y >= top ? col : lo, sa, fl); vert(b, b.y >= top ? col : lo, sb, fl);
    vert(c, c.y >= top ? col : lo, sc, fl); vert(d, d.y >= top ? col : lo, sd, fl);
  }
};
// the wall's foot in shadow, for depth
int footShade(int sh, float y1, float y0) { return std::max(80, (int)std::round(sh * std::max(0.6f, 1 - (y1 - y0) * 0.04f))); }
} // namespace

void buildTile(const TileInput& in, TileOutput& out) {
  double t0 = nowSeconds();
  const int N = CELLS, L = in.L, cs = cellSize(L), size = N * cs, W = N + 2;
  const int ox = in.tx * size, oz = in.tz * size, base = dimBase(in.dim);
  const ColorTable& ct = *in.colors;
  auto& R = reg();
  Grid g(W);
  DimGenerator* gen = in.gen ? in.gen->forDim(in.dim) : nullptr;
  bool floaty = in.dim == DIM_AETHER || in.dim == DIM_END;
  std::vector<float> treeTop(W * W, 0);
  std::vector<vec3> leafCol(L >= 2 ? W * W : 0);
  // 1. the ground, cell by cell: a real chunk's surface where there is one, otherwise the generator's
  for (int j = 0; j < W; j++) for (int i = 0; i < W; i++) {
    int k = j * W + i;
    int fx = (int)std::floor(ox + (i - 1 + 0.5) * cs), fz = (int)std::floor(oz + (j - 1 + 0.5) * cs);
    const ChunkSummary* sm = in.sums.empty() ? nullptr : in.sums[k].get();
    if (sm) {
      int q = ((fz & 15) >> 1) * 8 + ((fx & 15) >> 1);
      if (!sm->g[q]) { if (!floaty) { g.G[k] = base + SEA_LEVEL + 0.875f; g.FL[k] = 1; vec3 w = waterColor(unpack(ct.top[ct.water]) * vec3(60, 160, 224) / 255.f, 8); g.setC(g.GC, k, w); g.setC(g.SC, k, w); } continue; }
      g.G[k] = base + sm->g[q] / 4.f;
      if (sm->fb[q] >= 0) g.B[k] = base + sm->fb[q] / 4.f;
      for (int c = 0; c < 3; c++) { g.GC[k * 3 + c] = sm->gc[q * 3 + c]; g.SC[k * 3 + c] = sm->sc[q * 3 + c]; }
      if (sm->ct[q] > 0) { g.CT[k] = base + sm->ct[q] / 4.f; g.CB[k] = base + sm->cb[q] / 4.f; for (int c = 0; c < 3; c++) g.CC[k * 3 + c] = sm->cc[q * 3 + c]; }
      g.FL[k] = sm->f[q] | 64;   // 64: from a real chunk
      continue;
    }
    if (!gen) continue;
    ColumnSample s; gen->sample(fx, fz, s);
    if (s.height < 0 && s.water < 0) continue;   // nothing here (the void between islands)
    u8 tb = 0, hb = 0;   // (without a tint from the generator: the chunk mesher's default climate)
    if (s.water >= 0 && s.water >= s.height && s.fluid) {
      bool lava = R.tLiquid[s.fluid] == 2;
      g.G[k] = base + s.water + 0.875f; g.FL[k] = lava ? 32 : 1;
      vec3 c = blockColor(ct, s.fluid, tb, hb, false, s.tint);
      if (!lava) c = waterColor(c, (float)std::min(16, (int)std::round((s.water - s.height) / 2.f) * 2));
      g.setC(g.GC, k, c); g.setC(g.SC, k, c);
    } else {
      g.G[k] = base + s.height + 1.f;
      BlockId top = s.top ? s.top : ct.dirt;
      g.setC(g.GC, k, blockColor(ct, top, tb, hb, false, s.tint));
      BlockId sideId = R.tGrassy[top] && s.under ? s.under : top;
      g.setC(g.SC, k, blockColor(ct, sideId, tb, hb, true, s.tint));
    }
    if (s.floatBottom >= 0 && s.floatBottom <= s.height) g.B[k] = base + (float)s.floatBottom;
    if (s.tree && s.treeTop > s.height && s.leaves) {
      vec3 lc = blockColor(ct, s.leaves, tb, hb, false, s.tint);
      if (L <= 1) {   // trees with their crowns (near)
        g.CT[k] = base + s.treeTop + 1.f; g.CB[k] = std::max(g.G[k], g.CT[k] - 4); g.setC(g.CC, k, lc); g.FL[k] |= 2;
      } else { treeTop[k] = base + s.treeTop + 1.f; leafCol[k] = lc; }
    }
  }
  // far off, single trees can't be told apart: forests are blended into the ground, as tall as their trees
  if (L >= 2) {
    std::vector<float> G0 = g.G;
    for (int j = 1; j < W - 1; j++) for (int i = 1; i < W - 1; i++) {
      int k = j * W + i; if (treeTop[k] <= 0) continue;
      int nb = (treeTop[k - 1] > 0) + (treeTop[k + 1] > 0) + (treeTop[k - W] > 0) + (treeTop[k + W] > 0);
      if (nb < 2) continue;
      float cover = 0.55f + 0.1f * nb;
      g.setC(g.GC, k, glm::mix(g.col(g.GC, k), leafCol[k] * 0.85f, std::min(1.f, cover * 1.2f)));
      g.setC(g.SC, k, leafCol[k] * 0.7f);
      g.G[k] = G0[k] + std::round((treeTop[k] - G0[k]) * 0.7f * cover);
    }
  }
  // 2. the mesh: a column per cell, walls down to lower neighbours, crowns over the ground, a skirt round the edge
  MeshOut M{out.verts, (float)size};
  out.verts.reserve(N * N * 8);
  auto isVoid = [&](int k) { return g.G[k] <= VOID * 0.5f; };
  auto flagsOf = [&](int k) -> u16 { return (u16)((g.FL[k] & 1 ? 1 : 0) | (g.FL[k] & 32 ? 2 : 0)); };
  // tops (and the undersides of floating land), merged into rectangles of the same height and colour
  std::vector<u8> done(N * N);
  for (int pass = 0; pass < 2; pass++) {
    std::fill(done.begin(), done.end(), 0);
    auto key = [&](int a, int b) {
      if (isVoid(a) || isVoid(b)) return false;
      if (pass == 0) return g.G[a] == g.G[b] && g.same(g.GC, a, b) && flagsOf(a) == flagsOf(b);
      return g.B[a] == g.B[b] && g.same(g.SC, a, b);
    };
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
      int k = (j + 1) * W + (i + 1);
      if (done[j * N + i] || isVoid(k)) continue;
      if (pass == 1 && g.B[k] <= GROUNDED * 0.5f) continue;
      int w = 1; while (i + w < N && !done[j * N + i + w] && key(k, k + w)) w++;
      int h = 1;
      for (; j + h < N; h++) { bool ok = true; for (int q = 0; q < w; q++) if (done[(j + h) * N + i + q] || !key(k, k + h * W + q)) { ok = false; break; } if (!ok) break; }
      for (int jj = 0; jj < h; jj++) for (int q = 0; q < w; q++) done[(j + jj) * N + i + q] = 1;
      float x0 = (float)i * cs, z0 = (float)j * cs, x1 = (float)(i + w) * cs, z1 = (float)(j + h) * cs;
      if (pass == 0) { float y = g.G[k]; M.quad({x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}, {0, 1, 0}, g.col(g.GC, k), 255, 255, 255, 255, flagsOf(k)); }
      else { float y = g.B[k]; M.quad({x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, {0, -1, 0}, g.col(g.SC, k) * 0.85f, 145, 145, 145, 145); }
    }
  }
  // walls, merged along each edge where they match
  const float skirt = 4 + cs * 0.75f;
  struct Seg { float y1, y0; int k; bool on; };
  for (int d = 0; d < 4; d++) {
    // d: 0 -z, 1 +z, 2 -x, 3 +x
    int dk = d == 0 ? -W : d == 1 ? W : d == 2 ? -1 : 1;
    int sh = d < 2 ? 205 : 170;
    vec3 n = d == 0 ? vec3(0, 0, -1) : d == 1 ? vec3(0, 0, 1) : d == 2 ? vec3(-1, 0, 0) : vec3(1, 0, 0);
    for (int line = 0; line < N; line++) {
      Seg run[2] = {{0, 0, 0, false}, {0, 0, 0, false}}; int runStart[2] = {0, 0};
      auto emit = [&](int slot, int a, int b) {
        Seg& s = run[slot]; if (!s.on) return;
        float t0 = (float)a * cs, t1 = (float)b * cs, e;
        vec3 p0, p1;
        if (d < 2) { e = (float)(d == 0 ? line : line + 1) * cs; p0 = {t0, 0, e}; p1 = {t1, 0, e}; }
        else { e = (float)(d == 2 ? line : line + 1) * cs; p0 = {e, 0, t0}; p1 = {e, 0, t1}; }
        vec3 c = g.col(g.SC, s.k); int foot = footShade(sh, s.y1, s.y0);
        M.quad({p0.x, s.y1, p0.z}, {p1.x, s.y1, p1.z}, {p1.x, s.y0, p1.z}, {p0.x, s.y0, p0.z}, n, c, sh, sh, foot, foot);
      };
      for (int t = 0; t <= N; t++) {
        Seg cur[2] = {{0, 0, 0, false}, {0, 0, 0, false}};
        if (t < N) {
          int i = d < 2 ? t : line, j = d < 2 ? line : t;
          int k = (j + 1) * W + (i + 1), nk = k + dk;
          bool edge = d == 0 ? j == 0 : d == 1 ? j == N - 1 : d == 2 ? i == 0 : i == N - 1;
          if (!isVoid(k)) {
            float h = g.G[k], b = g.B[k] > GROUNDED * 0.5f ? g.B[k] : (float)base;
            if (isVoid(nk)) cur[0] = {h, b, k, h > b};
            else {
              float lo = std::max(g.G[nk], b);
              if (edge) lo = std::min(lo, h) - skirt;
              if (lo < h) cur[0] = {h, lo, k, true};
              if (g.B[k] > GROUNDED * 0.5f && g.B[nk] > g.B[k] && !edge) { float top = std::min(g.B[nk], h); if (top > g.B[k]) cur[1] = {top, g.B[k], k, true}; }
            }
          }
        }
        for (int slot = 0; slot < 2; slot++) {
          Seg& r = run[slot]; Seg& c = cur[slot];
          bool same = r.on && c.on && r.y1 == c.y1 && r.y0 == c.y0 && g.same(g.SC, r.k, c.k);
          if (same) continue;
          if (r.on) emit(slot, runStart[slot], t);
          r = c; runStart[slot] = t;
        }
      }
    }
  }
  // crowns of leaves over the ground (near tiles), their trunks under them (nearest)
  if (L <= 1) {
    const int EDX[4] = {0, 0, -1, 1}, EDZ[4] = {-1, 1, 0, 0};
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
      int k = (j + 1) * W + (i + 1);
      float h = g.G[k], ctop = g.CT[k];
      if (isVoid(k) || !(ctop > h + 0.4f)) continue;
      float x0 = (float)i * cs, z0 = (float)j * cs, x1 = x0 + cs, z1 = z0 + cs;
      vec3 cc = g.col(g.CC, k);
      bool flt = L == 0;
      float bot = flt && g.CB[k] < 998 ? std::max(g.CB[k], h) : h;
      M.quad({x0, ctop, z0}, {x0, ctop, z1}, {x1, ctop, z1}, {x1, ctop, z0}, {0, 1, 0}, cc, 245, 245, 245, 245);
      if (bot > h + 0.3f) M.quad({x0, bot, z0}, {x1, bot, z0}, {x1, bot, z1}, {x0, bot, z1}, {0, -1, 0}, cc, 110, 110, 110, 110);
      for (int e = 0; e < 4; e++) {
        int nk = k + EDZ[e] * W + EDX[e];
        float nct = g.CT[nk] > g.G[nk] + 0.4f ? g.CT[nk] : 0.f;
        float nbot = nct ? (flt && g.CB[nk] < 998 ? std::max(g.CB[nk], g.G[nk]) : g.G[nk]) : 1e9f;
        float from = nct && nbot <= bot + 0.01f ? std::max(bot, nct) : bot;
        if (from >= ctop - 0.01f) continue;
        vec3 a, b;
        if (e == 0) { a = {x0, 0, z0}; b = {x1, 0, z0}; } else if (e == 1) { a = {x0, 0, z1}; b = {x1, 0, z1}; }
        else if (e == 2) { a = {x0, 0, z0}; b = {x0, 0, z1}; } else { a = {x1, 0, z0}; b = {x1, 0, z1}; }
        int s = (int)std::round((e < 2 ? 205 : 170) * 0.92f);
        vec3 n(EDX[e], 0, EDZ[e]);
        M.quad({a.x, ctop, a.z}, {b.x, ctop, b.z}, {b.x, from, b.z}, {a.x, from, a.z}, n, cc * 0.9f, s, s, s, s, 0, cc * 0.75f);
      }
      if (flt && (g.FL[k] & 2) && bot > h + 0.3f) {
        float tw = std::min((float)cs, 1.f) / 2, mx = x0 + cs / 2.f, mz = z0 + cs / 2.f;
        vec3 tc(92, 70, 46);
        M.quad({mx - tw, bot, mz - tw}, {mx + tw, bot, mz - tw}, {mx + tw, h, mz - tw}, {mx - tw, h, mz - tw}, {0, 0, -1}, tc, 200, 200, 200, 200);
        M.quad({mx - tw, bot, mz + tw}, {mx + tw, bot, mz + tw}, {mx + tw, h, mz + tw}, {mx - tw, h, mz + tw}, {0, 0, 1}, tc, 200, 200, 200, 200);
        M.quad({mx - tw, bot, mz - tw}, {mx - tw, bot, mz + tw}, {mx - tw, h, mz + tw}, {mx - tw, h, mz - tw}, {-1, 0, 0}, tc, 165, 165, 165, 165);
        M.quad({mx + tw, bot, mz - tw}, {mx + tw, bot, mz + tw}, {mx + tw, h, mz + tw}, {mx + tw, h, mz - tw}, {1, 0, 0}, tc, 165, 165, 165, 165);
      }
    }
  }
  out.minY = M.minY < M.maxY ? M.minY : 0; out.maxY = M.maxY;
  out.ms = (nowSeconds() - t0) * 1000;
}

} // namespace dq::lod
