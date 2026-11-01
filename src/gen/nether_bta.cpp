// gen/nether_bta.cpp — see nether_bta.h. NetherShape (the ground, the roof and the lands) and DepthsGen's terrain
// version 5 generation (DepthsGen::generateBTA and its queries).
#include "gen/nether_bta.h"
#include "gen/dimgens.h"
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace dq {
using namespace vmap;

const char* netherBiomeName(int b) {
  static const char* N[NB_COUNT] = {"The Crag", "Volcanic Islands", "Sulfur Pools", "Crystal Forest", "The Old World"};
  return b >= 0 && b < NB_COUNT ? N[b] : "The Nether";
}

namespace {
inline double sat(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
inline double smooth(double t) { t = sat(t); return t * t * (3 - 2 * t); }
inline double h01(u32 h, int shift) { return ((h >> shift) & 0xFFFF) / 65535.0; }
int pickBiome(double r) { return r < 0.24 ? NB_CRAG : r < 0.44 ? NB_VOLCANIC : r < 0.62 ? NB_SULFUR : r < 0.80 ? NB_CRYSTAL : NB_OLDWORLD; }
constexpr double PI_D = 3.14159265358979323846;
} // namespace

NetherShape::NetherShape(i32 seed)
  : seed_(seed), nWarp((u32)seed ^ 0x51a7u), nA((u32)seed ^ 0x1b2cu), nB((u32)seed ^ 0x2c3du), nC((u32)seed ^ 0x3d4eu),
    nRoof((u32)seed ^ 0x4e5fu), nSt((u32)seed ^ 0x5f60u), nPil((u32)seed ^ 0x6071u) {}

void NetherShape::biomeMix(int x, int z, u8& a, u8& b, float& mix) const {
  const double G = 230;
  const double wx = x + 40 * nWarp.noise2(x / 120.0, z / 120.0), wz = z + 40 * nWarp.noise2(x / 120.0 + 71.3, z / 120.0 - 18.9);
  const int gx = (int)std::floor(wx / G), gz = (int)std::floor(wz / G);
  double d1 = 1e18, d2 = 1e18; int b1 = 0, b2 = 0;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    const int cx = gx + dx, cz = gz + dz;
    const u32 h = hash32(cx, cz, 0x4e7b, seed_);
    const double px = (cx + 0.15 + 0.7 * h01(h, 0)) * G, pz = (cz + 0.15 + 0.7 * h01(h, 16)) * G;
    const double d = std::hypot(wx - px, wz - pz);
    const int bi = pickBiome(hashFloat(cx, cz, 0x51, seed_));
    if (d < d1) { d2 = d1; b2 = b1; d1 = d; b1 = bi; } else if (d < d2) { d2 = d; b2 = bi; }
  }
  a = (u8)b1; b = (u8)b2;
  mix = b1 == b2 ? 1.f : (float)(0.5 + 0.5 * smooth((d2 - d1) / 56.0));
}

double NetherShape::sulfurBase(int x, int z) const { return 57 + 3.5 * nA.noise2(x / 56.0 - 20, z / 56.0) + 1.5 * nB.noise2(x / 15.0, z / 15.0); }

double NetherShape::floorOf(int b, int x, int z, NetherCol& c) const {
  switch (b) {
    case NB_CRAG: {
      double h = 64 + 12 * nA.noise2(x / 80.0, z / 80.0) + 5 * nB.noise2(x / 24.0, z / 24.0);
      const double r = 1 - std::fabs(nC.noise2(x / 46.0, z / 46.0));
      h += 16 * r * r * r;                                             // crags: sharp ridges
      const double sp = nB.noise2(x / 11.0 + 300, z / 11.0 - 200);
      if (sp > 0.62) h += (sp - 0.62) * 70;                            // rock spires
      return h;
    }
    case NB_VOLCANIC: {
      double h = 43 + 9 * nA.noise2(x / 64.0 + 40, z / 64.0) + 4 * nB.noise2(x / 18.0, z / 18.0);   // mostly under the lava sea
      const double G = 84;
      const int gx = (int)std::floor(x / G), gz = (int)std::floor(z / G);
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        const int cx = gx + dx, cz = gz + dz;
        const u32 hh = hash32(cx, cz, 0x7010, seed_);
        if ((hh & 255) > 150) continue;
        const u32 h2 = hash32(cx, cz, 0x7011, seed_);
        const double px = (cx + 0.2 + 0.6 * h01(hh, 8)) * G, pz = (cz + 0.2 + 0.6 * h01(h2, 0)) * G;
        const double R = 11 + 9 * h01(h2, 16), H = 26 + 26 * h01(hh, 16);
        const double d = std::hypot(x + 0.5 - px, z + 0.5 - pz);
        if (d >= R) continue;
        const double cr = R * 0.24;
        const double rim = LAVA - 2 + (H + 2) * std::pow(1 - cr / R, 1.3);
        const int lvl = (int)std::floor(rim) - 1;   // the crater's lava, just under its rim
        double cone = LAVA - 2 + (H + 2) * std::pow(1 - d / R, 1.3);
        if (d < cr) { cone = rim - 3 - (cr - d) * 0.9; c.poolTop = std::max(c.poolTop, lvl); c.pool = 1; }
        // the breach: a notch in the rim on one side, where the lava spills out and runs down the slope
        const double want = h01(h2, 8) * 2 * PI_D, ang = std::atan2(z + 0.5 - pz, x + 0.5 - px);
        double diff = std::fabs(std::fmod(ang - want + 3 * PI_D, 2 * PI_D) - PI_D);
        if (diff < 0.2 && d >= cr - 0.5 && d < cr + 2.5) { cone = std::min(cone, (double)lvl - 1); c.poolTop = std::max(c.poolTop, lvl); c.pool = 1; }
        if (cone > h) { h = cone; c.cone = true; }
      }
      return h;
    }
    case NB_SULFUR: {
      double h = sulfurBase(x, z);
      // pools of acid, each at its own level, ringed by a raised rim of crust
      const double G = 34;
      const int gx = (int)std::floor(x / G), gz = (int)std::floor(z / G);
      double best = 1e9; int S = 0; double poolFloor = 0; bool rim = false; int rimS = 0;
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        const int cx = gx + dx, cz = gz + dz;
        const u32 hh = hash32(cx, cz, 0x5011, seed_);
        if ((hh & 255) > 155) continue;
        const double px = (cx + 0.2 + 0.6 * h01(hh, 8)) * G, pz = (cz + 0.2 + 0.6 * h01(hh, 16)) * G;
        const double r = 4 + 7 * ((hh >> 24) / 255.0);
        const double d = std::hypot(x + 0.5 - px, z + 0.5 - pz);
        if (d < r) {
          if (d / r < best) { best = d / r; S = (int)std::lround(sulfurBase((int)px, (int)pz)); poolFloor = S - (1 + 3.5 * std::pow(1 - d / r, 0.6)); }
        } else if (d < r + 2.5) { rim = true; rimS = std::max(rimS, (int)std::lround(sulfurBase((int)px, (int)pz))); }
      }
      if (best < 1e8) { h = poolFloor; c.poolTop = S; c.pool = 2; }
      else if (rim) h = std::max(h, (double)rimS + 1);
      return h;
    }
    case NB_CRYSTAL:
      return 63 + 7 * nA.noise2(x / 60.0 + 90, z / 60.0) + 3 * nB.noise2(x / 17.0, z / 17.0);
    default: {   // the Old World: long dunes of ash, and here and there a pit down to the lava, half hidden
      double h = 72 + 8 * nA.noise2(x / 110.0 - 60, z / 110.0 + 10) + 5 * nC.noise2(x / 34.0, z / 52.0) + 1.5 * nB.noise2(x / 9.0, z / 9.0);
      const double pit = nPil.noise2(x / 13.0 + 900, z / 13.0 - 400);
      if (pit > 0.7) h = std::min(h, LAVA - 3 + (h - LAVA) * smooth((0.82 - pit) / 0.12));
      return h;
    }
  }
}

double NetherShape::roofOf(int b, int x, int z) const {
  static const double ADJ[NB_COUNT] = {-4, 12, -8, 0, 16}, TH[NB_COUNT] = {0.3, 0.5, 0.45, 0.4, 0.55};
  double r = 192 + 14 * nRoof.noise2(x / 90.0, z / 90.0) + ADJ[b];
  const double s = nSt.noise2(x / 8.0, z / 8.0);
  if (s > TH[b]) r -= (s - TH[b]) * 70;                                // stalactites
  const double s2 = nSt.noise2(x / 23.0 + 500, z / 23.0 - 300);
  if (s2 > 0.62) r -= (s2 - 0.62) * 110;                               // great hanging masses of rock
  return r;
}

NetherCol NetherShape::column(int x, int z) const {
  NetherCol c, cb;
  u8 a, b; float mix;
  biomeMix(x, z, a, b, mix);
  c.biome = a; c.other = b; c.mix = mix;
  double fl = floorOf(a, x, z, c), rf = roofOf(a, x, z);
  if (b != a && mix < 0.999f) {
    fl = mix * fl + (1 - mix) * floorOf(b, x, z, cb);
    rf = mix * rf + (1 - mix) * roofOf(b, x, z);
  }
  if (mix < 0.93f) { c.pool = 0; c.poolTop = -1; c.cone = false; }   // craters and pools only well inside their own land
  if (a == NB_CRAG && mix > 0.8f && nPil.noise2(x / 26.0, z / 26.0) > 0.74) c.pillar = true;   // pillars of rock to the roof
  c.floor = std::clamp((int)std::floor(fl), 8, 200);
  c.roof = std::clamp((int)std::floor(rf), c.floor + 14, DIM_H - 6);
  if (c.pool && c.poolTop <= c.floor) { c.pool = 0; c.poolTop = -1; }
  return c;
}

void netherBiomeMix(i32 seed, int x, int z, u8& a, u8& b, float& mix) {
  static std::mutex mu; static std::map<i32, std::shared_ptr<NetherShape>> shapes;
  std::shared_ptr<NetherShape> s;
  { std::lock_guard<std::mutex> l(mu); auto& p = shapes[seed]; if (!p) p = std::make_shared<NetherShape>(seed); s = p; }
  s->biomeMix(x, z, a, b, mix);
}

void netherVolcanoes(i32 seed, int x, int z, int range, std::vector<NetherVolcano>& out) {
  out.clear();
  const double G = 84;   // as floorOf's NB_VOLCANIC
  const int gx0 = (int)std::floor((x - range) / G), gx1 = (int)std::floor((x + range) / G);
  const int gz0 = (int)std::floor((z - range) / G), gz1 = (int)std::floor((z + range) / G);
  for (int cz = gz0; cz <= gz1; cz++) for (int cx = gx0; cx <= gx1; cx++) {
    const u32 hh = hash32(cx, cz, 0x7010, seed);
    if ((hh & 255) > 150) continue;
    const u32 h2 = hash32(cx, cz, 0x7011, seed);
    const double px = (cx + 0.2 + 0.6 * h01(hh, 8)) * G, pz = (cz + 0.2 + 0.6 * h01(h2, 0)) * G;
    if (std::hypot(px - x, pz - z) > range) continue;
    u8 a, b; float mix;
    netherBiomeMix(seed, (int)std::floor(px), (int)std::floor(pz), a, b, mix);
    if (a != NB_VOLCANIC || mix < 0.93f) continue;   // craters only well inside their own land (column())
    const double R = 11 + 9 * h01(h2, 16), H = 26 + 26 * h01(hh, 16), cr = R * 0.24;
    const double rim = NetherShape::LAVA - 2 + (H + 2) * std::pow(1 - cr / R, 1.3);
    NetherVolcano v; v.x = (int)std::floor(px); v.z = (int)std::floor(pz); v.lavaY = (int)std::floor(rim) - 1;
    v.radius = (float)R; v.crater = (float)cr; v.height = (float)H; v.hash = hh ^ (h2 << 7);
    out.push_back(v);
  }
}

// =================================================================================================== generation
namespace {
// one land's blocks
struct Mats { BlockId top, sub, deep, roof; };
} // namespace

void DepthsGen::generateBTA(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * CS, bz = ch.cz * CS, LV = LAVA_Y;
  const i32 s5 = seed_ ^ 5;
  thread_local std::vector<NetherCol> cols; cols.resize(256);
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) cols[z * 16 + x] = bta_->column(bx + x, bz + z);
  auto landOf = [&](const NetherCol& c, int wx, int wz) { return c.biome != c.other && hashFloat(wx, 17, wz, seed_) > c.mix ? c.other : c.biome; };   // dithered at the borders

  // 1. the ground, the roof, the lava sea, the craters' lava and the pools' acid
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const NetherCol& c = cols[z * 16 + x];
    const int land = landOf(c, wx, wz);
    const double patch = nFloor.noise3(wx / 14.0, 7.5, wz / 14.0);
    const double r = hashFloat(wx, 3, wz, seed_ ^ 0x77);
    const double strata = nHill.noise3(wx / 60.0, 3.0, wz / 60.0);
    Mats M{B.netherrack, B.netherrack, strata > 0.3 ? B.emberstone : B.netherrack, B.netherrack};
    switch (land) {
      case NB_CRAG:
        if (c.floor <= LV + 3) M.top = patch > 0 ? B.soul_sand : B.gravel;
        else if (patch > 0.55) M.top = B.soul_sand;
        if (c.floor <= LV + 4 && r < 0.2) M.top = B.magma_block;           // melting rock round the lava
        break;
      case NB_VOLCANIC:
        M.sub = B.basalt; M.deep = B.blackstone; M.roof = B.basalt;
        if (c.floor <= LV + 2) M.top = B.cinder_sand;
        else if (c.cone) M.top = c.pool ? B.obsidian : r < 0.5 ? B.basalt : r < 0.88 ? B.blackstone : r < 0.95 ? B.magma_block : B.sulphur_block;
        else M.top = patch > 0.5 ? B.sulphur_block : r < 0.6 ? B.blackstone : B.basalt;
        if (c.pool) M.sub = B.obsidian;
        break;
      case NB_SULFUR:
        M.top = c.pool ? B.sulphur_block : patch > -0.3 ? B.sulphur_block : r < 0.5 ? B.netherrack : B.basalt;
        M.sub = c.pool || patch > 0.2 ? B.sulphur_block : B.netherrack;
        break;
      case NB_CRYSTAL:
        M.top = patch > 0.45 ? B.soul_soil : B.blackstone; M.sub = B.blackstone; M.roof = B.blackstone;
        break;
      default:   // the Old World
        M.top = r < 0.025 && B.smouldering_ash ? B.smouldering_ash : B.ash; M.sub = B.ash; M.deep = B.soul_schist ? B.soul_schist : B.blackstone;
        M.roof = r < 0.5 ? B.basalt : B.blackstone;
        break;
    }
    for (int y = 0; y < DIM_H; y++) {
      BlockId id = AIR;
      if (y == 0 || y == DIM_H - 1 || (y < 5 && hashFloat(wx, y, wz, seed_) < 0.75 - y * 0.15) ||
          (y > DIM_H - 6 && hashFloat(wx, y, wz, s5) < 0.75 - (DIM_H - 1 - y) * 0.15)) id = B.bedrock;
      else if (y <= c.floor) {
        const int dep = c.floor - y;
        id = dep == 0 ? M.top : dep < (land == NB_OLDWORLD ? 4 : 3) ? M.sub : M.deep;
        if (dep > 2 && land == NB_OLDWORLD && dep > 6) id = strata > 0.1 ? B.blackstone : M.deep;
      } else if (c.pillar) id = y >= c.roof - 1 ? M.roof : land == NB_CRAG && r < 0.3 ? B.emberstone : B.netherrack;
      else if (y >= c.roof) id = M.roof;
      else if (c.pool && y <= c.poolTop) id = c.pool == 1 ? B.lava : (B.acid ? B.acid : B.lava);
      else if (y <= LV) id = B.lava;
      b.blocks[colIdx(x, y, z)] = id;
      b.meta[colIdx(x, y, z)] = 0;
    }
  }
  // 2. tunnels low in the rock (they stay under the walking ground's top layers mostly, and out of the roof)
  carveCaves(b, ch.cx, ch.cz, btaCarve_);

  // 3. what stands on the ground
  auto air = [&](int x, int y, int z) { return y > 0 && y < DIM_H && b.blocks[colIdx(x, y, z)] == AIR; };
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const NetherCol& c = cols[z * 16 + x];
    const int y = c.floor;
    if (c.pillar || !air(x, y + 1, z)) continue;
    const BlockId top = b.blocks[colIdx(x, y, z)];
    const double r = hashFloat(wx, y, wz, seed_ ^ 0x99);
    switch (landOf(c, wx, wz)) {
      case NB_CRAG:
        if (top == B.netherrack && r < 0.006 && B.fire) b.blocks[colIdx(x, y + 1, z)] = B.fire;
        else if (top == B.netherrack && r < 0.02) b.blocks[colIdx(x, y, z)] = B.magma_block;   // melting rock
        break;
      case NB_VOLCANIC:
        if (c.cone && !c.pool && r < 0.012 && B.fire) b.blocks[colIdx(x, y + 1, z)] = B.fire;
        break;
      case NB_SULFUR:
        if (top == B.sulphur_block && r < 0.012 && B.steam_vent) b.blocks[colIdx(x, y, z)] = B.steam_vent;   // vents that breathe hot steam
        else if (top == B.sulphur_block && r > 0.995) for (int k = 1; k <= 2 + (int)(r * 1000) % 3 && air(x, y + k, z); k++) b.blocks[colIdx(x, y + k, z)] = B.sulphur_block;
        break;
      case NB_CRYSTAL:
        if (r < 0.04 && B.red_crystal_cluster) b.blocks[colIdx(x, y + 1, z)] = B.red_crystal_cluster;
        break;
      default:
        if (r > 0.9975 && B.pumice) for (int k = 1; k <= 2 && air(x, y + k, z); k++) b.blocks[colIdx(x, y + k, z)] = B.pumice;
        else if (r > 0.9955 && B.ashen_trunk) { const int th = 3 + (int)(r * 10000) % 4; for (int k = 1; k <= th && air(x, y + k, z); k++) b.blocks[colIdx(x, y + k, z)] = B.ashen_trunk; }
        break;
    }
  }
  // 4. the lands' great features, from cells round this chunk (each one whole on every chunk it reaches)
  auto setIn = [&](int wx, int y, int wz, BlockId id, bool overAny = false) {
    const int x = wx - bx, z = wz - bz;
    if (x < 0 || x > 15 || z < 0 || z > 15 || y < 1 || y >= DIM_H - 1) return;
    BlockId& cur = b.blocks[colIdx(x, y, z)];
    if (overAny ? cur != B.bedrock : (cur == AIR || cur == B.fire || cur == B.red_crystal_cluster)) cur = id;
  };
  // crystal spires (and crystals hanging from the roof): a dense maze of them
  if (B.red_crystal) {
    const int G = 7, M = 12;
    for (int cz = (bz - M) / G - 1; cz <= (bz + 16 + M) / G + 1; cz++) for (int cx = (bx - M) / G - 1; cx <= (bx + 16 + M) / G + 1; cx++) {
      const u32 h = hash32(cx, cz, 0xC4A5, seed_), h2 = hash32(cx, cz, 0xC4A6, seed_);
      const bool up = (h & 255) < 140, down = ((h >> 8) & 255) < 70;
      if (!up && !down) continue;
      const int px = cx * G + (int)(h01(h2, 0) * (G - 1)), pz = cz * G + (int)(h01(h2, 16) * (G - 1));
      if (px < bx - M || px > bx + 15 + M || pz < bz - M || pz > bz + 15 + M) continue;
      const NetherCol c = bta_->column(px, pz);
      if (c.biome != NB_CRYSTAL || c.mix < 0.8f || c.pillar) continue;
      const double f1 = h01(h, 16), f2 = (h >> 24) / 255.0, f3 = h01(h2, 8);
      const bool charged = f3 < 0.1 && B.charged_crystal;
      const BlockId mat = charged ? B.charged_crystal : B.red_crystal;
      const double lx = (h01(h2, 4) - 0.5) * 0.55, lz = (h01(h2, 12) - 0.5) * 0.55;
      if (up) {
        double hgt = 7 + 22 * std::pow(f1, 1.5); if (f2 < 0.08) hgt = 34 + 22 * f2 / 0.08;   // now and then a giant
        const double r0 = 1.3 + 1.8 * f2 + (hgt > 30 ? 1.2 : 0);
        for (int t = -2; t < (int)hgt; t++) {
          const double k = std::max(0.0, (double)t) / hgt, rr = r0 * std::pow(1 - k, 0.75) + 0.35;
          const double ax = px + 0.5 + lx * std::max(0, t), az = pz + 0.5 + lz * std::max(0, t);
          for (int wz = (int)std::floor(az - rr); wz <= (int)std::floor(az + rr); wz++) for (int wx = (int)std::floor(ax - rr); wx <= (int)std::floor(ax + rr); wx++)
            if (std::hypot(wx + 0.5 - ax, wz + 0.5 - az) <= rr) setIn(wx, c.floor + 1 + t, wz, mat, t < 0);
        }
      }
      if (down) {
        const double len = 5 + 14 * f1, r0 = 1.0 + 1.4 * f2;
        for (int t = -2; t < (int)len; t++) {
          const double k = std::max(0.0, (double)t) / len, rr = r0 * std::pow(1 - k, 0.8) + 0.3;
          const double ax = px + 0.5 - lx * std::max(0, t) * 0.6, az = pz + 0.5 - lz * std::max(0, t) * 0.6;
          for (int wz = (int)std::floor(az - rr); wz <= (int)std::floor(az + rr); wz++) for (int wx = (int)std::floor(ax - rr); wx <= (int)std::floor(ax + rr); wx++)
            if (std::hypot(wx + 0.5 - ax, wz + 0.5 - az) <= rr) setIn(wx, c.roof - 1 - t, wz, mat, t < 0);
        }
      }
    }
  }
  // the Old World's ruins: broken pillars, arches and walls of scorched brick, half sunk in the ash
  if (B.scorched_bricks) {
    const int G = 40, M = 12;
    for (int cz = (bz - M) / G - 1; cz <= (bz + 16 + M) / G + 1; cz++) for (int cx = (bx - M) / G - 1; cx <= (bx + 16 + M) / G + 1; cx++) {
      const u32 h = hash32(cx, cz, 0x0D1D, seed_), h2 = hash32(cx, cz, 0x0D1E, seed_);
      if ((h & 255) > 150) continue;
      const int px = cx * G + 6 + (int)(h01(h2, 0) * (G - 12)), pz = cz * G + 6 + (int)(h01(h2, 16) * (G - 12));
      if (px < bx - M - 6 || px > bx + 15 + M + 6 || pz < bz - M - 6 || pz > bz + 15 + M + 6) continue;
      const NetherCol c = bta_->column(px, pz);
      if (c.biome != NB_OLDWORLD || c.mix < 0.85f || c.floor <= LV + 2) continue;
      const int base = c.floor - 2, kind = (int)((h >> 8) % 3);
      auto brick = [&](int wx, int y, int wz) { const double q = hashFloat(wx, y, wz, seed_ ^ 0xB1); setIn(wx, y, wz, q < 0.3 ? B.cracked_scorched_bricks : B.scorched_bricks, true); };
      auto pillar = [&](int x0, int z0, int w, int hgt) {
        for (int y = base; y < base + hgt; y++) for (int dz = 0; dz < w; dz++) for (int dx = 0; dx < w; dx++) {
          if (y >= base + hgt - 2 && hashFloat(x0 + dx, y, z0 + dz, seed_ ^ 0xB2) < 0.45) continue;   // a broken top
          brick(x0 + dx, y, z0 + dz);
        }
      };
      const int hgt = 6 + (int)(((h >> 16) & 255) / 255.0 * 10);
      const bool alongX = (h >> 24) & 1;
      if (kind == 0) {   // a few pillars, standing or broken off
        for (int k = 0; k < 4; k++) {
          const u32 hk = hash32(cx, cz, 0x0D20 + k, seed_);
          pillar(px + (int)(hk % 13) - 6, pz + (int)((hk >> 8) % 13) - 6, 2 + (int)((hk >> 16) & 1), 3 + (int)((hk >> 20) % (unsigned)hgt));
        }
      } else if (kind == 1) {   // an arch: two pillars and the span across their tops
        const int gap = 5 + (int)((h >> 20) & 3), ah = 7 + (int)((h >> 12) & 3);
        const int x2 = alongX ? px + gap + 2 : px, z2 = alongX ? pz : pz + gap + 2;
        pillar(px, pz, 2, ah + 2); pillar(x2, z2, 2, ah + 2);
        const bool broken = ((h >> 4) & 3) == 0;
        for (int k = 0; k < gap + 4; k++) for (int t = 0; t < 2; t++) for (int w = 0; w < 2; w++) {
          if (broken && k > gap / 2 && k < gap / 2 + 3) continue;
          const int wx = alongX ? px + k : px + w, wz = alongX ? pz + w : pz + k;
          brick(wx, base + ah + t, wz);
        }
      } else {   // a long wall, gapped and crumbling
        const int len = 9 + (int)((h >> 12) % 9);
        for (int k = 0; k < len; k++) {
          const int wx = alongX ? px + k : px, wz = alongX ? pz : pz + k;
          const int wh = 2 + (int)(hashFloat(wx, 0, wz, seed_ ^ 0xB3) * 4);
          if (hashFloat(wx, 1, wz, seed_ ^ 0xB4) < 0.15) continue;
          for (int y = base; y < base + 2 + wh; y++) brick(wx, y, wz);
        }
      }
    }
  }
  // the Crag: bleached bones (a spine and its ribs, half buried) and lavafalls from the roof
  if (B.bone_block) {
    const int G = 56, M = 10;
    for (int cz = (bz - M) / G - 1; cz <= (bz + 16 + M) / G + 1; cz++) for (int cx = (bx - M) / G - 1; cx <= (bx + 16 + M) / G + 1; cx++) {
      const u32 h = hash32(cx, cz, 0xB0E5, seed_), h2 = hash32(cx, cz, 0xB0E6, seed_);
      if ((h & 255) > 100) continue;
      const int px = cx * G + 8 + (int)(h01(h2, 0) * (G - 16)), pz = cz * G + 8 + (int)(h01(h2, 16) * (G - 16));
      if (px < bx - M - 12 || px > bx + 15 + M + 12 || pz < bz - M - 12 || pz > bz + 15 + M + 12) continue;
      const NetherCol c = bta_->column(px, pz);
      if (c.biome != NB_CRAG || c.mix < 0.85f || c.floor <= LV + 1) continue;
      const bool alongX = (h >> 8) & 1;
      const int len = 7 + (int)((h >> 9) % 6), y0 = c.floor;
      for (int k = 0; k < len; k++) {
        const int sx = alongX ? px + k : px, sz = alongX ? pz : pz + k;
        setIn(sx, y0, sz, B.bone_block, true);
        if (k % 2 == 1 && k < len - 1) for (int a = 0; a <= 8; a++) {   // a rib: half a ring across the spine
          const double ang = PI_D * a / 8, rr = 3.2 - (k > len - 4 ? 1 : 0);
          const int off = (int)std::lround(std::cos(ang) * rr), dy = (int)std::lround(std::sin(ang) * rr);
          setIn(alongX ? sx : sx + off, y0 + dy, alongX ? sz + off : sz, B.bone_block, false);
        }
      }
    }
  }
  {
    const u32 h = hash32(ch.cx, ch.cz, 0x1A7F, seed_);
    if ((h & 255) < 70) {   // a lavafall: lava breaking out of the roof (it runs when the land is first seen)
      const int x = 2 + (int)((h >> 8) % 12), z = 2 + (int)((h >> 12) % 12);
      const NetherCol& c = cols[z * 16 + x];
      if (!c.pillar && c.biome == NB_CRAG && c.roof - c.floor > 24 && b.blocks[colIdx(x, c.roof, z)] != AIR && b.blocks[colIdx(x, c.roof - 1, z)] == AIR)
        b.blocks[colIdx(x, c.roof, z)] = B.lava;
    }
  }
  glowstone(b, ch.cx, ch.cz);
  ores(b, ch.cx, ch.cz);
  structures.build(b, ch);
  if (B.void_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.void_crust;
  b.store(ch);
}

bool DepthsGen::solidBTA(int x, int y, int z) const {
  if (y <= 0 || y >= DIM_H - 1) return true;
  const NetherCol c = bta_->column(x, z);
  return y <= c.floor || y >= c.roof || c.pillar;
}

TerrainColumn DepthsGen::terrainBTA(int x, int z) const {
  const NetherCol c = bta_->column(x, z);
  TerrainColumn t;
  t.y = c.pillar ? c.roof : c.floor;
  if (c.floor < LAVA_Y) t.water = LAVA_Y;
  if (c.pool && c.poolTop > c.floor) t.water = c.poolTop;
  t.jsY = (int)(t.y / NETHER_K);
  return t;
}

void DepthsGen::sampleBTA(int wx, int wz, ColumnSample& s) const {
  const NetherCol c = bta_->column(wx, wz);
  s = ColumnSample();
  s.height = (i16)c.floor; s.roof = (i16)c.roof; s.roofBlock = c.biome == NB_VOLCANIC ? B.basalt : c.biome == NB_CRYSTAL ? B.blackstone : B.netherrack;
  BlockId top = B.netherrack, under = B.netherrack;
  switch (c.biome) {
    case NB_CRAG: top = c.floor <= LAVA_Y + 3 ? B.soul_sand : B.netherrack; break;
    case NB_VOLCANIC: top = c.floor <= LAVA_Y + 2 ? B.cinder_sand : c.cone ? B.basalt : B.blackstone; under = B.basalt; break;
    case NB_SULFUR: top = B.sulphur_block; break;
    case NB_CRYSTAL: top = B.blackstone; under = B.blackstone; break;
    default: top = B.ash; under = B.ash; break;
  }
  s.top = top; s.under = under;
  if (c.floor < LAVA_Y) { s.water = (i16)LAVA_Y; s.fluid = B.lava; }
  if (c.pool && c.poolTop > c.floor) { s.water = (i16)c.poolTop; s.fluid = c.pool == 1 ? B.lava : (B.acid ? B.acid : B.lava); }
}

} // namespace dq
