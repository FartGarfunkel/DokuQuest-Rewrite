// gen/land_biomes.cpp — see mc_biomes.h
#include "gen/land_biomes.h"
#include "common/noise.h"
#include <algorithm>
#include <cmath>

namespace dq {

namespace {
// the linear congruential step (Java long arithmetic: wrapping)
inline i64 lcg(i64 seed, i64 salt) {
  const u64 s = (u64)seed;
  return (i64)(s * (s * 6364136223846793005ull + 1442695040888963407ull) + (u64)salt);
}
inline i64 floorMod(i64 a, i64 b) { const i64 m = a % b; return m < 0 ? m + b : m; }

// the region layer's area context
struct Ctx {
  i64 seed, rval = 0;
  bool strong = false;
  void init(i64 x, i64 y) {
    i64 sx = x, sy = y;
    if (strong) {
      // posHashF so sequential quarts don't share low bits (hashFloat stripes on its third axis)
      sx = (i64)(posHashF((i32)x, (i32)y, 0xC11, (i32)seed) * 9007199254740992.0);
      sy = (i64)(posHashF((i32)y, (i32)x, 0xC12, (i32)seed) * 9007199254740992.0);
    }
    i64 i = seed; i = lcg(i, sx); i = lcg(i, sy); i = lcg(i, sx); rval = lcg(i, sy);
  }
  int next(int bound) { const int r = (int)floorMod(rval >> 24, bound); rval = lcg(rval, seed); return r; }
  int pick(int a, int b) { return next(2) == 0 ? a : b; }
  int pick(int a, int b, int c, int d) { const int i = next(4); return i == 0 ? a : i == 1 ? b : i == 2 ? c : d; }
};
i64 mixSeed(i64 world, i64 mod) {
  i64 i = lcg(mod, mod); i = lcg(i, mod); i = lcg(i, mod);
  i64 j = lcg(world, i); j = lcg(j, i); return lcg(j, i);
}
// ZoomLayer.modeOrRandom
int modeOrRandom(Ctx& c, int a, int b, int cc, int d) {
  if (b == cc && cc == d) return b;
  if (a == b && a == cc) return a;
  if (a == b && a == d) return a;
  if (a == cc && a == d) return a;
  if (a == b && cc != d) return a;
  if (a == cc && b != d) return a;
  if (a == d && b != cc) return a;
  if (b == cc && a != d) return b;
  if (b == d && a != cc) return b;
  if (cc == d && a != b) return cc;
  return c.pick(a, b, cc, d);
}
} // namespace

bool LandBiomes::load(const fs::path& dir, i64 worldSeed, bool withExtras, bool strongHash, bool withDappled) {
  strong_ = strongHash;
  auto j = readJson(dir / "biome_regions.json");
  if (!j || !j->contains("regions")) return false;
  worldSeed_ = worldSeed;
  regionSize_ = j->value("region_size", 3);
  for (auto& n : (*j)["biomes"]) {
    Info in; in.name = n.get<std::string>();
    const auto c = in.name.find(':');
    if (auto bj = readJson(dir / "biome" / in.name.substr(0, c) / (in.name.substr(c + 1) + ".json"))) {
      in.temperature = bj->value("temperature", 0.8f); in.downfall = bj->value("downfall", 0.4f);
      in.precipitation = bj->value("has_precipitation", true); in.frozen = bj->value("temperature_modifier", std::string()) == "frozen";
      if (bj->contains("effects")) {
        const json& e = (*bj)["effects"];
        in.grass = e.value("grass_color", -1); in.foliage = e.value("foliage_color", -1);
        in.water = e.value("water_color", 0x3F76E4); in.sky = e.value("sky_color", 0x78A7FF);
        in.grassModifier = e.value("grass_color_modifier", std::string());
        if (in.grassModifier == "none") in.grassModifier.clear();
      }
    }
    byName_[in.name] = (int)info_.size();
    info_.push_back(std::move(in));
  }
  for (auto& rj : (*j)["regions"]) {
    Region r; r.name = rj.value("name", ""); r.weight = rj.value("weight", 0);
    if (!withExtras && r.name.rfind("rare:", 0) == 0) continue;   // (terrain versions 15-16: without that extra region)
    for (auto& e : rj["entries"]) {
      Box b;
      for (int k = 0; k < 6; k++) { b.lo[k] = e[k * 2].get<i64>(); b.hi[k] = e[k * 2 + 1].get<i64>(); }
      b.lo[6] = b.hi[6] = e[12].get<i64>();
      b.biome = e[13].get<int>();
      r.boxes.push_back(b);
    }
    if (withDappled) {   // (terrain version 26: the forests' humid side, a good share of every wood, is the Dappled Forest)
      const int forest = find("main:forest"), dappled = find("dappled:dappled_forest");
      if (forest >= 0 && dappled >= 0) {
        const size_t n = r.boxes.size(); const i64 cut = 200;
        for (size_t i = 0; i < n; i++) {
          if (r.boxes[i].biome != forest || r.boxes[i].hi[1] <= cut) continue;
          Box d = r.boxes[i]; d.biome = dappled; d.lo[1] = std::max(d.lo[1], cut);
          if (r.boxes[i].lo[1] > cut) r.boxes[i].biome = dappled;
          else { r.boxes[i].hi[1] = cut - 1; r.boxes.push_back(d); }
        }
      }
    }
    build(r);
    regions_.push_back(std::move(r));
  }
  // the layers (LayeredNoiseUtil.uniqueness): initial (1), fuzzy zoom (2000), 3 zooms (2001..), region-size zooms (1001..)
  layerSeed_.push_back(mixSeed(worldSeed, 1));
  layerSeed_.push_back(mixSeed(worldSeed, 2000));
  for (int i = 0; i < 3; i++) layerSeed_.push_back(mixSeed(worldSeed, 2001 + i));
  for (int i = 0; i < regionSize_; i++) layerSeed_.push_back(mixSeed(worldSeed, 1001 + i));
  // (the biome zoom seed: a hash of the seed)
  u64 z = (u64)worldSeed + 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; zoomSeed_ = z ^ (z >> 31);
  DQ_LOG("land biomes: %d biomes, %d regions", (int)info_.size(), (int)regions_.size());
  return !regions_.empty();
}

// ---- the region layer ----
int LandBiomes::layer(int L, int x, int z) const {
  struct E { i64 k; int v; };
  thread_local std::vector<std::vector<E>> cache;
  thread_local const LandBiomes* owner = nullptr;
  if (owner != this || cache.size() != layerSeed_.size()) { cache.assign(layerSeed_.size(), std::vector<E>(1 << 12, E{INT64_MIN, 0})); owner = this; }
  const i64 key = ((i64)x << 32) ^ (u32)z;
  E& e = cache[L][((u64)key * 0x9E3779B97F4A7C15ull) >> 52];
  if (e.k == key) return e.v;
  Ctx c{layerSeed_[L]};
  c.strong = strong_;
  c.init(x, z);
  int v = 0;
  if (L == 0) {   // InitialLayer: a region by weight
    int total = 0; for (auto& r : regions_) total += r.weight;
    if (total > 0) {
      int i = c.next(total);
      for (int k = 0; k < (int)regions_.size(); k++) { i -= regions_[k].weight; if (i < 0) { v = k; break; } }
    }
  } else {   // ZoomLayer (the first: FUZZY)
    const int px = x >> 1, pz = z >> 1;
    const int a = layer(L - 1, px, pz);
    c.init(x >> 1 << 1, z >> 1 << 1);
    const int jx = x & 1, jz = z & 1;
    if (jx == 0 && jz == 0) v = a;
    else {
      const int l = layer(L - 1, px, pz + 1);
      const int i1 = c.pick(a, l);
      if (jx == 0 && jz == 1) v = i1;
      else {
        const int j1 = layer(L - 1, px + 1, pz);
        const int k1 = c.pick(a, j1);
        if (jx == 1 && jz == 0) v = k1;
        else {
          const int l1 = layer(L - 1, px + 1, pz + 1);
          v = L == 1 ? c.pick(a, j1, l, l1) : modeOrRandom(c, a, j1, l, l1);
        }
      }
    }
  }
  e = {key, v};
  return v;
}
int LandBiomes::region(int qx, int qz) const { return regions_.empty() ? 0 : layer((int)layerSeed_.size() - 1, qx, qz); }

// ---- the nearest box (the R-tree search's answer: the least sum of squared distances) ----
void LandBiomes::build(Region& r) {
  r.nodes.clear();
  struct Job { int node, first, count; };
  // (ties go to the box listed first: its place in the list rides along in the top bits of its biome)
  std::vector<std::pair<Box, int>> tagged; for (size_t i = 0; i < r.boxes.size(); i++) tagged.push_back({r.boxes[i], (int)i});
  r.nodes.push_back({}); std::vector<Job> jobs{{0, 0, (int)tagged.size()}};
  while (!jobs.empty()) {
    Job jb = jobs.back(); jobs.pop_back();
    Node n;
    for (int k = 0; k < 7; k++) { n.lo[k] = INT64_MAX; n.hi[k] = INT64_MIN; }
    for (int i = jb.first; i < jb.first + jb.count; i++) for (int k = 0; k < 7; k++) { n.lo[k] = std::min(n.lo[k], tagged[i].first.lo[k]); n.hi[k] = std::max(n.hi[k], tagged[i].first.hi[k]); }
    n.first = jb.first; n.count = jb.count;
    if (jb.count > 8) {
      int dim = 0; i64 best = -1;
      for (int k = 0; k < 7; k++) {
        i64 mn = INT64_MAX, mx = INT64_MIN;
        for (int i = jb.first; i < jb.first + jb.count; i++) { const i64 m = tagged[i].first.lo[k] + tagged[i].first.hi[k]; mn = std::min(mn, m); mx = std::max(mx, m); }
        if (mx - mn > best) { best = mx - mn; dim = k; }
      }
      std::stable_sort(tagged.begin() + jb.first, tagged.begin() + jb.first + jb.count, [&](auto& a, auto& b) { return a.first.lo[dim] + a.first.hi[dim] < b.first.lo[dim] + b.first.hi[dim]; });
      const int kids = std::min(8, (jb.count + 7) / 8);
      n.child = (int)r.nodes.size(); n.nchild = kids;
      for (int c = 0; c < kids; c++) r.nodes.push_back({});
      for (int c = 0; c < kids; c++) {
        const int a = jb.first + (int)((i64)jb.count * c / kids), b = jb.first + (int)((i64)jb.count * (c + 1) / kids);
        jobs.push_back({n.child + c, a, b - a});
      }
    }
    r.nodes[jb.node] = n;
  }
  r.boxes.clear();
  for (auto& t : tagged) { Box b = t.first; b.biome = (b.biome & 0xFFFF) | (t.second << 16); r.boxes.push_back(b); }
}

int LandBiomes::lookup(int region, const i64 p[6]) const {
  if (regions_.empty()) return 0;
  const Region& r = regions_[std::clamp(region, 0, (int)regions_.size() - 1)];
  i64 t[7]; for (int k = 0; k < 6; k++) t[k] = p[k]; t[6] = 0;
  auto dist = [&](const i64* lo, const i64* hi) {
    i64 s = 0;
    for (int k = 0; k < 7; k++) { const i64 d = t[k] > hi[k] ? t[k] - hi[k] : lo[k] > t[k] ? lo[k] - t[k] : 0; s += d * d; }
    return s;
  };
  i64 best = INT64_MAX; int bestBox = -1, bestOrder = INT32_MAX;
  int stack[256]; int sp = 0; stack[sp++] = 0;
  while (sp) {
    const Node& n = r.nodes[stack[--sp]];
    if (dist(n.lo, n.hi) > best) continue;
    if (n.child < 0) {
      for (int i = n.first; i < n.first + n.count; i++) {
        const Box& b = r.boxes[i];
        const i64 d = dist(b.lo, b.hi); const int ord = b.biome >> 16;
        if (d < best || (d == best && ord < bestOrder)) { best = d; bestBox = i; bestOrder = ord; }
      }
    } else for (int c = n.nchild - 1; c >= 0 && sp < 255; c--) stack[sp++] = n.child + c;
  }
  return bestBox < 0 ? 0 : r.boxes[bestBox].biome & 0xFFFF;
}

// ---- the biome zoom: which quart a block reads ----------
void LandBiomes::quartFiddle(int cx, int cy, int cz, double& a, double& b, double& c) const {
  if (strong_) {
    // posHashF mixes all four args (hashFloat stripes on its third); scale 1.2: a bit more than Minecraft's 0.9
    const double scale = 1.2;
    const i32 s0 = (i32)zoomSeed_, s1 = (i32)((zoomSeed_ >> 32) ^ 0x9E3779B9ull);
    a = (posHashF(cx, cy, cz, s0) - 0.5) * scale;
    b = (posHashF(cy, cz, cx, s1) - 0.5) * scale;
    c = (posHashF(cz, cx, cy, s0 ^ s1) - 0.5) * scale;
  } else {
    auto fiddle = [](i64 s) { return ((double)floorMod(s >> 24, 1024) / 1024.0 - 0.5) * 0.9; };
    const i64 S = (i64)zoomSeed_;
    i64 s = lcg(S, cx); s = lcg(s, cy); s = lcg(s, cz); s = lcg(s, cx); s = lcg(s, cy); s = lcg(s, cz);
    a = fiddle(s); s = lcg(s, S);
    b = fiddle(s); s = lcg(s, S);
    c = fiddle(s);
  }
}

void LandBiomes::fuzzyQuart(int x, int y, int z, int& qx, int& qy, int& qz) const {
  if (strong_) {
    // a short warp (the second zoom): neighbouring quarts mix, thin climate slabs don't stay as ruler bands
    auto vn = [&](double fx, double fz, i32 salt) {
      const int ix = (int)std::floor(fx), iz = (int)std::floor(fz); const double ux = fx - ix, uz = fz - iz;
      const double sx = ux * ux * (3 - 2 * ux), sz = uz * uz * (3 - 2 * uz);
      auto h = [&](int a, int c) { return posHashF(a, salt, c, (i32)zoomSeed_) * 2.0 - 1.0; };
      return (h(ix, iz) * (1 - sx) + h(ix + 1, iz) * sx) * (1 - sz) + (h(ix, iz + 1) * (1 - sx) + h(ix + 1, iz + 1) * sx) * sz;
    };
    const int x0 = x, y0 = y, z0 = z;
    x = x0 + (int)std::floor(vn(x0 / 24.0, z0 / 24.0, 0xA11) * 10.0);
    y = y0 + (int)std::floor(vn(x0 / 24.0, z0 / 24.0, 0xA15) * 2.0);
    z = z0 + (int)std::floor(vn(x0 / 24.0, z0 / 24.0, 0xA12) * 10.0);
  }
  const int i = x - 2, j = y - 2, k = z - 2;
  const int l = i >> 2, m = j >> 2, n = k >> 2;
  const double d0 = (i & 3) / 4.0, d1 = (j & 3) / 4.0, d2 = (k & 3) / 4.0;
  // each corner's jitter (its own, whichever block asks): kept per thread
  struct F { i64 k = INT64_MIN; double a, b, c; };
  thread_local std::vector<F> cache(1 << 14); thread_local u64 owner = 0;
  const u64 own = zoomSeed_ ^ (strong_ ? 0xA5A5A5A5A5A5A5A5ull : 0);
  if (owner != own) { std::fill(cache.begin(), cache.end(), F{}); owner = own; }
  auto fid = [&](int cx, int cy, int cz) -> const F& {
    const i64 key = ((i64)cx << 40) ^ ((i64)(cy & 0xFFFF) << 24) ^ (i64)(u32)(cz & 0xFFFFFF);
    F& f = cache[((u64)key * 0x9E3779B97F4A7C15ull) >> 50];
    if (f.k == key) return f;
    quartFiddle(cx, cy, cz, f.a, f.b, f.c); f.k = key;
    return f;
  };
  auto pick8 = [&](int l0, int m0, int n0, double e0, double e1, double e2, int& ox, int& oy, int& oz) {
    int pick = 0; double best = 1e300;
    for (int c = 0; c < 8; c++) {
      const bool fx = !(c & 4), fy = !(c & 2), fz = !(c & 1);
      const F& f = fid(fx ? l0 : l0 + 1, fy ? m0 : m0 + 1, fz ? n0 : n0 + 1);
      const double ex = (fx ? e0 : e0 - 1) + f.a, ey = (fy ? e1 : e1 - 1) + f.b, ez = (fz ? e2 : e2 - 1) + f.c;
      const double d = ez * ez + ey * ey + ex * ex;
      if (best > d) { pick = c; best = d; }
    }
    ox = (pick & 4) == 0 ? l0 : l0 + 1; oy = (pick & 2) == 0 ? m0 : m0 + 1; oz = (pick & 1) == 0 ? n0 : n0 + 1;
  };
  pick8(l, m, n, d0, d1, d2, qx, qy, qz);
  if (strong_) {
    // second zoom at 8-block: nudge the quart at most one toward the coarse cell (neighbouring quarts mix)
    const int i8 = x - 4, j8 = y - 4, k8 = z - 4;
    int cx, cy, cz;
    pick8(i8 >> 3, j8 >> 3, k8 >> 3, (i8 & 7) / 8.0, (j8 & 7) / 8.0, (k8 & 7) / 8.0, cx, cy, cz);
    const int nx = cx * 2 + (qx & 1), ny = cy * 2 + (qy & 1), nz = cz * 2 + (qz & 1);
    if (qx < nx) qx++; else if (qx > nx) qx--;
    if (qy < ny) qy++; else if (qy > ny) qy--;
    if (qz < nz) qz++; else if (qz > nz) qz--;
  }
}

bool LandBiomes::coldEnoughToSnow(int b, int x, int y, int z) const {
  if (b < 0 || b >= (int)info_.size()) return false;
  const Info& in = info_[b];
  float t = in.temperature;
  // (the reference frozen modifier and height chill use their own simplex noises; a hash-smooth stand-in here)
  auto vn = [&](double fx, double fz, u64 salt) {
    const int ix = (int)std::floor(fx), iz = (int)std::floor(fz); const double ux = fx - ix, uz = fz - iz;
    auto h = [&](int a, int c) { u64 k = ((u64)(u32)a << 32 | (u32)c) ^ (zoomSeed_ + salt); k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33;
      if (strong_) { k *= 0xc4ceb9fe1a85ec53ull; k ^= k >> 33; k = (k ^ (k >> 29)) * 0xBF58476D1CE4E5B9ull; k ^= k >> 32; }   // (18: the whole of murmur's mix and more: no stripes)
      return (double)(k >> 11) / (double)(1ull << 53) * 2 - 1; };
    const double sx = ux * ux * (3 - 2 * ux), sz = uz * uz * (3 - 2 * uz);
    return (h(ix, iz) * (1 - sx) + h(ix + 1, iz) * sx) * (1 - sz) + (h(ix, iz + 1) * (1 - sx) + h(ix + 1, iz + 1) * sx) * sz;
  };
  if (in.frozen) {
    const double f = vn(x * 0.05, z * 0.05, 1) * 7.0 + vn(x * 0.2, z * 0.2, 2);
    if (f < 0.3 && vn(x * 0.09, z * 0.09, 3) < 0.8) t = 0.2f;
  }
  if (y > 80) t -= ((float)(vn(x / 8.0, z / 8.0, 4) * 8.0) + (float)y - 80.0f) * 0.05f / 40.0f;
  return t < 0.15f;
}

} // namespace dq
