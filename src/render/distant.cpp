// render/distant.cpp — see distant.h. The main-thread half of game/distant.js: which tiles to draw
// from here (distSelect: coarse far off, finer near, a tile split only once all four of its quarters
// are ready), what to make next (distWork, on the thread pool instead of a few ms a frame), summaries
// of real chunks (loaded, or out of the save) and drawing (drawDistant: cut away chunk by chunk where
// the real chunks are drawn, the storm's shadow and rain over it, fog at the far edge).
#include "render/distant.h"
#include <cstring>
#include "render/sky.h"
#include "sim/level.h"
#include "sim/weather.h"
#include "world/region.h"
#include "assets/tileset.h"

namespace dq {

using namespace lod;

namespace {
const std::vector<VertexAttrib> LOD_ATTRS = {
  {0, 2, GL_UNSIGNED_SHORT, GL_FALSE, false, 0},
  {1, 1, GL_SHORT, GL_FALSE, false, 4},
  {2, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 6},
  {3, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 8},    // tint 5-6-5
  {4, 1, GL_UNSIGNED_SHORT, GL_FALSE, true, 10},   // flags | face << 4 | shade << 7
};
inline u64 tkey(int L, int tx, int tz) { return ((u64)L << 58) | ((u64)((u32)tx & 0x1FFFFFFFu) << 29) | (u64)((u32)tz & 0x1FFFFFFFu); }
inline u64 rkey(int dim, int rx, int rz) { return ((u64)dim << 60) | ((u64)((u32)rx & 0x3FFFFFFFu) << 30) | (u64)((u32)rz & 0x3FFFFFFFu); }
int topLevel(float range) { int L = 2; while (L < MAX_LEVEL && tileSize(L) < range / 4) L++; return L; }
u32 chunkHash(const Chunk& c) {
  u32 h = 2166136261u ^ (u32)c.maxY;
  for (int i = 0; i < 256; i++) { int y = c.heightMap[i]; h = (h ^ (u32)y) * 16777619u; h = (h ^ (u32)c.get(i & 15, y, i >> 4)) * 16777619u; }
  return h;
}
} // namespace

constexpr double FADE_S = 0.9;   // seconds a new tile takes to fade in
static const double REFINE = [] { const char* e = std::getenv("DQ_LOD_REFINE"); double v = e ? std::atof(e) : 0; return v > 0.5 ? v : 4.5; }();
struct DistantTerrain::Tile {
  int L = 0, tx = 0, tz = 0;
  Mesh mesh; size_t quads = 0; size_t bytes = 0;
  float minY = 0, maxY = 0;
  u64 used = 0;
  bool dirty = false, building = false, ready = false;
  double readyAt = -1e9;          // when it was first made (its fade in)
  double madeAt = -1e9;           // when it was last made: a big tile changing as the far land is generated waits a while
  float fadeIn = 1; vec4 fadeOut{0};   // this frame: how far in it has faded; a parent's four quarters, how far their children have
};
struct DistantTerrain::Band {
  int dim = 0;
  std::unordered_map<u64, std::unique_ptr<Tile>> tiles;
  std::vector<Tile*> draw;
  struct Want { int L, tx, tz; float pri; };
  std::vector<Want> want;
  GLuint maskTex = 0; int maskN = 0, maskCx0 = 0, maskCz0 = 0; std::vector<u8> mask;
  bool active = false;
  ~Band() { if (maskTex) glDeleteTextures(1, &maskTex); }
};

DistantTerrain::DistantTerrain() { for (int d = 0; d < DIM_COUNT; d++) { bands[d] = std::make_unique<Band>(); bands[d]->dim = d; } }
DistantTerrain::~DistantTerrain() { saveFarCache(); }

// ---- the far land's cache: header (magic, tag), the block keys used, then each chunk's voxel runs; zlib'd ----------
namespace {
void putU32(std::vector<u8>& o, u32 v) { for (int i = 0; i < 4; i++) o.push_back((u8)(v >> (8 * i))); }
void putU16(std::vector<u8>& o, u16 v) { o.push_back((u8)v); o.push_back((u8)(v >> 8)); }
struct Rd { const u8* p; size_t n, i = 0; bool ok = true;
  u8 b() { if (i >= n) { ok = false; return 0; } return p[i++]; }
  u16 h() { u16 a = b(); return (u16)(a | b() << 8); }
  u32 w() { u32 a = h(); return a | (u32)h() << 16; } };
}
void DistantTerrain::saveFarCache() {
  if (cacheFile.empty() || !cacheLoaded) return;
  std::vector<std::pair<ChunkKey, const ChunkSummary*>> list;
  for (auto& [k, sp] : sums) if (sp && sp->generated) list.push_back({k, sp.get()});
  if (list.empty()) return;
  const Registry& R = reg();
  std::unordered_map<u16, u16> pal; std::vector<u16> ids;
  for (auto& [k, sm] : list) for (auto& r : sm->runs) if (!pal.count(r.id)) { pal[r.id] = (u16)ids.size(); ids.push_back(r.id); }
  std::vector<u8> o;
  putU32(o, (u32)cacheTag.size()); o.insert(o.end(), cacheTag.begin(), cacheTag.end());
  putU32(o, (u32)ids.size());
  for (u16 id : ids) { const std::string& key = R.block(id).key; putU16(o, (u16)key.size()); o.insert(o.end(), key.begin(), key.end()); }
  putU32(o, (u32)list.size());
  for (auto& [k, sm] : list) {
    putU32(o, (u32)k.dim); putU32(o, (u32)k.cx); putU32(o, (u32)k.cz);
    for (u16 v : sm->start) putU16(o, v);
    o.insert(o.end(), sm->temp, sm->temp + 64); o.insert(o.end(), sm->humid, sm->humid + 64);
    for (auto& r : sm->runs) { o.push_back(r.y0); o.push_back(r.y1); putU16(o, pal[r.id]); }
  }
  std::vector<u8> z = zCompress(o.data(), o.size(), 3);
  std::error_code ec; fs::path tmp = cacheFile; tmp += ".tmp";
  if (FILE* f = std::fopen(tmp.string().c_str(), "wb")) {
    const char magic[8] = {'D', 'Q', 'L', 'O', 'D', 'F', '1', 0};
    std::fwrite(magic, 1, 8, f); u32 raw = (u32)o.size(); std::fwrite(&raw, 4, 1, f); std::fwrite(z.data(), 1, z.size(), f); std::fclose(f);
    fs::rename(tmp, cacheFile, ec);
    DQ_LOG("distant terrain: kept %zu generated chunks of far land (%.1f MB)", list.size(), z.size() / 1048576.0);
  }
}

void DistantTerrain::reset() {
  saveFarCache();
  for (auto& b : bands) { b->tiles.clear(); b->draw.clear(); b->want.clear(); b->active = false; }
  sums.clear(); sumRegions.clear(); seen.clear(); scanOrder.clear(); scanPos = 0;
  shared = std::make_shared<Shared>();   // results of jobs still running are dropped
  inFlight = 0; storedInFlight = 0; storedRequested = false; storedPos = 0; storedKeys.clear();
  gpuBytes = 0; tilesHeld = 0; summaries = 0;
  genTried.clear(); genInFlight = 0; genCx = 1 << 30; genDim = -1; genRing = 0; genIdx = 0;
  cacheFile.clear(); cacheLoaded = true;
}

void DistantTerrain::markDirty(int dim, int cx, int cz) {
  Band& b = *bands[dim];
  for (int L = 0; L <= MAX_LEVEL; L++) {
    int s = tileSize(L);
    auto it = b.tiles.find(tkey(L, floordiv(cx * 16, s), floordiv(cz * 16, s)));
    if (it != b.tiles.end()) it->second->dirty = true;
  }
}

void DistantTerrain::addSummary(const ChunkKey& k, std::shared_ptr<ChunkSummary> s, bool quiet) {
  auto it = sums.find(k);
  bool changed = true;
  if (it != sums.end()) {
    // the tiles that show it need making again only if its top has changed
    const ChunkSummary& o = *it->second; changed = false;
    // (any voxel changed: a block placed or dug that shows at 2x2 blocks)
    changed = o.runs.size() != s->runs.size() || std::memcmp(o.start, s->start, sizeof o.start) != 0;
    for (size_t q = 0; q < o.runs.size() && !changed; q++) if (o.runs[q].y0 != s->runs[q].y0 || o.runs[q].y1 != s->runs[q].y1 || o.runs[q].id != s->runs[q].id) changed = true;
    it->second = s;
  } else {
    sums[k] = s;
    sumRegions[rkey(k.dim, k.cx >> 4, k.cz >> 4)]++;
    // (kept small by trimSummaries: the farthest go first)
  }
  summaries = sums.size();
  if (changed && !quiet) markDirty(k.dim, k.cx, k.cz);
}

// at most sixteen thousand chunks' voxels (a few hundred runs each, about 25 MB): past that the farthest are let go (and
// may be made again if you come back toward them)
void DistantTerrain::trimSummaries(glm::dvec3 cam) {
  const size_t CAP = 16000, KEEP = 14000;
  if (sums.size() <= CAP) return;
  std::vector<std::pair<double, ChunkKey>> v; v.reserve(sums.size());
  for (auto& [k, s] : sums) v.push_back({std::hypot(k.cx * 16 + 8 - cam.x, k.cz * 16 + 8 - cam.z) + (k.dim == dimOfY((int)std::floor(cam.y)) ? 0 : 1e6), k});
  std::nth_element(v.begin(), v.begin() + KEEP, v.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for (size_t i = KEEP; i < v.size(); i++) {
    const ChunkKey& k = v[i].second;
    auto rk = rkey(k.dim, k.cx >> 4, k.cz >> 4);
    if (--sumRegions[rk] <= 0) sumRegions.erase(rk);
    sums.erase(k); genTried.erase(k);
  }
  summaries = sums.size();
}

void DistantTerrain::genFar(World& w, int dim, glm::dvec3 cam, ThreadPool& pool) {
  const int maxJobs = pool.threads() > 2 ? 2 : 1;
  if (!gen || !colors || genInFlight >= maxJobs || dim < 0 || dim >= DIM_COUNT || !gen->forDim(dim)) return;
  const int ccx = (int)std::floor(cam.x / 16), ccz = (int)std::floor(cam.z / 16);
  if (ccx != genCx || ccz != genCz || dim != genDim) { genCx = ccx; genCz = ccz; genDim = dim; genRing = 0; genIdx = 0; }
  // out to the far land's reach, at most 1024 blocks (past it the voxels are 32+ blocks wide: the generator's columns do)
  const int maxRing = std::min(64, (int)(range / 16) + 1);
  std::vector<ChunkKey> batch;
  int looked = 0;
  while (batch.size() < 4 && genRing <= maxRing && looked < 4096) {
    const int r = genRing, n = r == 0 ? 1 : 8 * r;
    if (genIdx >= n) { genRing++; genIdx = 0; continue; }
    const int t = genIdx++, side = r ? t / (2 * r) : 0, off = r ? t % (2 * r) : 0;
    const int dx = r == 0 ? 0 : side == 0 ? -r + off : side == 1 ? r : side == 2 ? r - off : -r;
    const int dz = r == 0 ? 0 : side == 0 ? -r : side == 1 ? -r + off : side == 2 ? r : r - off;
    looked++;
    if (dx * dx + dz * dz > maxRing * maxRing) continue;
    const ChunkKey k{dim, ccx + dx, ccz + dz};
    if (sums.count(k) || seen.count(k) || genTried.count(k) || w.chunk(dim, k.cx, k.cz)) continue;
    genTried.insert(k); batch.push_back(k);
  }
  if (batch.empty()) return;
  genInFlight++;
  auto sh = shared; auto g = gen; auto cols = colors;
  pool.submit([sh, g, cols, batch] {
    std::vector<std::pair<ChunkKey, std::shared_ptr<ChunkSummary>>> got;
    for (const ChunkKey& k : batch) {
      DimGenerator* dg = g->forDim(k.dim);
      if (!dg) continue;
      Chunk c(k.dim, k.cx, k.cz);
      dg->generate(c);
      c.recomputeMaxY();
      auto sm = summarizeChunk(c, *cols); sm->generated = true;
      got.push_back({k, sm});
    }
    std::lock_guard<std::mutex> lk(sh->mx);
    for (auto& x : got) sh->gen.push_back(std::move(x));
    sh->genBatches++;
  }, 1000);
}

bool DistantTerrain::chunkReal(int dim, int cx, int cz) const {
  if (realR < 1e8f) { const double dx = cx * 16 + 8 - camAt.x, dz = cz * 16 + 8 - camAt.z; if (dx * dx + dz * dz > (double)realR * realR) return false; }
  auto it = seen.find({dim, cx, cz});
  return it != seen.end() && clock - it->second.first > 0.6;
}

// loaded chunks: summarized when they arrive (a few a frame), again when their top changes
void DistantTerrain::scanChunks(World& w, double now) {
  int budget = 6;
  for (auto& [k, cp] : w.chunks()) {
    const Chunk* c = cp.get();
    auto it = seen.find(k);
    if (it == seen.end() || it->second.ptr != c) {
      if (budget <= 0) continue;
      budget--;
      u32 h = chunkHash(*c);
      seen[k] = {c, h, now, frame};
      scanOrder.push_back(k);
      addSummary(k, summarizeChunk(*c, *colors), false);
    } else it->second.stamp = frame;
  }
  // round robin: has a chunk's top changed? (a build, a crater, a felled tree)
  for (int n = 0; n < 24 && !scanOrder.empty(); n++) {
    if (scanPos >= scanOrder.size()) scanPos = 0;
    ChunkKey k = scanOrder[scanPos];
    auto it = seen.find(k);
    const Chunk* c = w.chunk(k.dim, k.cx, k.cz);
    if (it == seen.end() || !c || c != it->second.ptr) { if (it != seen.end() && !c) seen.erase(it); scanOrder[scanPos] = scanOrder.back(); scanOrder.pop_back(); continue; }
    u32 h = chunkHash(*c);
    if (h != it->second.hash) { it->second.hash = h; addSummary(k, summarizeChunk(*c, *colors), false); }
    scanPos++;
  }
  // forget chunks that went away (their summaries stay)
  if (frame % 30 == 0) for (auto it = seen.begin(); it != seen.end();) { if (!w.chunk(it->first.dim, it->first.cx, it->first.cz)) it = seen.erase(it); else ++it; }
}

void DistantTerrain::updateMask(Band& b, const FrameCtx& f, int maskR) {
  int M = 2 * maskR + 1;
  int ccx = ifloor(f.cam.pos.x) >> 4, ccz = ifloor(f.cam.pos.z) >> 4;
  b.maskCx0 = ccx - maskR; b.maskCz0 = ccz - maskR;
  b.mask.assign((size_t)M * M, 0);
  for (int j = 0; j < M; j++) for (int i = 0; i < M; i++) if (chunkReal(b.dim, b.maskCx0 + i, b.maskCz0 + j)) b.mask[j * M + i] = 255;
  glActiveTexture(GL_TEXTURE4);
  if (!b.maskTex) glGenTextures(1, &b.maskTex);
  glBindTexture(GL_TEXTURE_2D, b.maskTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (b.maskN != M) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, M, M, 0, GL_RED, GL_UNSIGNED_BYTE, b.mask.data());
    b.maskN = M;
  } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, M, M, GL_RED, GL_UNSIGNED_BYTE, b.mask.data());
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glActiveTexture(GL_TEXTURE0);
}

// which tiles to draw from here (distSelect)
void DistantTerrain::select(Band& b, double cx, double cz) {
  b.draw.clear(); b.want.clear();
  int Lt = topLevel(range);
  double S = tileSize(Lt);
  int t0x = (int)std::floor((cx - range) / S), t1x = (int)std::floor((cx + range) / S), t0z = (int)std::floor((cz - range) / S), t1z = (int)std::floor((cz + range) / S);
  double maskReach = (b.maskN / 2) * 16.0;
  auto covered = [&](int x0, int z0, int s, double far) {
    if (far > maskReach - 8) return false;
    for (int cz2 = z0 >> 4; cz2 < (z0 + s) >> 4; cz2++) for (int cx2 = x0 >> 4; cx2 < (x0 + s) >> 4; cx2++) if (!chunkReal(b.dim, cx2, cz2)) return false;
    return true;
  };
  std::function<void(int, int, int)> visit = [&](int L, int tx, int tz) {
    int s = tileSize(L); double x0 = (double)tx * s, z0 = (double)tz * s;
    double nx = std::max(x0, std::min(cx, x0 + s)), nz = std::max(z0, std::min(cz, z0 + s));
    if (std::hypot(nx - cx, nz - cz) > range) return;
    double far = std::max(std::max(std::hypot(x0 - cx, z0 - cz), std::hypot(x0 + s - cx, z0 - cz)), std::max(std::hypot(x0 - cx, z0 + s - cz), std::hypot(x0 + s - cx, z0 + s - cz)));
    if (covered((int)x0, (int)z0, s, far)) return;   // all of it is real chunks, drawn
    auto it = b.tiles.find(tkey(L, tx, tz));
    Tile* t = it == b.tiles.end() ? nullptr : it->second.get();
    double mid = std::hypot(x0 + s / 2.0 - cx, z0 + s / 2.0 - cz);
    // a tile splits into its four finer ones while it's nearer than REFINE tile-widths: a voxel then stays about
    // distance/150 across, a few pixels on screen at any distance, as Cubyz keeps its far land (at 1.25 it was
    // distance/40: far hills became stairs of 32-block steps, worst at the wide edges of the view)
    if (L > 0 && mid < s * REFINE) {
      int ks = tileSize(L - 1), live = 0; bool ready = true;
      for (int q = 0; q < 4; q++) {
        int a = tx * 2 + (q & 1), c = tz * 2 + (q >> 1); double kx = (double)a * ks, kz = (double)c * ks;
        if (std::hypot(std::max(kx, std::min(cx, kx + ks)) - cx, std::max(kz, std::min(cz, kz + ks)) - cz) > range) continue;
        live |= 1 << q;
        auto kt = b.tiles.find(tkey(L - 1, a, c));
        if (kt == b.tiles.end() || !kt->second->ready) ready = false;
      }
      if (ready) {
        for (int q = 0; q < 4; q++) if (live >> q & 1) visit(L - 1, tx * 2 + (q & 1), tz * 2 + (q >> 1));
        // the finer tiles fade in over this one (a dither, pixel by pixel: Horizon's fade), so the land doesn't pop
        vec4 fo(0); bool fading = false;
        for (int q = 0; q < 4; q++) if (live >> q & 1) {
          auto kt = b.tiles.find(tkey(L - 1, tx * 2 + (q & 1), tz * 2 + (q >> 1)));
          float f = kt == b.tiles.end() ? 1.f : (float)std::min(1.0, (clock - kt->second->readyAt) / FADE_S);
          fo[q] = f; if (f < 1) fading = true;
        }
        if (fading && t && t->ready) { t->fadeIn = 1; t->fadeOut = fo; b.draw.push_back(t); t->used = frame; }
        return;
      }
      for (int q = 0; q < 4; q++) if (live >> q & 1) { int a = tx * 2 + (q & 1), c = tz * 2 + (q >> 1); if (!b.tiles.count(tkey(L - 1, a, c))) b.want.push_back({L - 1, a, c, (float)mid}); }
    }
    if (t && t->ready) {
      t->fadeOut = vec4(0); t->fadeIn = (float)std::min(1.0, (clock - t->readyAt) / FADE_S);
      b.draw.push_back(t); t->used = frame; if (t->dirty && !t->building && clock - t->madeAt > (L >= 3 ? 6.0 : L >= 1 ? 1.5 : 0.0)) b.want.push_back({L, tx, tz, (float)mid + 400});
    }
    else if (!t) b.want.push_back({L, tx, tz, (float)(mid - (Lt - L) * 2000)});   // coarse ones first
    else t->used = frame;
  };
  for (int tz = t0z; tz <= t1z; tz++) for (int tx = t0x; tx <= t1x; tx++) visit(Lt, tx, tz);
}

void DistantTerrain::submit(Band& b, int L, int tx, int tz, ThreadPool& pool, float pri) {
  auto& tp = b.tiles[tkey(L, tx, tz)];
  if (!tp) { tp = std::make_unique<Tile>(); tp->L = L; tp->tx = tx; tp->tz = tz; tp->used = frame; }
  if (tp->building) return;
  tp->building = true; tp->dirty = false;
  auto in = std::make_shared<TileInput>();
  in->dim = b.dim; in->L = L; in->tx = tx; in->tz = tz; in->gen = gen; in->colors = colors;
  // real chunks' surfaces under the cells, if any are known round here
  int cs = cellSize(L), size = tileSize(L), W = CELLS + 2;
  int x0 = tx * size - cs, z0 = tz * size - cs, x1 = tx * size + size + cs, z1 = tz * size + size + cs;
  bool any = false;
  for (int rz = z0 >> 8; rz <= (z1 >> 8) && !any; rz++) for (int rx = x0 >> 8; rx <= (x1 >> 8) && !any; rx++) if (sumRegions.count(rkey(b.dim, rx, rz))) any = true;
  if (any) {
    // a cell's centre at level 0; past it, the centres of its four quarters (a coarse voxel is made from finer ones)
    const int Q = L == 0 ? 1 : 4;
    in->sums.resize((size_t)W * W * Q);
    for (int j = 0; j < W; j++) for (int i = 0; i < W; i++) for (int q = 0; q < Q; q++) {
      const double cx = tx * (double)size + (i - 1 + 0.5) * cs, cz = tz * (double)size + (j - 1 + 0.5) * cs;
      const int fx = Q == 1 ? (int)std::floor(cx) : (int)std::floor(std::floor(cx) + ((q & 1) ? 0.25 : -0.25) * cs);
      const int fz = Q == 1 ? (int)std::floor(cz) : (int)std::floor(std::floor(cz) + ((q & 2) ? 0.25 : -0.25) * cs);
      auto it = sums.find({b.dim, fx >> 4, fz >> 4});
      if (it != sums.end()) in->sums[((size_t)j * W + i) * Q + q] = it->second;
    }
  }
  inFlight++;
  auto sh = shared; int dim = b.dim;
  pool.submit([sh, in, dim, L, tx, tz] {
    Done d{dim, L, tx, tz, {}};
    buildTile(*in, d.out);
    std::lock_guard<std::mutex> lk(sh->mx);
    sh->done.push_back(std::move(d));
  }, pri);
}

void DistantTerrain::update(Level& lv, World& w, const FrameCtx& f, ThreadPool& pool, double dt, const TileSet& tiles) {
  clock += dt; frame++; camAt = f.cam.pos;
  const Settings* st = f.settings;
  int want = st ? st->farTerrain : 0, R = st ? st->renderDist : 8;
  bool on = want > 0 && f.cam.realm == REALM_MAIN && w.realm == REALM_MAIN && lv.gen;
  if (!on) {
    if (!sums.empty() || inFlight) reset();
    for (auto& b : bands) { b->tiles.clear(); b->draw.clear(); }
    range = 0; setSkyFarRange(0); drawn = 0; gpuBytes = 0;
    return;
  }
  lvl = &lv;
  if (&w != worldFor || lv.gen != gen) {
    reset(); worldFor = &w; gen = lv.gen; store = w.regionStoreShared();
    // the far land made last time, out of the world's folder (on a worker; nothing is generated again till it's in)
    if (!lv.folder.empty()) {
      cacheFile = lv.folder / "lod_far.bin";
      cacheTag = std::string(DQ_VERSION) + "/" + std::to_string(lv.info.genVersion) + "/" + std::to_string(lv.info.seed);
      cacheLoaded = false;
      auto sh = shared; fs::path file = cacheFile; std::string tag = cacheTag;
      pool.submit([sh, file, tag] {
        std::vector<std::pair<ChunkKey, std::shared_ptr<ChunkSummary>>> got;
        std::vector<u8> blob;
        if (FILE* f = std::fopen(file.string().c_str(), "rb")) {
          std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
          if (n > 12) { blob.resize((size_t)n); if (std::fread(blob.data(), 1, blob.size(), f) != blob.size()) blob.clear(); }
          std::fclose(f);
        }
        std::vector<u8> raw;
        if (blob.size() > 12 && std::memcmp(blob.data(), "DQLODF1", 8) == 0) {
          u32 rawN; std::memcpy(&rawN, blob.data() + 8, 4);
          if (zDecompress(blob.data() + 12, blob.size() - 12, raw, rawN)) {
            Rd r{raw.data(), raw.size()};
            const u32 tn = r.w(); std::string t(std::min<u32>(tn, 256), '\0'); for (auto& c : t) c = (char)r.b();
            const u32 np = r.ok && t == tag ? r.w() : 0;
            if (r.ok && t == tag && np < 65536) {
              const Registry& R = reg();
              std::vector<u16> ids(np);
              for (auto& id : ids) { std::string key(std::min<u16>(r.h(), 200), '\0'); for (auto& c : key) c = (char)r.b(); id = R.blockId(key); }
              const u32 count = r.w();
              for (u32 q = 0; q < count && r.ok; q++) {
                ChunkKey k{(int)r.w(), (int)r.w(), (int)r.w()};
                auto sm = std::make_shared<ChunkSummary>(); sm->generated = true;
                for (auto& v : sm->start) v = r.h();
                for (auto& v : sm->temp) v = r.b();
                for (auto& v : sm->humid) v = r.b();
                sm->runs.resize(sm->start[64]);
                for (auto& run : sm->runs) { run.y0 = r.b(); run.y1 = r.b(); u16 pi = r.h(); run.id = pi < ids.size() ? ids[pi] : AIR; }
                if (r.ok && k.dim >= 0 && k.dim < DIM_COUNT) got.push_back({k, sm});
              }
            }
          }
        }
        std::lock_guard<std::mutex> lk(sh->mx);
        for (auto& x : got) sh->gen.push_back(std::move(x));
        sh->cacheDone = true;
      }, -50);
    }
  }
  std::string ck = tiles.packName + ":" + std::to_string(tiles.count);
  if (!colors || colorsFor != ck) {   // a new resource pack: the far land takes its colours
    colors = makeColorTable(tiles); colorsFor = ck;
    for (auto& b : bands) for (auto& [k, t] : b->tiles) t->dirty = true;
    sums.clear(); sumRegions.clear(); seen.clear(); scanOrder.clear(); storedRequested = false; storedPos = 0; genTried.clear(); genCx = 1 << 30;
  }
  // on a machine that can't keep up it draws closer by itself, and reaches back out when it can
  float fps = dt > 0 ? (float)(1.0 / dt) : 60.f, cap = st && st->fpsLimit > 0 ? (float)st->fpsLimit : 60.f;
  if (fps < std::min(40.f, cap * 0.7f)) { adaptLow += (float)dt; adaptHigh = 0; } else if (fps > std::min(55.f, cap * 0.9f)) { adaptHigh += (float)dt; adaptLow = 0; } else { adaptLow = std::max(0.f, adaptLow - (float)dt); adaptHigh = 0; }
  if (!adaptive) { adaptScale = 1; adaptLow = adaptHigh = 0; }
  if (adaptLow > 5) { adaptScale = std::max(0.3f, adaptScale * 0.8f); adaptLow = 0; }
  if (adaptHigh > 12 && adaptScale < 1) { adaptScale = std::min(1.f, adaptScale * 1.15f); adaptHigh = 0; }
  range = std::max(std::round(want * adaptScale), R * 16.f + 160.f);
  maxBytes = (size_t)((96.0 + want / 16384.0 * 600.0) * 1048576.0);   // (finer tiles: more of them)
  // results from the workers: summaries out of the save, finished tiles (uploaded within a budget)
  {
    std::vector<Done> done; std::vector<std::pair<ChunkKey, std::shared_ptr<ChunkSummary>>> got; std::vector<ChunkKey> stored; bool listed = false;
    std::vector<std::pair<ChunkKey, std::shared_ptr<ChunkSummary>>> made; int batches = 0;
    { std::lock_guard<std::mutex> lk(shared->mx); done.swap(shared->done); got.swap(shared->sums); made.swap(shared->gen); batches = shared->genBatches; shared->genBatches = 0;
      if (shared->cacheDone) { shared->cacheDone = false; cacheLoaded = true; }
      if (shared->storedListed) { stored.swap(shared->stored); listed = true; shared->storedListed = false; } }
    genInFlight = std::max(0, genInFlight - batches);
    for (auto& [k, s] : made) { genTried.insert(k); if (s && !seen.count(k) && !sums.count(k)) { addSummary(k, s, false); generated++; } }
    if (listed) { storedKeys = std::move(stored); storedPos = 0; }
    for (auto& [k, s] : got) { storedInFlight = std::max(0, storedInFlight - 1); if (s && !seen.count(k)) addSummary(k, s, false); }
    double t0 = nowSeconds(); size_t i = 0;
    for (; i < done.size(); i++) {
      Done& d = done[i]; inFlight--;
      Band& b = *bands[d.dim];
      auto it = b.tiles.find(tkey(d.L, d.tx, d.tz));
      if (it == b.tiles.end()) continue;
      Tile& t = *it->second;
      t.building = false;
      gpuBytes -= std::min(gpuBytes, t.bytes);
      size_t quads = d.out.verts.size() / 4;
      t.mesh.setup(sizeof(LodVertex), LOD_ATTRS);
      if (quads) t.mesh.uploadQuads(d.out.verts.data(), d.out.verts.size() * sizeof(LodVertex), quads);
      else if (t.quads) t.mesh.release();
      if (!t.ready) t.readyAt = clock;   // (a remake of a tile already shown doesn't fade again)
      t.quads = quads; t.bytes = d.out.verts.size() * sizeof(LodVertex) + 64; t.minY = d.out.minY; t.maxY = d.out.maxY; t.ready = true; t.madeAt = clock;
      gpuBytes += t.bytes; built++;
      buildMsTotal += d.out.ms; buildMsMax = std::max(buildMsMax, d.out.ms);
      if ((nowSeconds() - t0) * 1000 > 3) { i++; break; }
    }
    if (i < done.size()) { std::lock_guard<std::mutex> lk(shared->mx); for (; i < done.size(); i++) shared->done.push_back(std::move(done[i])); }
  }
  scanChunks(w, clock);
  // chunks you've changed, out of the save: their real blocks, so your builds show from afar
  if (store && !storedRequested) {
    storedRequested = true;
    auto sh = shared; auto st2 = store;
    pool.submit([sh, st2] {
      std::vector<ChunkKey> all;
      for (int d = 0; d < DIM_COUNT; d++) { auto v = st2->listStored(d); all.insert(all.end(), v.begin(), v.end()); }
      std::lock_guard<std::mutex> lk(sh->mx); sh->stored = std::move(all); sh->storedListed = true;
    }, 900);
  }
  if (storedInFlight == 0 && storedPos < storedKeys.size() && store) {
    std::vector<ChunkKey> batch;
    while (storedPos < storedKeys.size() && batch.size() < 48) { ChunkKey k = storedKeys[storedPos++]; if (!seen.count(k) && !sums.count(k)) batch.push_back(k); }
    if (!batch.empty()) {
      storedInFlight = (int)batch.size();
      auto sh = shared; auto st2 = store; auto cols = colors;
      pool.submit([sh, st2, cols, batch] {
        std::vector<u8> blob;
        for (auto& k : batch) {
          std::shared_ptr<ChunkSummary> s;
          if (st2->load(k, blob)) { Chunk c(k.dim, k.cx, k.cz); if (decodeChunk(c, blob.data(), blob.size())) { c.recomputeMaxY(); s = summarizeChunk(c, *cols); } }
          std::lock_guard<std::mutex> lk(sh->mx);
          if (s) sh->sums.push_back({k, s}); else sh->sums.push_back({k, nullptr});
        }
      }, 950);
    }
  }
  // which lands: the one you're in (the only one drawn: game/seamless.js), and the one beyond the cloud bank
  // (Overworld <-> Skylands) made ready while you're in the bank, so it's there when you come out of it
  int d = f.dim;
  if (d != DIM_NETHER && cacheLoaded) genFar(w, d, f.cam.pos, pool);
  trimSummaries(f.cam.pos);
  for (auto& b : bands) b->active = false;
  if (d == DIM_NETHER) { setSkyFarRange(0); for (auto& b : bands) b->draw.clear(); drawn = 0; return; }   // a roofed land: the fog hides the far end
  setSkyFarRange(range);
  int maskR = std::min(40, R + 3);
  std::vector<int> list = {d};
  if (d == DIM_OVERWORLD && f.cloudBank > 0.01f) list.push_back(DIM_AETHER);
  if (d == DIM_AETHER && f.cloudBank > 0.01f) list.push_back(DIM_OVERWORLD);
  std::vector<std::tuple<float, int, Band::Want>> wants;
  for (size_t n = 0; n < list.size(); n++) {
    Band& b = *bands[list[n]];
    b.active = true;
    updateMask(b, f, maskR);
    select(b, f.cam.pos.x, f.cam.pos.z);
    for (auto& wv : b.want) wants.push_back({wv.pri + (n ? 1500.f : 0.f), b.dim, wv});
  }
  for (auto& b : bands) if (!b->active) b->draw.clear();
  // what to make next: one or two tiles at a time on the workers, nearest and coarsest first
  std::sort(wants.begin(), wants.end(), [](auto& a, auto& b) { return std::get<0>(a) < std::get<0>(b); });
  int maxJobs = std::max(1, std::min(6, pool.threads() - 1));
  for (auto& [pri, dim, wv] : wants) {
    if (inFlight >= maxJobs) break;
    Band& b = *bands[dim];
    auto it = b.tiles.find(tkey(wv.L, wv.tx, wv.tz));
    if (it != b.tiles.end() && (it->second->building || (it->second->ready && !it->second->dirty))) continue;
    submit(b, wv.L, wv.tx, wv.tz, pool, 300 + std::max(0.f, pri) / 64.f);
  }
  building = inFlight;
  // forget the ones not drawn for a while, past the memory it may use
  tilesHeld = 0;
  for (auto& b : bands) tilesHeld += (int)b->tiles.size();
  if (gpuBytes > maxBytes) {
    std::vector<std::tuple<u64, int, u64>> all;
    for (auto& b : bands) for (auto& [k, t] : b->tiles) if (!t->building && t->used + 2 < frame) all.push_back({t->used, b->dim, k});
    std::sort(all.begin(), all.end());
    for (auto& [u, dim, k] : all) {
      if (gpuBytes <= maxBytes * 0.85) break;
      auto& T = bands[dim]->tiles;
      auto it = T.find(k); if (it == T.end()) continue;
      gpuBytes -= std::min(gpuBytes, it->second->bytes);
      T.erase(it);
    }
  }
}

void DistantTerrain::draw(const FrameCtx& f, const SkyEnv& env, const TileTextures& tex) {
  if (f.portalDepth > 0 && (f.cam.realm != REALM_MAIN || f.dim < 0 || f.dim >= DIM_COUNT || !bands[f.dim]->active)) return;   // a portal's view: only into a land drawn here
  if (f.portalDepth == 0) drawn = 0;
  bool any = false;
  for (auto& b : bands) if (b->active && !b->draw.empty()) any = true;
  if (!any) return;
  if (!glReady) { glReady = true; shader.load("distant.glsl", "distant.glsl"); }
  if (!shader.ok()) return;
  shader.use();
  f.apply(shader);
  shader.set("uViewProj", f.cam.viewProj);
  shader.set("uCamFrac", vec3(f.cam.pos - f.cam.origin));
  shader.set("uDay", f.daylight * 0.92f);
  shader.set("uMinLight", f.minLight);
  shader.set("uDrop", 0.6f);
  // the weather over it (game/skies.js stormAt): the storm's shadow, and rain falling between you and it
  WeatherSystem* W = lvl ? weatherOf(*lvl) : nullptr;
  bool stormy = W && (W->front.on || W->trail.on || W->kind != "clear");
  vec4 stormU(0, 0, WeatherSystem::BAND, 0);
  if (stormy) {
    const WeatherFront& F = W->front; const WeatherFront& T = W->trail;
    bool th = (F.on ? F.kind : W->kind) == "thunder";
    shader.set("uFront", vec4(F.ux, F.uz, F.edge, F.on ? 1.f : 0.f));
    shader.set("uTrail", vec4(T.ux, T.uz, T.edge, T.on ? 1.f : 0.f));
    stormU = vec4(!T.on && W->kind != "clear" ? 1.f : 0.f, th ? 1.f : 0.7f, WeatherSystem::BAND, th ? 0.55f : 0.4f);
    shader.set("uAnchor", vec2((float)(W->anchor.x - f.cam.origin.x), (float)(W->anchor.y - f.cam.origin.z)));
    float lum = 0.25f + 0.75f * clampv((env.day - 0.2f) / 0.8f, 0.f, 1.f);
    shader.set("uRainCol", vec3(th ? 0.36f : 0.52f, th ? 0.38f : 0.55f, th ? 0.43f : 0.6f) * lum);
  }
  tex.bind(shader, 5);   // the blocks' own textures (units 5..10), drawn on the voxels like Cubyz's far chunks
  glActiveTexture(GL_TEXTURE4);
  shader.set("uMask", 4);
  GLint locO = shader.loc("uTileOrigin"), locS = shader.loc("uTileScale"), locC = shader.loc("uCell"), locW = shader.loc("uTileWorld");
  for (auto& bp : bands) {
    Band& b = *bp;
    if (!b.active || b.draw.empty() || b.dim != f.dim) continue;   // only the camera's own land
    glBindTexture(GL_TEXTURE_2D, b.maskTex);
    glm::dvec3 o = f.cam.origin;
    shader.set("uMaskO", vec3((float)(b.maskCx0 * 16.0 - o.x), (float)(b.maskCz0 * 16.0 - o.z), (float)(b.maskN * 16)));
    shader.set("uStorm", b.dim == DIM_OVERWORLD ? stormU : vec4(0, 0, WeatherSystem::BAND, 0));
    for (Tile* t : b.draw) {
      if (!t->quads) continue;
      int size = tileSize(t->L);
      double wx = (double)t->tx * size, wz = (double)t->tz * size;
      vec3 rel((float)(wx - o.x), (float)(-o.y), (float)(wz - o.z));
      if (!f.cam.frustum.box(vec3(rel.x, t->minY - 0.6f + rel.y, rel.z), vec3(rel.x + size, t->maxY + 1 + rel.y, rel.z + size))) continue;
      glUniform3fv(locO, 1, &rel[0]);
      glUniform1f(locS, size / 32768.f);
      glUniform1f(locC, (float)cellSize(t->L));
      glUniform2f(locW, (float)std::fmod(wx, 8192.0), (float)std::fmod(wz, 8192.0));
      shader.set("uFadeIn", t->fadeIn); shader.set("uFadeOut", t->fadeOut); shader.set("uTileSize", (float)size);
      t->mesh.draw();
      drawn++;
    }
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
}

} // namespace dq
