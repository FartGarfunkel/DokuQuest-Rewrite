#include "world/light.h"
#include <cstring>

namespace dq {

u8 skyTopLevel(int dim) { return dim == DIM_NETHER ? 0 : 15; }

// ---------------------------------------------------------------- first pass (worker thread)
void initialChunkLight(Chunk& c) {
  const Registry& R = reg();
  const u8 skyTop = skyTopLevel(c.dim);
  thread_local std::vector<u8> SKY, BL; thread_local std::vector<u32> Q;
  SKY.assign(16 * 16 * DIM_H, 0); BL.assign(16 * 16 * DIM_H, 0);
  auto idx = [](int x, int y, int z) { return (y << 8) | (z << 4) | x; };
  // 1. direct sunlight down each column until the first light-stopping block
  int top[256];
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    int t = -1;
    for (int y = std::min(DIM_H - 1, c.maxY + 1); y >= 0; y--) { BlockId b = c.get(x, y, z); if (b && (R.tOpaque[b] || R.tAtt[b] || R.tLiquid[b])) { t = y; break; } }
    top[z * 16 + x] = t;
    if (skyTop) for (int y = t + 1; y < DIM_H; y++) SKY[idx(x, y, z)] = skyTop;
  }
  auto flood = [&](std::vector<u8>& L, bool sky) {
    size_t h = 0;
    while (h < Q.size()) {
      u32 p = Q[h++]; int x = p & 15, z = (p >> 4) & 15, y = (int)(p >> 8);
      int l = L[idx(x, y, z)];
      if (l <= 1) continue;
      for (int d = 0; d < 6; d++) {
        int nx = x, ny = y, nz = z;
        if (d == 0) { if (++nx > 15) continue; } else if (d == 1) { if (--nx < 0) continue; }
        else if (d == 2) { if (++ny >= DIM_H) continue; } else if (d == 3) { if (--ny < 0) continue; }
        else if (d == 4) { if (++nz > 15) continue; } else { if (--nz < 0) continue; }
        BlockId nb = c.get(nx, ny, nz);
        if (R.tOpaque[nb]) continue;
        int att = R.tAtt[nb];
        int nl = (sky && d == 3 && l == 15 && att == 0) ? 15 : l - 1 - att;
        int ni = idx(nx, ny, nz);
        if (nl > L[ni]) { L[ni] = (u8)nl; Q.push_back((u32)(nx | (nz << 4) | (ny << 8))); }
      }
    }
    Q.clear();
  };
  // 2. seeds: lit cells beside taller columns, and the cell above a see-through top block
  if (skyTop) {
    Q.clear();
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int t = top[z * 16 + x], mt = t;
      if (x > 0) mt = std::max(mt, top[z * 16 + x - 1]);
      if (x < 15) mt = std::max(mt, top[z * 16 + x + 1]);
      if (z > 0) mt = std::max(mt, top[(z - 1) * 16 + x]);
      if (z < 15) mt = std::max(mt, top[(z + 1) * 16 + x]);
      int yEnd = std::min(mt, DIM_H - 1);
      for (int y = t + 1; y <= yEnd; y++) Q.push_back((u32)(x | (z << 4) | (y << 8)));
      if (t >= 0 && t + 1 < DIM_H && !R.tOpaque[c.get(x, t, z)] && t + 1 > yEnd) Q.push_back((u32)(x | (z << 4) | ((t + 1) << 8)));
    }
    flood(SKY, true);
  }
  // 3. block light from emitters
  Q.clear();
  for (int sy = 0; sy < SECS; sy++) {
    const Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) {
      BlockId id = s->blocks[i];
      if (!id || !(R.tLight[id] || R.tLMeta[id])) continue;
      u8 e = R.metaLight(id, s->meta[i]);
      if (!e) continue;
      int x = i & 15, z = (i >> 4) & 15, y = sy * 16 + (i >> 8);
      BL[idx(x, y, z)] = e;
      Q.push_back((u32)(x | (z << 4) | (y << 8)));
    }
  }
  flood(BL, false);
  // 4. store: allocated sections take the values; empty sections stay unallocated when uniform
  for (int sy = 0; sy < SECS; sy++) {
    int base = sy * 4096;
    u8 first = (u8)(SKY[base] << 4 | BL[base]);
    Section* s = c.section(sy);
    if (!s) {
      bool uniform = true;
      for (int i = 1; i < 4096 && uniform; i++) uniform = (u8)(SKY[base + i] << 4 | BL[base + i]) == first;
      if (uniform) { c.airLight[sy] = first; continue; }
      s = c.ensure(sy);
    }
    for (int i = 0; i < 4096; i++) s->light[i] = (u8)(SKY[base + i] << 4 | BL[base + i]);
  }
  c.lightReady = true;
}

// ---------------------------------------------------------------- incremental engine (main thread)
Chunk* LightEngine::chunkAt(int x, int y, int z) {
  if (!inWorldY(y)) return nullptr;
  int dim = y >> 8, cx = x >> 4, cz = z >> 4;
  CacheEnt& e = cache[((cx & 7) | ((cz & 7) << 3))];
  if (e.c && e.dim == dim && e.cx == cx && e.cz == cz) return e.c;
  Chunk* c = w->chunk(dim, cx, cz);
  e.dim = dim; e.cx = cx; e.cz = cz; e.c = c;
  return c;
}
u8 LightEngine::get(int x, int y, int z, int ch) {
  Chunk* c = chunkAt(x, y, z);
  if (!c) return 0;
  u8 l = c->getLight(x & 15, y & 255, z & 15);
  return ch ? (l & 15) : (l >> 4);
}
void LightEngine::set(int x, int y, int z, int ch, u8 v) {
  Chunk* c = chunkAt(x, y, z);
  if (!c) return;
  int lx = x & 15, ly = y & 255, lz = z & 15;
  u8 l = c->getLight(lx, ly, lz);
  u8 nl = ch ? (u8)((l & 0xF0) | v) : (u8)((v << 4) | (l & 15));
  if (nl == l) return;
  c->setLight(lx, ly, lz, nl);
  cells++;
  touch(x, y, z);
}
void LightEngine::touch(int x, int y, int z) {
  int d = y >> 8, cx = x >> 4, cz = z >> 4, sy = (y & 255) >> 4;
  SectionKey k{d, cx, sy, cz};
  if (!(k == lastTouched)) { touched.insert(k); lastTouched = k; }
  int lx = x & 15, ly = y & 15, lz = z & 15;
  if (lx == 0) touched.insert({d, cx - 1, sy, cz});
  if (lx == 15) touched.insert({d, cx + 1, sy, cz});
  if (lz == 0) touched.insert({d, cx, sy, cz - 1});
  if (lz == 15) touched.insert({d, cx, sy, cz + 1});
  if (ly == 0) touched.insert({d, cx, sy - 1, cz});
  if (ly == 15) touched.insert({d, cx, sy + 1, cz});
}
// light a cell gives by itself: block emitters, and open sky at the very top of a band
u8 LightEngine::emission(int x, int y, int z, int ch, BlockId id, u8 meta) {
  const Registry& R = reg();
  if (ch) return R.metaLight(id, meta);
  if ((y & 255) != 255 || R.tOpaque[id]) return 0;
  int top = skyTopLevel(y >> 8);
  int att = R.tAtt[id];
  return (u8)std::max(0, att ? top - 1 - att : top);
}

void LightEngine::propagate(int ch) {
  const Registry& R = reg();
  auto& Q = inc[ch];
  for (size_t h = 0; h < Q.size(); h++) {
    Node n = Q[h];
    int l = get(n.x, n.y, n.z, ch);
    if (l <= 1) continue;
    for (int d = 0; d < 6; d++) {
      int nx = n.x + (d == 0) - (d == 1), ny = n.y + (d == 2) - (d == 3), nz = n.z + (d == 4) - (d == 5);
      Chunk* c = chunkAt(nx, ny, nz);
      if (!c) continue;
      BlockId nb = c->get(nx & 15, ny & 255, nz & 15);
      if (R.tOpaque[nb]) continue;
      int att = R.tAtt[nb];
      // sky light pours straight down at full strength, also across a band border (the Skylands' open
      // underside onto the Overworld's sky)
      int nl = (ch == 0 && d == 3 && l == 15 && att == 0) ? 15 : l - 1 - att;
      if (nl <= 0) continue;
      if (nl > get(nx, ny, nz, ch)) { set(nx, ny, nz, ch, (u8)nl); Q.push_back({nx, ny, nz, (u8)nl}); }
    }
  }
  Q.clear();
}

void LightEngine::unpropagate(int ch) {
  auto& Q = rem[ch];
  for (size_t h = 0; h < Q.size(); h++) {
    Node n = Q[h];
    for (int d = 0; d < 6; d++) {
      int nx = n.x + (d == 0) - (d == 1), ny = n.y + (d == 2) - (d == 3), nz = n.z + (d == 4) - (d == 5);
      Chunk* c = chunkAt(nx, ny, nz);
      if (!c) continue;
      int nl = get(nx, ny, nz, ch);
      if (!nl) continue;
      bool dependent = nl < n.l || (ch == 0 && d == 3 && n.l == 15 && nl == 15);
      if (dependent) {
        set(nx, ny, nz, ch, 0);
        Q.push_back({nx, ny, nz, (u8)nl});
        int lx = nx & 15, ly = ny & 255, lz = nz & 15;
        u8 e = emission(nx, ny, nz, ch, c->get(lx, ly, lz), c->getMeta(lx, ly, lz));
        if (e) { set(nx, ny, nz, ch, e); inc[ch].push_back({nx, ny, nz, e}); }
      } else inc[ch].push_back({nx, ny, nz, (u8)nl});
    }
  }
  Q.clear();
}

void LightEngine::onBlockChanged(World& world, int x, int y, int z, BlockId o, BlockId n) { changes.push_back({x, y, z}); }
void LightEngine::onChunkLoaded(World& world, Chunk& c) { arrivals.push_back(c.key()); }

void LightEngine::applyChange(const ivec3& p) {
  const Registry& R = reg();
  Chunk* c = chunkAt(p.x, p.y, p.z);
  if (!c) return;
  int lx = p.x & 15, ly = p.y & 255, lz = p.z & 15;
  BlockId id = c->get(lx, ly, lz); u8 meta = c->getMeta(lx, ly, lz);
  for (int ch = 0; ch < 2; ch++) {
    u8 cur = get(p.x, p.y, p.z, ch);
    u8 e = emission(p.x, p.y, p.z, ch, id, meta);
    // take away whatever this cell held (and what depended on it)...
    if (cur) { set(p.x, p.y, p.z, ch, 0); rem[ch].push_back({p.x, p.y, p.z, cur}); unpropagate(ch); }
    // ...then light it again from itself and from its neighbours
    if (e) { set(p.x, p.y, p.z, ch, e); inc[ch].push_back({p.x, p.y, p.z, e}); }
    if (!R.tOpaque[id] || e) for (int d = 0; d < 6; d++) {
      int nx = p.x + (d == 0) - (d == 1), ny = p.y + (d == 2) - (d == 3), nz = p.z + (d == 4) - (d == 5);
      if (!chunkAt(nx, ny, nz)) continue;
      u8 l = get(nx, ny, nz, ch);
      if (l > 1) inc[ch].push_back({nx, ny, nz, l});
    }
    propagate(ch);
  }
}

void LightEngine::mergeBorders(Chunk& c) {
  // compare every cell on the chunk's sides with the neighbour's; the brighter side floods across
  auto pair = [&](int ax, int ay, int az, int bx, int by, int bz) {
    u8 la = get(ax, ay, az, 0) << 4 | get(ax, ay, az, 1), lb = get(bx, by, bz, 0) << 4 | get(bx, by, bz, 1);
    if (la == lb) return;
    for (int ch = 0; ch < 2; ch++) {
      int a = ch ? la & 15 : la >> 4, b = ch ? lb & 15 : lb >> 4;
      if (a > b + 1) inc[ch].push_back({ax, ay, az, (u8)a});
      else if (b > a + 1) inc[ch].push_back({bx, by, bz, (u8)b});
    }
  };
  int x0 = c.cx * 16, z0 = c.cz * 16, base = dimBase(c.dim);
  static const int NX[4] = {-1, 1, 0, 0}, NZ[4] = {0, 0, -1, 1};
  for (int k = 0; k < 4; k++) {
    Chunk* n = w->chunk(c.dim, c.cx + NX[k], c.cz + NZ[k]);
    if (!n) continue;
    int ymax = std::min(DIM_H - 1, std::max(c.maxY, n->maxY) + 1);
    for (int ly = 0; ly <= ymax; ly++) for (int i = 0; i < 16; i++) {
      int ax, az, bx, bz;
      if (NX[k]) { ax = NX[k] < 0 ? x0 : x0 + 15; bx = ax + NX[k]; az = bz = z0 + i; }
      else { az = NZ[k] < 0 ? z0 : z0 + 15; bz = az + NZ[k]; ax = bx = x0 + i; }
      pair(ax, base + ly, az, bx, base + ly, bz);
    }
  }
  // the bands above and below (the stacked world is one column)
  for (int s = -1; s <= 1; s += 2) {
    int dd = c.dim + s;
    if (dd < 0 || dd >= DIM_COUNT || !w->chunk(dd, c.cx, c.cz)) continue;
    int ay = s < 0 ? base : base + DIM_H - 1, by = ay + s;
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) pair(x0 + x, ay, z0 + z, x0 + x, by, z0 + z);
  }
}

void LightEngine::flush(World& world) {
  if (changes.empty() && arrivals.empty()) return;
  double t0 = nowSeconds();
  w = &world; cells = 0;
  for (auto& e : cache) e = CacheEnt();
  std::vector<ChunkKey> arr; arr.swap(arrivals);
  for (auto& k : arr) {
    Chunk* c = w->chunk(k.dim, k.cx, k.cz);
    if (!c) continue;
    mergeBorders(*c);
    propagate(0); propagate(1);
  }
  std::vector<ivec3> ch; ch.swap(changes);
  for (auto& p : ch) applyChange(p);
  for (auto& k : touched) w->markSectionDirty(k.dim, k.cx, k.sy, k.cz);
  touched.clear(); lastTouched = {-1, 0, 0, 0};
  lastCells = cells; lastMs = (nowSeconds() - t0) * 1000;
}

} // namespace dq
