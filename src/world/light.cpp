#include "world/light.h"
#include "world/light_color.h"
#include <algorithm>
#include <cstring>

namespace dq {

u8 skyTopLevel(int dim) { return dim == DIM_NETHER ? 0 : 15; }

// A chiseled (carved) block of an opaque material takes light in, so its open octants and the faces beside them are
// lit, but passes none on: light doesn't leak through walls of slabs, stairs and carved blocks.
static inline bool carvedSink(const Registry& R, const Chunk& c, int lx, int ly, int lz, BlockId id) {
  return R.tOpaque[id] && c.getCarve(lx, ly, lz);
}

static bool zero3(const u8* c) { return !c[0] && !c[1] && !c[2]; }
static bool fullWhite(const u8* c) { return c[0] == 255 && c[1] == 255 && c[2] == 255; }
static void copy3(u8* d, const u8* s) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
static void sub1(u8& v, int a) { v = (u8)(v > a ? v - a : 0); }
static void sub3(u8* c, int a) { sub1(c[0], a); sub1(c[1], a); sub1(c[2], a); }

// Water takes Cubyz's (9, 5, 1). Stained glass takes that dye's absorbedLight, a different amount on
// each channel, so white light comes out the color of the glass. Every other attenuating block takes
// att*16 on all three channels (leaves att 1 -> 16, plus the step of 8). Opaque blocks are not entered.
static void absorb(u8* c, const Registry& R, BlockId id) {
  if (!id) return;
  if (R.tLiquid[id] == 1) { sub1(c[0], 9); sub1(c[1], 5); sub1(c[2], 1); return; }
  if (R.tAbsR[id] || R.tAbsG[id] || R.tAbsB[id]) {
    sub1(c[0], R.tAbsR[id]); sub1(c[1], R.tAbsG[id]); sub1(c[2], R.tAbsB[id]);
    return;
  }
  if (R.tAtt[id]) sub3(c, (int)R.tAtt[id] * 16);
}

// A sun column stops at the first block that changes the light. Stained glass is clear to the mesher
// (not opaque, lightAtt 0) but it still filters, so the cells under a glass ceiling are flooded
// through the glass instead of being painted full white.
static bool stopsSky(const Registry& R, BlockId b) {
  return b && (R.tOpaque[b] || R.tAtt[b] || R.tLiquid[b] || R.tAbsR[b] || R.tAbsG[b] || R.tAbsB[b]);
}

// One step out of a cell. Full white sun going straight down (d == 3, -Y) does not decay. After water
// has pulled a channel below 255 the next downward step does decay.
static void stepFrom(u8* dst, const u8* src, bool sun, int dir, const Registry& R, BlockId dest) {
  copy3(dst, src);
  if (!(sun && dir == 3 && fullWhite(src))) sub3(dst, 8);
  absorb(dst, R, dest);
}

static bool greater3(const u8* a, const u8* b) { return a[0] > b[0] || a[1] > b[1] || a[2] > b[2]; }
static void maxUp(u8* d, const u8* s, bool& up) {
  for (int i = 0; i < 3; i++) if (s[i] > d[i]) { d[i] = s[i]; up = true; }
}

struct Rgb3 { u8 c[3]{}; };

// ---------------------------------------------------------------- first pass (worker thread)
void initialChunkLight(Chunk& c) {
  const Registry& R = reg();
  const u8 skyTop = skyTopLevel(c.dim);
  thread_local std::vector<Rgb3> SKY, BL;
  thread_local std::vector<u32> Q;
  SKY.assign(16 * 16 * DIM_H, {});
  BL.assign(16 * 16 * DIM_H, {});
  auto idx = [](int x, int y, int z) { return (y << 8) | (z << 4) | x; };
  // 1. direct sunlight down each column until the first light-stopping block. The open air is full white.
  int top[256];
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    int t = -1;
    for (int y = std::min(DIM_H - 1, c.maxY + 1); y >= 0; y--) {
      BlockId b = c.get(x, y, z);
      if (stopsSky(R, b)) { t = y; break; }
    }
    top[z * 16 + x] = t;
    if (skyTop) for (int y = t + 1; y < DIM_H; y++) { u8* s = SKY[idx(x, y, z)].c; s[0] = s[1] = s[2] = 255; }
    if (skyTop && t >= 0 && carvedSink(R, c, x, t, z, c.get(x, t, z))) {
      u8* s = SKY[idx(x, t, z)].c; s[0] = s[1] = s[2] = 255;
    }
  }
  auto flood = [&](std::vector<Rgb3>& L, bool sky) {
    size_t h = 0;
    while (h < Q.size()) {
      u32 p = Q[h++];
      int x = p & 15, z = (p >> 4) & 15, y = (int)(p >> 8);
      const u8* cur = L[idx(x, y, z)].c;
      if (zero3(cur)) continue;
      BlockId id = c.get(x, y, z);
      if (carvedSink(R, c, x, y, z, id) && (sky || !R.metaLight(id, c.getMeta(x, y, z)))) continue;
      for (int d = 0; d < 6; d++) {
        int nx = x, ny = y, nz = z;
        if (d == 0) { if (++nx > 15) continue; } else if (d == 1) { if (--nx < 0) continue; }
        else if (d == 2) { if (++ny >= DIM_H) continue; } else if (d == 3) { if (--ny < 0) continue; }
        else if (d == 4) { if (++nz > 15) continue; } else { if (--nz < 0) continue; }
        BlockId nb = c.get(nx, ny, nz);
        if (R.tOpaque[nb] && !c.getCarve(nx, ny, nz)) continue;
        u8 stepped[3];
        stepFrom(stepped, cur, sky, d, R, nb);
        if (zero3(stepped)) continue;
        Rgb3& dst = L[idx(nx, ny, nz)];
        bool up = false; maxUp(dst.c, stepped, up);
        if (up && !R.tOpaque[nb]) Q.push_back((u32)(nx | (nz << 4) | (ny << 8)));
      }
    }
    Q.clear();
  };
  // 2. seeds: lit cells beside taller columns, the cell above a see-through top, and a see-through ceiling
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
      if (t == DIM_H - 1 && !R.tOpaque[c.get(x, t, z)]) {
        u8 e[3] = {255, 255, 255};
        absorb(e, R, c.get(x, t, z));
        copy3(SKY[idx(x, t, z)].c, e);
        Q.push_back((u32)(x | (z << 4) | (t << 8)));
      }
    }
    flood(SKY, true);
  }
  // 3. block light from emitters (level 15 -> 255 on the brightest channel)
  Q.clear();
  for (int sy = 0; sy < SECS; sy++) {
    const Section* s = c.section(sy);
    if (!s) continue;
    for (int i = 0; i < 4096; i++) {
      BlockId id = s->blocks[i];
      if (!id || !(R.tLight[id] || R.tLMeta[id])) continue;
      u8 level = R.metaLight(id, s->meta[i]);
      if (!level) continue;
      int x = i & 15, z = (i >> 4) & 15, y = sy * 16 + (i >> 8);
      blockLightRgb(id, level, BL[idx(x, y, z)].c);
      Q.push_back((u32)(x | (z << 4) | (y << 8)));
    }
  }
  flood(BL, false);
  // 4. store. A uniform section stays unallocated and remembers its rgb (open sky is full sun, a nether cave is dark).
  for (int sy = 0; sy < SECS; sy++) {
    int base = sy * 4096;
    auto cell = [&](int i) {
      LightRgb L;
      copy3(L.s, SKY[base + i].c);
      copy3(L.b, BL[base + i].c);
      return L;
    };
    LightRgb first = cell(0);
    Section* s = c.section(sy);
    if (!s) {
      bool uniform = true;
      for (int i = 1; i < 4096 && uniform; i++) uniform = cell(i) == first;
      if (uniform) { c.airRgb[sy] = first; c.airLight[sy] = lightNibble(first); continue; }
      s = c.ensure(sy);
    }
    for (int i = 0; i < 4096; i++) { LightRgb L = cell(i); s->rgb[i] = L; s->light[i] = lightNibble(L); }
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
LightEngine::R3 LightEngine::getC(int x, int y, int z, int ch) {
  R3 r;
  Chunk* c = chunkAt(x, y, z);
  if (!c) return r;   // a missing chunk contributes nothing (an unallocated section still has its air rgb)
  LightRgb L = c->getLightRgb(x & 15, y & 255, z & 15);
  copy3(r.c, ch ? L.b : L.s);
  return r;
}
void LightEngine::setC(int x, int y, int z, int ch, const R3& v) {
  Chunk* c = chunkAt(x, y, z);
  if (!c) return;
  int lx = x & 15, ly = y & 255, lz = z & 15;
  LightRgb L = c->getLightRgb(lx, ly, lz);
  u8* dst = ch ? L.b : L.s;
  if (dst[0] == v.c[0] && dst[1] == v.c[1] && dst[2] == v.c[2]) return;
  copy3(dst, v.c);
  c->setLightRgb(lx, ly, lz, L);
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
LightEngine::R3 LightEngine::emission(int x, int y, int z, int ch, BlockId id, u8 meta) {
  R3 e;
  const Registry& R = reg();
  if (ch == 0) {
    if ((y & 255) != 255 || R.tOpaque[id]) return e;
    e.c[0] = e.c[1] = e.c[2] = 255;
    absorb(e.c, R, id);
    return e;
  }
  blockLightRgb(id, R.metaLight(id, meta), e.c);
  return e;
}

LightEngine::Node LightEngine::nodeAt(int x, int y, int z, const u8* c) {
  Node n;
  n.x = x; n.y = y; n.z = z;
  n.c[0] = c[0]; n.c[1] = c[1]; n.c[2] = c[2];
  return n;
}

void LightEngine::propagate(int ch) {
  const Registry& R = reg();
  auto& Q = inc[ch];
  for (size_t h = 0; h < Q.size(); h++) {
    Node n = Q[h];
    R3 cur = getC(n.x, n.y, n.z, ch);
    if (zero3(cur.c)) continue;
    Chunk* nc = chunkAt(n.x, n.y, n.z);
    if (!nc) continue;
    int lx = n.x & 15, ly = n.y & 255, lz = n.z & 15;
    BlockId id = nc->get(lx, ly, lz);
    if (carvedSink(R, *nc, lx, ly, lz, id) && (ch == 0 || !R.metaLight(id, nc->getMeta(lx, ly, lz)))) continue;
    for (int d = 0; d < 6; d++) {
      int nx = n.x + (d == 0) - (d == 1), ny = n.y + (d == 2) - (d == 3), nz = n.z + (d == 4) - (d == 5);
      Chunk* c = chunkAt(nx, ny, nz);
      if (!c) continue;
      int nlx = nx & 15, nly = ny & 255, nlz = nz & 15;
      BlockId nb = c->get(nlx, nly, nlz);
      if (R.tOpaque[nb] && !c->getCarve(nlx, nly, nlz)) continue;
      u8 stepped[3];
      stepFrom(stepped, cur.c, ch == 0, d, R, nb);
      if (zero3(stepped)) continue;
      R3 dst = getC(nx, ny, nz, ch);
      bool up = false; maxUp(dst.c, stepped, up);
      if (!up) continue;
      setC(nx, ny, nz, ch, dst);
      Q.push_back(nodeAt(nx, ny, nz, dst.c));
    }
  }
  Q.clear();
}

void LightEngine::unpropagate(int ch) {
  const Registry& R = reg();
  auto& Q = rem[ch];
  for (size_t h = 0; h < Q.size(); h++) {
    Node n = Q[h];
    for (int d = 0; d < 6; d++) {
      int nx = n.x + (d == 0) - (d == 1), ny = n.y + (d == 2) - (d == 3), nz = n.z + (d == 4) - (d == 5);
      Chunk* c = chunkAt(nx, ny, nz);
      if (!c) continue;
      R3 nl = getC(nx, ny, nz, ch);
      if (zero3(nl.c)) continue;
      // A channel is removed only when it is strictly dimmer than the light that went away, or it is the
      // full-white sun directly below another full-white sun. Brighter or equal channels belong to some
      // other light and are spread again afterwards, one channel at a time.
      const bool sunDown = ch == 0 && d == 3 && fullWhite(n.c) && fullWhite(nl.c);
      R3 kept = nl, gone{};
      bool dep = false, indep = false;
      for (int i = 0; i < 3; i++) {
        if (!nl.c[i]) continue;
        if (nl.c[i] < n.c[i] || (sunDown && n.c[i] == 255)) { kept.c[i] = 0; gone.c[i] = nl.c[i]; dep = true; }
        else indep = true;
      }
      if (dep) {
        setC(nx, ny, nz, ch, kept);
        if (!zero3(gone.c)) Q.push_back(nodeAt(nx, ny, nz, gone.c));
        int lx = nx & 15, ly = ny & 255, lz = nz & 15;
        R3 e = emission(nx, ny, nz, ch, c->get(lx, ly, lz), c->getMeta(lx, ly, lz));
        bool boost = false;
        for (int i = 0; i < 3; i++) if (e.c[i] > kept.c[i]) { kept.c[i] = e.c[i]; boost = true; }
        if (boost) { setC(nx, ny, nz, ch, kept); inc[ch].push_back(nodeAt(nx, ny, nz, kept.c)); }
      }
      if (indep) inc[ch].push_back(nodeAt(nx, ny, nz, nl.c));
    }
  }
  Q.clear();
}

void LightEngine::onBlockChanged(World& world, int x, int y, int z, BlockId o, BlockId n) { (void)world; (void)o; (void)n; changes.push_back({x, y, z}); }
void LightEngine::onChunkLoaded(World& world, Chunk& c) { (void)world; arrivals.push_back(c.key()); }

void LightEngine::applyChange(const ivec3& p) {
  const Registry& R = reg();
  Chunk* c = chunkAt(p.x, p.y, p.z);
  if (!c) return;
  int lx = p.x & 15, ly = p.y & 255, lz = p.z & 15;
  BlockId id = c->get(lx, ly, lz); u8 meta = c->getMeta(lx, ly, lz);
  const bool carved = R.tOpaque[id] && c->getCarve(lx, ly, lz) != 0;   // a seat on a torch is not a light sink
  for (int ch = 0; ch < 2; ch++) {
    R3 cur = getC(p.x, p.y, p.z, ch);
    R3 e = emission(p.x, p.y, p.z, ch, id, meta);
    if (!zero3(cur.c)) { setC(p.x, p.y, p.z, ch, {}); rem[ch].push_back(nodeAt(p.x, p.y, p.z, cur.c)); unpropagate(ch); }
    if (!zero3(e.c)) { setC(p.x, p.y, p.z, ch, e); inc[ch].push_back(nodeAt(p.x, p.y, p.z, e.c)); }
    if (!R.tOpaque[id] || !zero3(e.c) || carved) for (int d = 0; d < 6; d++) {
      int nx = p.x + (d == 0) - (d == 1), ny = p.y + (d == 2) - (d == 3), nz = p.z + (d == 4) - (d == 5);
      if (!chunkAt(nx, ny, nz)) continue;
      R3 l = getC(nx, ny, nz, ch);
      if (l.c[0] >= 8 || l.c[1] >= 8 || l.c[2] >= 8 || fullWhite(l.c)) inc[ch].push_back(nodeAt(nx, ny, nz, l.c));
    }
    propagate(ch);
  }
}

void LightEngine::mergeBorders(Chunk& c) {
  // the brighter side of a chunk or band border floods across. Each channel is its own comparison,
  // so a red light and a blue light on opposite sides both cross.
  auto pair = [&](int ax, int ay, int az, int bx, int by, int bz) {
    for (int ch = 0; ch < 2; ch++) {
      R3 a = getC(ax, ay, az, ch), b = getC(bx, by, bz, ch);
      if (greater3(a.c, b.c)) inc[ch].push_back(nodeAt(ax, ay, az, a.c));
      if (greater3(b.c, a.c)) inc[ch].push_back(nodeAt(bx, by, bz, b.c));
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
  // the bands above and below (the stacked world is one column). Full sun crosses the border undiminished.
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
