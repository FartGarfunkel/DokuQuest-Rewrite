// gen/nether_bta.cpp — see nether_bta.h. NetherShape (the ground, the roof and the lands) and DepthsGen's terrain
// version 5 generation (DepthsGen::generateBTA and its queries).
#include "gen/nether_bta.h"
#include "gen/dimgens.h"
#include "world/dims.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace dq {
using namespace vmap;

const char* netherBiomeName(int b) {
  static const char* N[NB_COUNT] = {
    "The Crag", "Volcanic Islands", "Sulfur Pools", "Crystal Forest", "The Old World",
    "The Shelf", "Crystal Plains", "Old World Desert"};
  return b >= 0 && b < NB_COUNT ? N[b] : "The Nether";
}

namespace {
inline double sat(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
inline double smooth(double t) { t = sat(t); return t * t * (3 - 2 * t); }
inline double h01(u32 h, int shift) { return ((h >> shift) & 0xFFFF) / 65535.0; }
int pickBiome(double r) { return r < 0.24 ? NB_CRAG : r < 0.44 ? NB_VOLCANIC : r < 0.62 ? NB_SULFUR : r < 0.80 ? NB_CRYSTAL : NB_OLDWORLD; }
constexpr double PI_D = 3.14159265358979323846;
} // namespace

NetherShape::NetherShape(i32 seed, bool open8)
  : seed_(seed), open8_(open8), nWarp((u32)seed ^ 0x51a7u), nA((u32)seed ^ 0x1b2cu), nB((u32)seed ^ 0x2c3du), nC((u32)seed ^ 0x3d4eu),
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
  if (open8_) {   // terrain 8: BTA's seven lands as walkable ground (version 5's shapes unchanged for its worlds)
    switch (b) {
      case NB_CRAG: {   // rolling crags, a ridge here and there, the odd short spire
        double h = 66 + 10 * nA.noise2(x / 80.0, z / 80.0) + 4 * nB.noise2(x / 24.0, z / 24.0);
        const double r = 1 - std::fabs(nC.noise2(x / 46.0, z / 46.0));
        h += 9 * r * r * r;
        const double sp = nB.noise2(x / 11.0 + 300, z / 11.0 - 200);
        if (sp > 0.72) h += (sp - 0.72) * 40;
        return h;
      }
      case NB_VOLCANIC: {   // islands half out of the lava, channels between them, the cones as before
        const double cone = floorOfV5(NB_VOLCANIC, x, z, c);
        const double land = 48.5 + 7 * nA.noise2(x / 70.0 + 40, z / 70.0) + 3 * nB.noise2(x / 20.0, z / 20.0);
        return std::max(cone, land);
      }
      case NB_CRYSTAL_PLAINS: return 64 + 2.5 * nA.noise2(x / 50.0 - 30, z / 50.0 + 70) + 1.0 * nB.noise2(x / 12.0, z / 12.0);
      case NB_SHELF: {   // terraces: the ground in steps of five, each a broad ledge
        const double base = 62 + 12 * nA.noise2(x / 90.0 + 15, z / 90.0 - 45);
        return std::floor(base / 5.0) * 5.0 + 0.8 * nB.noise2(x / 9.0, z / 9.0);
      }
      default: break;   // sulfur pools, the crystal forest, the Old World: as version 5 makes them
    }
  }
  return floorOfV5(b, x, z, c);
}

double NetherShape::floorOfV5(int b, int x, int z, NetherCol& c) const {
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
  static const double ADJ[NB_COUNT] = {-4, 12, -8, 0, 16, 6, 4, 16}, TH[NB_COUNT] = {0.3, 0.5, 0.45, 0.4, 0.55, 0.5, 0.55, 0.55};
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

NetherCol NetherShape::columnWeighted(int x, int z, const float w[NB_COUNT]) const {
  NetherCol c;
  int best = 0; for (int b = 1; b < NB_COUNT; b++) if (w[b] > w[best]) best = b;
  double fl = 0, rf = 0, sum = 0;
  for (int b = 0; b < NB_COUNT; b++) {
    if (w[b] <= 0.001f) continue;
    NetherCol tmp;
    fl += w[b] * floorOf(b, x, z, b == best ? c : tmp); rf += w[b] * roofOf(b, x, z); sum += w[b];
  }
  if (sum <= 0) return c;
  fl /= sum; rf /= sum;
  c.biome = c.other = (u8)best; c.mix = (float)(w[best] / sum);
  if (c.mix < 0.93f) { c.pool = 0; c.poolTop = -1; c.cone = false; }   // craters and pools only well inside their own land
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

// =================================================================================================== version 6
// Cave density follows BTA 8.0's Nether generator: two limit noises and a main noise, divided and lerped,
// with a cosine wall and a fade to air at the top of the upper three-quarter field. The field is stored
// on sample rows 8..32 of a 33-tall column (4 blocks in x/z, 8 in y). The bottom quarter stays open so
// the sea can reach the thin bedrock cap — a solid slab there would trap the stacked-world shaft.
constexpr int FIELD_Y = 25;   // computed samples in the upper three quarters (stored on rows 8..32)
namespace {
constexpr double PI_N = 3.14159265358979323846;

double vertOff(int dy) {
  double off = std::cos(dy * PI_N * 6.0 / FIELD_Y) * 2.0;
  double edge = dy > FIELD_Y / 2 ? (FIELD_Y - 1 - dy) : dy;
  if (edge < 4) { double d = 4.0 - edge; off -= d * d * d * 10.0; }
  return off;
}
} // namespace

NetherClimate::NetherClimate(i32 seed, int genVersion)
  : seed_(seed), ver_(genVersion), nMin((u32)seed ^ 0x6D01u), nMax((u32)seed ^ 0x6D11u), nMain((u32)seed ^ 0x6D21u),
    nTemp((u32)seed ^ 0xB10Cu), nHum((u32)seed ^ 0xB20Du), nFuzz((u32)seed ^ 0xB30Eu),
    nBeach((u32)seed ^ 0xB40Fu), nSoil((u32)seed ^ 0xB510u) {}

double NetherClimate::f3(const SimplexNoise& n, double x, double y, double z, int oct, double freq, double amp) const {
  double sum = 0;
  for (int i = 0; i < oct; i++) {
    sum += n.noise3(x * freq, y * freq, z * freq) * amp;
    freq *= 0.5; amp *= 2.0;
  }
  return sum;
}
double NetherClimate::f2(const SimplexNoise& n, double x, double z, double scale, double lac) const {
  double sum = 0, freq = 1, amp = 1;
  x *= scale; z *= scale;
  // version 7: each of BTA's 2D simplex octaves also does scale /= 1.5 and amplitude *= 0.55 (SimplexNoise.addRegion),
  // which makes the lands BTA's size and share (Volcanic a third, the Old World a tenth)
  const double inner = ver_ >= 7 ? 1.0 / 1.5 : 1.0, gain = ver_ >= 7 ? 0.55 : 1.0;
  for (int i = 0; i < 4; i++) {
    sum += n.noise2(x * freq * inner, z * freq * inner) * amp * gain;
    freq *= lac; amp *= 2.0;
  }
  return sum;
}
void NetherClimate::climateAt(int x, int z, double& temp, double& humid) const {
  const double tn = f2(nTemp, x, z, 1.0 / 60.0, 0.25);
  const double hn = f2(nHum, x, z, 1.0 / 30.0, 1.0 / 3.0);
  const double fz = f2(nFuzz, x, z, 1.0 / 6.0, 0.5882352941176471);
  const double a = fz * 1.1 + 0.5;
  temp = (tn * 0.15 + 0.7) * 0.99 + a * 0.01;
  temp = 1.0 - (1.0 - temp) * (1.0 - temp);
  humid = (hn * 0.15 + 0.5) * 0.998 + a * 0.002;
  temp = std::clamp(temp, 0.0, 1.0);
  humid = std::clamp(humid, 0.0, 1.0);
  if (ver_ >= 7) { temp = std::floor(temp * 50) / 50; humid = std::floor(humid * 50) / 50; }   // BTA's BiomeRangeLookup(span 50)
}
u8 NetherClimate::biomeFrom(double temp, double humid, int y) const {
  // Exclusive upper bounds. Clamp so 1.0 still lands in the top bin, matching a quantized lookup.
  const double t = std::min(temp, 0.999), h = std::min(humid, 0.999);
  const double alt = std::clamp(y / 128.0, 0.0, 0.999);
  u8 b = NB_OLDWORLD;
  if (t >= 0.95 && h < 1.0 && alt >= 0.5) b = NB_VOLCANIC;
  if (t >= 0.75 && t < 0.95 && h < 0.85 && alt >= 0.5) b = NB_CRAG;
  if (t >= 0.75 && t < 0.95 && h >= 0.85 && alt >= 0.5) b = NB_SULFUR;
  if (t >= 0.70 && t < 0.75 && alt >= 0.5) b = NB_SHELF;
  if (t >= 0.45 && t < 0.70 && h >= 0.1 && alt >= 0.5) b = NB_CRYSTAL;
  if (t >= 0.45 && t < 0.70 && h < 0.1 && alt >= 0.5) b = NB_CRYSTAL_PLAINS;
  if (t < 0.45 && alt >= 0.5) b = NB_OLDWORLD;
  if (alt < 0.5) b = NB_OLD_DESERT;   // last, so the floor under y 64 is desert whatever the climate
  return b;
}
u8 NetherClimate::biomeAt(int x, int y, int z) const {
  double t, h; climateAt(x, z, t, h); return biomeFrom(t, h, y);
}
int NetherClimate::oceanKind(int x, int z) const {
  switch (biomeAt(x, OCEAN_Y, z)) {
    case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: return 2;
    case NB_SHELF: return 4;
    case NB_OLDWORLD: return 3;
    default: return 1;
  }
}
double NetherClimate::fieldDensity(int ix, int dy, int iz, int octaves) const {
  if (octaves < 1) octaves = 1;
  if (octaves > 16) octaves = 16;
  const int skip = 16 - octaves;
  const double freq = std::ldexp(1.0, -skip), amp = std::ldexp(1.0, skip);
  const double minD = f3(nMin, ix * 684.412, dy * 2053.236, iz * 684.412, octaves, freq, amp) / 512.0;
  const double maxD = f3(nMax, ix * 684.412, dy * 2053.236, iz * 684.412, octaves, freq, amp) / 512.0;
  // BTA's main noise has 8 octaves starting at frequency 1, amplitude 1 (version 6 started it 256x too low and loud)
  const int mOct = std::min(octaves, 8), mSkip = 8 - mOct;
  const double main = ver_ >= 7 ? f3(nMain, ix * 8.55515, dy * 34.2206, iz * 8.55515, mOct, std::ldexp(1.0, -mSkip), std::ldexp(1.0, mSkip)) / 10.0
                                : f3(nMain, ix * 8.55515, dy * 34.2206, iz * 8.55515, octaves, freq, amp) / 10.0;
  const double w = (main + 1.0) / 2.0;
  double d = w < 0 ? minD : w > 1 ? maxD : minD + (maxD - minD) * w;
  d -= vertOff(dy);
  if (dy > FIELD_Y - 4) {
    const double m = (dy - (FIELD_Y - 4)) / 3.0;
    d = d * (1.0 - m) + -10.0 * m;
  }
  return d;
}
void NetherClimate::columnDensity(int x, int z, float dens[SAMPLES], int octaves) const {
  for (int i = 0; i < SAMPLES; i++) dens[i] = 0;
  const int ix = floordiv(x, 4), iz = floordiv(z, 4);
  const double tx = (x - ix * 4) / 4.0, tz = (z - iz * 4) / 4.0;
  for (int dy = 0; dy < FIELD_Y; dy++) {
    const double d00 = fieldDensity(ix, dy, iz, octaves), d10 = fieldDensity(ix + 1, dy, iz, octaves);
    const double d01 = fieldDensity(ix, dy, iz + 1, octaves), d11 = fieldDensity(ix + 1, dy, iz + 1, octaves);
    const double a = d00 + (d10 - d00) * tx, b = d01 + (d11 - d01) * tx;
    dens[dy + 8] = (float)(a + (b - a) * tz);
  }
}
float NetherClimate::densityAtY(const float dens[SAMPLES], int y) {
  if (y <= 0) return 1;
  const int iy = y >> 3;
  if (iy >= SAMPLES - 1) return dens[SAMPLES - 1];
  const float ty = (y & 7) / 8.f;
  return dens[iy] * (1.f - ty) + dens[iy + 1] * ty;
}
double NetherClimate::probeAt(int ix, int dy, int iz) const {
  if (dy < 0 || dy >= FIELD_Y) return 0;
  return fieldDensity(ix, dy, iz, 3);
}
bool NetherClimate::solidAt(int x, int y, int z) const {
  if (y <= 0 || y >= 255) return true;
  if (y <= 3 || y >= 252) return true;
  struct Cache { const NetherClimate* self; int x, z, ocean; float d[SAMPLES]; };
  thread_local Cache c{nullptr, INT32_MIN, INT32_MIN, -1, {}};
  if (c.self != this || c.x != x || c.z != z) {
    columnDensity(x, z, c.d, 8);
    c.ocean = oceanKind(x, z);
    c.self = this; c.x = x; c.z = z;
  }
  if (densityAtY(c.d, y) > 0.f) return true;
  if (y < OCEAN_Y) {
    if (c.ocean == 2 && y == OCEAN_Y - 1) return true;
    return c.ocean == 3 || c.ocean == 4;
  }
  return false;
}

bool netherExactLand(i32 seed, int x, int y, int z, u8& land);   // gen/nether_exact.cpp
void netherLandAt(i32 seed, int genVersion, int worldX, int worldY, int worldZ, u8& a, u8& b, float& mix) {
  if (genVersion < 6) { netherBiomeMix(seed, worldX, worldZ, a, b, mix); return; }
  if (genVersion >= 7) {   // BTA's own biomes (exact)
    const int y = std::clamp(worldY - dimBase(DIM_NETHER), 0, 255);
    netherExactLand(seed, worldX, y, worldZ, a); b = a; mix = 1.f;
    return;
  }
  static std::mutex mu; static std::map<std::pair<i32, int>, std::shared_ptr<NetherClimate>> climates;
  std::shared_ptr<NetherClimate> c;
  const int v = genVersion >= 7 ? 7 : 6;
  { std::lock_guard<std::mutex> l(mu); auto& p = climates[{seed, v}]; if (!p) p = std::make_shared<NetherClimate>(seed, v); c = p; }
  int y = worldY - dimBase(DIM_NETHER);
  if (y < 0) y = 0;
  if (y > 255) y = 255;
  a = b = c->biomeAt(worldX, y, worldZ);
  mix = 1.f;
}

namespace {
struct Skin { BlockId top, fill; bool old; };
Skin skinFor(const GenBlocks& B, u8 biome, bool v7 = false) {
  if (v7) switch (biome) {   // BTA 8.0's SurfaceProperties
    case NB_VOLCANIC: return {B.brimsand ? B.brimsand : B.cinder_sand, B.nether_brimstone ? B.nether_brimstone : B.basalt, false};
    case NB_SULFUR: return {B.nether_brimstone ? B.nether_brimstone : B.basalt, B.nether_brimstone ? B.nether_brimstone : B.basalt, false};
    case NB_OLDWORLD: return {B.ash, B.ash, true};
    case NB_OLD_DESERT: return {B.soul_sand, B.soul_sand, true};
    default: return {B.cobbled_netherrack ? B.cobbled_netherrack : B.emberstone, B.cobbled_netherrack ? B.cobbled_netherrack : B.emberstone, false};
  }
  switch (biome) {
    case NB_VOLCANIC: return {B.cinder_sand, B.sulphur_block, false};
    case NB_SULFUR: return {B.sulphur_block, B.sulphur_block, false};
    case NB_OLDWORLD: return {B.ash, B.ash, true};
    case NB_OLD_DESERT: return {B.soul_sand, B.soul_sand, true};
    case NB_SHELF: return {B.permafrost ? B.permafrost : B.netherrack, B.packed_ice ? B.packed_ice : B.netherrack, false};
    default: return {B.emberstone, B.emberstone, false};
  }
}
bool rackBiome(u8 b) { return b == NB_CRAG || b == NB_SHELF || b == NB_CRYSTAL || b == NB_CRYSTAL_PLAINS; }
} // namespace

bool DepthsGen::solidCave(int x, int y, int z) const { return cave_->solidAt(x, y, z); }

TerrainColumn DepthsGen::terrainCave(int x, int z) const {
  TerrainColumn t;
  const int kind = cave_->oceanKind(x, z);
  const bool fluid = kind == 1 || kind == 2;
  auto solid = [&](int y) { return cave_->solidAt(x, y, z); };
  if (!solid(NetherClimate::OCEAN_Y)) {
    int y = NetherClimate::OCEAN_Y - 1;
    while (y > 4 && !solid(y)) y--;
    t.y = y;
    if (fluid) t.water = NetherClimate::OCEAN_Y;
  } else {
    int y = NetherClimate::OCEAN_Y;
    while (y < 248 && solid(y + 1)) y++;
    t.y = y;
  }
  t.jsY = (int)(t.y / NETHER_K);
  return t;
}

void DepthsGen::sampleCave(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample();
  const int kind = cave_->oceanKind(wx, wz);
  const int ix = floordiv(wx, 4), iz = floordiv(wz, 4);
  auto probe = [&](int y) {
    const int dy = (y >> 3) - 8;
    return cave_->probeAt(ix, dy, iz);
  };
  int top = (kind == 3 || kind == 4) ? 95 : -1;
  for (int y = 80; y <= 200; y += 16) if (probe(y) > 0) top = y;
  const u8 biome = cave_->biomeAt(wx, top > 0 ? top : NetherClimate::OCEAN_Y, wz);
  BlockId topB = B.netherrack, under = B.netherrack;
  switch (biome) {
    case NB_VOLCANIC: topB = B.cinder_sand; under = B.sulphur_block; break;
    case NB_SULFUR: topB = B.sulphur_block; under = B.sulphur_block; break;
    case NB_OLDWORLD: topB = B.ash; under = B.ash; break;
    case NB_OLD_DESERT: topB = B.soul_sand; under = B.soul_sand; break;
    case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: topB = B.emberstone; under = B.netherrack; break;
    default: break;
  }
  s.top = topB; s.under = under; s.roof = 250; s.roofBlock = B.netherrack;
  if (top < 0 && (kind == 1 || kind == 2)) {
    s.height = 62; s.water = (i16)NetherClimate::OCEAN_Y; s.fluid = kind == 2 ? B.water : B.lava;
  } else s.height = (i16)(top < 0 ? 8 : top);
}

void DepthsGen::generateCave(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  const NetherClimate& C = *cave_;
  const bool v7 = C.version() >= 7;   // BTA's Nether as BTA makes it (terrain version 7)
  const BlockId cobble = v7 && B.cobbled_netherrack ? B.cobbled_netherrack : B.emberstone;
  // 5 x 25 x 5 density samples. Neighbouring chunks share the edge sample, so the caves join.
  float grid[5][FIELD_Y][5];
  const int ox = ch.cx * 4, oz = ch.cz * 4;
  for (int iz = 0; iz < 5; iz++) for (int ix = 0; ix < 5; ix++) for (int dy = 0; dy < FIELD_Y; dy++)
    grid[ix][dy][iz] = (float)C.fieldDensity(ox + ix, dy, oz + iz, 8);
  auto atRow = [&](int ix, int sy, int iz) -> float {
    if (sy < 8) return 0;
    const int dy = sy - 8;
    if (dy < 0 || dy >= FIELD_Y) return 0;
    return grid[ix][dy][iz];
  };
  struct Col {
    double t, h; bool beach; int soil; u8 atSea;
  };
  Col cols[256];
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x, wz = bz + z;
    Col& c = cols[z * 16 + x];
    C.climateAt(wx, wz, c.t, c.h);
    const double sand = C.f3(C.nBeach, wx * 0.03125, wz * 0.03125, 0, 4, 1, 1);
    const double grav = C.f3(C.nBeach, wx * 0.03125, 109.0134, wz * 0.03125, 4, 1, 1);
    const double j = hashFloat(wx, 2, wz, seed_);
    c.beach = sand + j * 0.2 > 0 || grav + hashFloat(wx, 3, wz, seed_) * 0.2 > 0;
    const double soilN = C.f3(C.nSoil, wx * 0.0625, wz * 0.0625, 0, 4, 1, 1);
    c.soil = (int)(soilN / 3.0 + 3.0 + hashFloat(wx, 4, wz, seed_) * 0.25);
    c.atSea = C.biomeFrom(c.t, c.h, NetherClimate::OCEAN_Y);
  }
  auto oceanKindOf = [](u8 biome) {
    switch (biome) {
      case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: return 2;
      case NB_SHELF: return 4;
      case NB_OLDWORLD: return 3;
      default: return 1;
    }
  };
  auto deepOf = [&](u8 biome, int wx, int y, int wz) {
    const bool flip = hashFloat(wx, y, wz, seed_ ^ 0x5A1) < 0.5;
    if (v7) switch (biome) {   // BTA: rack/cobble, cobble in the crystal lands, brimsand, brimstone, gloomstone/cobbled
      case NB_VOLCANIC: return B.brimsand ? B.brimsand : B.cinder_sand;
      case NB_SULFUR: return B.nether_brimstone ? B.nether_brimstone : B.basalt;
      case NB_OLDWORLD: case NB_OLD_DESERT: return flip && B.cobbled_gloomstone ? B.cobbled_gloomstone : B.gloomstone ? B.gloomstone : B.slate;
      case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: return cobble;
      default: return flip ? B.netherrack : cobble;
    }
    switch (biome) {
      case NB_VOLCANIC: {
        float h = hashFloat(wx, y, wz, seed_ ^ 0xBA5A);
        if (h < 0.5f && B.basalt) return B.basalt;
        if (h < 0.75f && B.blackstone) return B.blackstone;
        return B.cinder_sand;
      }
      case NB_SULFUR: return B.sulphur_block;
      case NB_SHELF: return B.permafrost ? B.permafrost : (flip ? B.netherrack : (B.packed_ice ? B.packed_ice : B.netherrack));
      case NB_OLDWORLD: case NB_OLD_DESERT: {
        float h = hashFloat(wx, y, wz, seed_ ^ 0x01D);
        if (h < 0.22f && B.gloomstone) return B.gloomstone;
        if (h < 0.38f && B.limestone) return B.limestone;
        return flip ? B.slate : B.blackstone;
      }
      case NB_CRYSTAL: case NB_CRYSTAL_PLAINS: {
        float h = hashFloat(wx, y, wz, seed_ ^ 0xC757);
        if (h < 0.34f && B.gloomstone) return B.gloomstone;
        return flip ? B.netherrack : B.emberstone;
      }
      default: {
        float h = hashFloat(wx, y, wz, seed_ ^ 0x6057);
        if (h < 0.1f && B.granite) return B.granite;
        if (h < 0.2f && B.limestone) return B.limestone;
        if (h < 0.34f && B.gloomstone) return B.gloomstone;
        return flip ? B.netherrack : B.emberstone;
      }
    }
  };
  auto mixPlace = [&](u8 biome, BlockId id, int wx, int y, int wz) {
    if (!id) return id;
    const bool flip = hashFloat(wx, y, wz, seed_ ^ 0xA11) < 0.5;
    if (v7) {   // BTA's mixStoneVariant
      if ((biome == NB_CRAG || biome == NB_SHELF) && id == cobble) return flip ? B.netherrack : cobble;
      if ((biome == NB_OLDWORLD || biome == NB_OLD_DESERT) && (id == B.gloomstone || id == B.cobbled_gloomstone) && B.cobbled_gloomstone) return flip ? B.cobbled_gloomstone : B.gloomstone;
      return id;
    }
    if (rackBiome(biome) && id == B.emberstone) return flip ? B.netherrack : B.emberstone;
    if ((biome == NB_OLDWORLD || biome == NB_OLD_DESERT) && (id == B.slate || id == B.blackstone))
      return flip ? B.slate : B.blackstone;
    return id;
  };

  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x, wz = bz + z;
    const int kind = oceanKindOf(cols[z * 16 + x].atSea);
    const int ix = x >> 2, iz = z >> 2;
    const float tx = (x & 3) / 4.f, tz = (z & 3) / 4.f;
    for (int y = 0; y < DIM_H; y++) {
      BlockId id = AIR;
      if (y == 0 || y == DIM_H - 1 || y <= 3) id = B.bedrock;
      else if (y == 4 && hashFloat(wx, y, wz, seed_) < 0.55) id = B.bedrock;
      else if (y >= 252 && hashFloat(wx, y, wz, seed_ ^ 5) < 0.75 - (DIM_H - 1 - y) * 0.15) id = B.bedrock;
      else {
        const int sy = y >> 3;
        const float ty = (y & 7) / 8.f;
        auto row = [&](int srow) {
          const float r00 = atRow(ix, srow, iz), r10 = atRow(ix + 1, srow, iz);
          const float r01 = atRow(ix, srow, iz + 1), r11 = atRow(ix + 1, srow, iz + 1);
          const float a = r00 + (r10 - r00) * tx, c = r01 + (r11 - r01) * tx;
          return a + (c - a) * tz;
        };
        const float d = row(sy) * (1.f - ty) + row(sy + 1) * ty;
        if (d > 0.f) id = B.netherrack;
        else if (y >= 5 && y < NetherClimate::OCEAN_Y) {
          if (kind == 2) id = y == NetherClimate::OCEAN_Y - 1 ? (v7 && B.rubyglass_crystal ? B.rubyglass_crystal : B.red_crystal ? B.red_crystal : B.water) : B.water;
          else if (kind == 4) id = hashFloat(wx, y, wz, seed_ ^ 0x54) < 0.5 ? B.netherrack : cobble;
          else if (kind == 3) id = B.obsidian;
          else id = B.lava;
        }
      }
      b.blocks[colIdx(x, y, z)] = id;
    }
  }

  if (!v7) glowstone(b, ch.cx, ch.cz);

  // Surface: repaint netherrack the way the sea's shores, the soil and the deep stone do.
  // Glowstone and the bedrock stay. Ores run after this so veins in emberstone still land.
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x, wz = bz + z;
    const Col& c = cols[z * 16 + x];
    int depth = -1;
    BlockId filler = B.netherrack;
    u8 skinBiome = c.atSea;
    bool have = false;
    for (int y = DIM_H - 2; y >= 1; y--) {
      BlockId& id = b.blocks[colIdx(x, y, z)];
      const u8 biome = C.biomeFrom(c.t, c.h, y);
      if (id == AIR) { depth = -1; skinBiome = biome; have = true; continue; }
      if (id != B.netherrack) continue;
      if (!have) { skinBiome = biome; have = true; }
      const Skin skin = skinFor(B, skinBiome, v7);
      if (depth == -1) {
        BlockId top = skin.top; filler = skin.fill;
        if (c.soil <= 0) {
          top = AIR;
          if (!v7) filler = deepOf(biome, wx, y, wz);
          else filler = biome == NB_VOLCANIC ? (B.brimsand ? B.brimsand : B.cinder_sand) : biome == NB_SULFUR ? B.sulphur_block
                      : (biome == NB_OLDWORLD || biome == NB_OLD_DESERT) ? deepOf(biome, wx, y, wz) : B.netherrack;
        }
        else if (y >= NetherClimate::OCEAN_Y - 4 && y <= NetherClimate::OCEAN_Y + 1 && c.beach) top = filler = B.soul_sand;
        else if (y <= NetherClimate::OCEAN_Y && biome == NB_SULFUR) {
          if (v7) { top = B.brimsand ? B.brimsand : B.cinder_sand; filler = B.nether_brimstone ? B.nether_brimstone : B.basalt; }
          else { top = B.cinder_sand; filler = B.sulphur_block; }
        }
        if (y < NetherClimate::OCEAN_Y && top == AIR) top = deepOf(biome, wx, y, wz);
        depth = c.soil + 2;
        const BlockId place = y >= NetherClimate::OCEAN_Y - 1 ? top : filler;
        id = place ? mixPlace(skinBiome, place, wx, y, wz) : AIR;
      } else if (depth > 0) {
        id = mixPlace(biome, filler, wx, y, wz);
        if (--depth == 0 && skin.old && filler == B.ash) {
          depth = 2 + (int)(hashFloat(wx, y, wz, seed_ ^ 0x51A) * 8);
          filler = B.slate;
        }
      } else id = deepOf(biome, wx, y, wz);
    }
  }

  ores(b, ch.cx, ch.cz);

  JRandom rnd(chunkSeed(seed_, ch.cx, ch.cz, 0x4E7A));
  auto colBiome = [&](int x, int z) { return cols[z * 16 + x].atSea; };
  auto solidish = [&](BlockId id) {
    return id && id != B.lava && id != B.water && id != B.acid && id != AIR;
  };
  auto findFloor = [&](int x, int z, int y0, int y1, int& y) {
    for (int yy = y1; yy >= y0; yy--) {
      if (b.at(x, yy, z) == AIR && solidish(b.at(x, yy - 1, z))) { y = yy; return true; }
    }
    return false;
  };
  auto findRoof = [&](int x, int z, int y0, int y1, int& y) {
    for (int yy = y0; yy <= y1; yy++) {
      if (b.at(x, yy, z) == AIR && solidish(b.at(x, yy + 1, z))) { y = yy; return true; }
    }
    return false;
  };
  if (v7) {   // BTA 8.0's large features and decorations (gen/nether_bta7.cpp)
    u8 atSea[256];
    for (int i = 0; i < 256; i++) atSea[i] = cols[i].atSea;
    btaFeatures7(b, ch.cx, ch.cz, atSea);
    structures.build(b, ch);
    if (B.void_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.void_crust;
    b.store(ch);
    return;
  }
  // Crystals: tall in the forest, shorter and fewer on the plains. Floor and ceiling.
  const bool anyCrystal = [&] {
    for (int i = 0; i < 256; i++) if (cols[i].atSea == NB_CRYSTAL || cols[i].atSea == NB_CRYSTAL_PLAINS) return true;
    return false;
  }();
  if (anyCrystal && B.red_crystal) {
    const int tries = 14;
    for (int n = 0; n < tries; n++) {
      const int x = rnd.nextInt(16), z = rnd.nextInt(16);
      const u8 land = colBiome(x, z);
      if (land != NB_CRYSTAL && land != NB_CRYSTAL_PLAINS) continue;
      const bool forest = land == NB_CRYSTAL;
      if (!forest && rnd.nextInt(3) != 0) continue;
      const int h = (forest ? 6 : 3) + rnd.nextInt(forest ? 10 : 5);
      const BlockId mat = B.charged_crystal && rnd.nextInt(12) == 0 ? B.charged_crystal : B.red_crystal;
      int y = 0;
      if (findFloor(x, z, 70, 150, y)) {
        for (int k = 0; k < h && y + k < DIM_H - 2 && b.at(x, y + k, z) == AIR; k++) b.blocks[colIdx(x, y + k, z)] = mat;
      }
      if (findRoof(x, z, 120, 230, y)) {
        for (int k = 0; k < h && y - k > 2 && b.at(x, y - k, z) == AIR; k++) b.blocks[colIdx(x, y - k, z)] = mat;
      }
    }
  }
  // Sulfur: acid pools and steam vents on the crust.
  for (int n = 0; n < 4; n++) {
    const int x = 2 + rnd.nextInt(12), z = 2 + rnd.nextInt(12);
    if (colBiome(x, z) != NB_SULFUR) continue;
    int y = 0;
    if (!findFloor(x, z, 70, 140, y)) continue;
    if (B.acid) {
      for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        const int sx = x + dx, sz = z + dz;
        if (sx < 0 || sx > 15 || sz < 0 || sz > 15) continue;
        if (b.at(sx, y - 1, sz) == AIR) continue;
        b.blocks[colIdx(sx, y - 1, sz)] = B.sulphur_block ? B.sulphur_block : B.netherrack;
        if (dx == 0 && dz == 0) b.blocks[colIdx(sx, y, sz)] = B.acid;
        else if (B.sulphur_block && b.at(sx, y, sz) == AIR && rnd.nextBool()) b.blocks[colIdx(sx, y, sz)] = B.sulphur_block;
      }
    }
    if (B.steam_vent) {
      if (b.at(x, y, z) == AIR) b.blocks[colIdx(x, y - 1, z)] = B.steam_vent;
      else if (x + 1 < 16 && b.at(x + 1, y, z) == AIR && solidish(b.at(x + 1, y - 1, z))) b.blocks[colIdx(x + 1, y - 1, z)] = B.steam_vent;
    }
  }
  // Volcanic spires: a basalt stack with magma, sometimes a lava cap.
  for (int n = 0; n < 6; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    if (colBiome(x, z) != NB_VOLCANIC) continue;
    int y = 0;
    if (!findFloor(x, z, 80, 160, y) || !B.basalt) continue;
    const int h = 4 + rnd.nextInt(8);
    for (int k = 0; k < h && y + k < DIM_H - 2 && b.at(x, y + k, z) == AIR; k++)
      b.blocks[colIdx(x, y + k, z)] = k > h - 3 && B.magma_block ? B.magma_block : B.basalt;
    const int cap = y + h;
    if (cap < DIM_H - 1 && b.at(x, cap, z) == AIR && rnd.nextInt(3) == 0) b.blocks[colIdx(x, cap, z)] = B.lava;
  }
  // Fire on netherrack in the hot lands.
  for (int n = 0; n < 18; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    const u8 land = colBiome(x, z);
    if (land != NB_CRAG && land != NB_SHELF && land != NB_VOLCANIC && land != NB_SULFUR) continue;
    int y = 0;
    if (!findFloor(x, z, 64, 200, y) || !B.fire) continue;
    if (b.at(x, y - 1, z) == B.netherrack || b.at(x, y - 1, z) == B.emberstone) b.blocks[colIdx(x, y, z)] = B.fire;
  }
  // Old world: bone piles, a petrified trunk, and ash drifts where there is room to walk.
  for (int n = 0; n < 6; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    const u8 land = colBiome(x, z);
    if (land != NB_OLDWORLD && land != NB_OLD_DESERT) continue;
    int y = 0;
    if (!findFloor(x, z, 80, 180, y) || !B.bone_block) continue;
    b.blocks[colIdx(x, y, z)] = B.bone_block;
    if (x + 1 < 16 && b.at(x + 1, y, z) == AIR) b.blocks[colIdx(x + 1, y, z)] = B.bone_block;
  }
  if (B.ashen_trunk && rnd.nextInt(3) == 0) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    if (colBiome(x, z) == NB_OLDWORLD) {
      int y = 0;
      if (findFloor(x, z, 80, 170, y)) {
        const int h = 3 + rnd.nextInt(4);
        for (int k = 0; k < h && y + k < DIM_H - 2 && b.at(x, y + k, z) == AIR; k++) b.blocks[colIdx(x, y + k, z)] = B.ashen_trunk;
      }
    }
  }
  if (B.ash) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const u8 land = C.biomeFrom(cols[z * 16 + x].t, cols[z * 16 + x].h, 120);
    if (land != NB_OLDWORLD && land != NB_OLD_DESERT) continue;
    if (hashFloat(bx + x, 9, bz + z, seed_) > 0.22) continue;
    for (int y = NetherClimate::OCEAN_Y + 1; y < 200; y++) {
      const BlockId here = b.at(x, y, z), under = b.at(x, y - 1, z);
      if (here != AIR || !solidish(under)) continue;
      if (under == B.obsidian || under == B.soul_sand || under == B.red_crystal) continue;
      if (b.at(x, y + 1, z) != AIR) continue;
      b.blocks[colIdx(x, y, z)] = B.smouldering_ash && hashFloat(bx + x, y, bz + z, seed_) < 0.08 ? B.smouldering_ash : B.ash;
      break;
    }
  }
  // A pillar on the 32-block grid, when this chunk holds its column and the gap is short enough to bridge.
  {
    const int gx = floordiv(bx + 8, 32), gz = floordiv(bz + 8, 32);
    const u32 ph = hash32(gx, gz, 0x7100, seed_);
    if ((ph & 7) == 0) {
      const int px = gx * 32 + (int)(ph >> 8 & 31), pz = gz * 32 + (int)(ph >> 16 & 31);
      const int lx = px - bx, lz = pz - bz;
      if (lx >= 0 && lx < 16 && lz >= 0 && lz < 16) {
        int floor = 0, roof = 0;
        if (findFloor(lx, lz, 64, 140, floor) && findRoof(lx, lz, floor + 4, 220, roof) && roof - floor < 48) {
          const u8 land = colBiome(lx, lz);
          BlockId mat = B.netherrack;
          if (land == NB_VOLCANIC || land == NB_SULFUR) mat = B.sulphur_block ? B.sulphur_block : B.basalt;
          else if (land == NB_OLDWORLD) mat = B.slate ? B.slate : B.netherrack;
          else if (land == NB_CRYSTAL || land == NB_CRYSTAL_PLAINS) mat = B.red_crystal ? B.red_crystal : B.emberstone;
          for (int y = floor; y <= roof && mat; y++) if (b.at(lx, y, lz) == AIR) b.blocks[colIdx(lx, y, lz)] = mat;
        }
      }
    }
  }

  structures.build(b, ch);
  if (B.void_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.void_crust;
  b.store(ch);
}

} // namespace dq
