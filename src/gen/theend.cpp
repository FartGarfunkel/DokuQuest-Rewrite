// gen/theend.cpp — The End (game/theend.js): the main island of end stone with its ring of obsidian pillars
// and the exit portal's bowl, the platform the stronghold portal lets you down onto, and past the gulf the
// outer islands with their chorus groves. Shapes are the original's, lifted END_DY (64). End cities are
// structures (gen/structures.h, DIM_END).
#include "gen/be/be_terrain.h"
#include "gen/dimgens.h"

namespace dq {
using namespace vmap;
static const double JS_TAU = 6.283185307179586;   // Math.PI * 2 (common.h's TAU is a float)


EndGen::EndGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed), nA(o.seed ^ 0xE4D01), nB(o.seed ^ 0xE4D02), nC(o.seed ^ 0xE4D03) {
  B.resolve();
  if (o.genVersion >= 7) {   // Better End's islands past the gulf; the dragon's own island stays as it is
    be::BETerrainOptions bo; bo.voidRing = OUTER; bo.centralIsland = false;
    be_ = std::make_shared<be::BETerrain>((int64_t)o.seed, bo);
  }
  // the ten obsidian pillars (WorldGen.endPillars): where, how high, how thick; the two tallest caged
  Mulberry32 rnd((u32)(seed_ ^ 0x5b1e5));
  double off = rnd() * JS_TAU;
  int hs[10];
  for (int i = 0; i < 10; i++) hs[i] = 76 + i * 3;
  for (int i = 9; i > 0; i--) { int j = jsFloor(rnd() * (i + 1)); std::swap(hs[i], hs[j]); }
  for (int i = 0; i < 10; i++) {
    double a = off + i / 10.0 * JS_TAU; int h = hs[i];
    pillars_.push_back({jsRound(std::cos(a) * PILLAR_R), jsRound(std::sin(a) * PILLAR_R), h + END_DY, 2 + (h - 76) / 8, h >= 97});
  }
}

bool EndGen::island(int x, int z, Isle& o) const {
  double r = std::hypot((double)x, (double)z);
  if (r < 112) {
    double R = 80 + nA.fbm2(x / 60.0, z / 60.0, 2) * 9;
    if (r >= R) return false;
    double k = 1 - (r / R) * (r / R);
    int top = 61 + jsFloor(k * 3 + nB.noise2(x / 24.0, z / 24.0) * 1.5);
    int bot = top - jsFloor(3 + k * 38 + nC.noise2(x / 18.0, z / 18.0) * 4);
    o = {top + END_DY, bot + END_DY, true};
    return true;
  }
  if (r < OUTER) return false;
  if (be_) {
    auto C = beCols(floordiv(x, 16), floordiv(z, 16));
    const int i = ((z & 15) << 4) | (x & 15);
    if (C->top[i] < 0) return false;
    o = {C->top[i], C->bot[i], false};
    return true;
  }
  double th = 0.28 + std::max(0.0, (OUTER + 70 - r) / 70) * 0.45 - std::min(0.1, std::max(0.0, r - OUTER - 70) / 4000);
  double m = nA.fbm2(x / 130.0, z / 130.0, 3) + nB.noise2(x / 40.0, z / 40.0) * 0.12;
  if (m < th) return false;
  double k = std::min(1.0, (m - th) * 5);
  int top = 58 + jsFloor(nC.fbm2(x / 90.0, z / 90.0, 2) * 9 + k * 3);
  int bot = top - jsFloor(2 + k * 22 + nB.noise2(x / 15.0, z / 15.0) * 3);
  // A slow lift per land, so a flight crosses peaks, shelves and lake bowls without
  // turning every island into a cliff. End cities still want a flat footprint.
  const int eb = endBiome(x, z);
  const double slow = nA.noise2(x / 220.0 + 4.0, z / 220.0 - 6.0);
  int lift = 0;
  switch (eb) {
    case 4: lift = 18 + jsFloor((slow + 1.0) * 6); break;                 // crystal mountains
    case 12: lift = 8 + jsFloor(std::fabs(slow) * 10); break;              // painted mountains
    case 9: lift = 5 + jsFloor((slow + 1.0) * 3); break;                   // amber hills
    case 8: lift = 6; break;                                               // ice shelves
    case 6: lift = 2; break;                                               // umbrella jungle
    case 1: lift = jsFloor(slow * 3); break;                               // spire fields
    case 5: lift = -4; break;                                              // sulphur springs
    case 7: lift = -3; break;                                              // shadow woods
    case 15: lift = -2; break;                                             // dry shrubland
    case 10: lift = -1; break;                                             // megalake, dented further below
    case 16: lift = 2 + jsFloor((nB.noise2(x / 38.0 + 5.0, z / 52.0 - 3.0) + 1.0) * 5); break;   // dust wastelands' dunes (BE terrainHeight 1.5)
    case 18: lift = 0; break;                                              // megalake grove
    default: break;
  }
  if (eb == 10) {
    double bowl = nB.noise2(x / 110.0 + 8.0, z / 110.0 - 2.0);
    if (bowl > 0.08) lift -= jsFloor((bowl - 0.08) * 18);
  }
  top += lift;
  if (top < bot + 4) bot = top - 4;
  double cd = craterDepth(x, z);   // crater bowls dent the barrens (their rims stand a little proud)
  if (cd != 0) { top -= (int)std::lround(cd); if (top <= bot + 1) return false; }
  o = {top + END_DY, bot + END_DY, false};
  return true;
}

// ---- the outer End: sixteen lands, Better End's biomes (gen/theend_be.cpp dresses them) ----
//  0 chorus forest     1 blossoming spires 2 foggy mushroomland 3 glowing grasslands
//  4 crystal mountains 5 sulphur springs   6 umbrella jungle    7 shadow forest
//  8 ice starfield     9 amber land       10 megalake          11 lantern woods
// 12 painted mountains 13 neon oasis      14 dragon graveyards 15 dry shrubland
// 16 dust wastelands   17 umbra valley     18 megalake grove
static int endBiomeOf(const SimplexNoise& nA, const SimplexNoise& nB, const SimplexNoise& nC, int x, int z) {
  double b = nC.noise2(x / 380.0 + 51.3, z / 380.0 - 17.9);
  double v = nA.noise2(x / 240.0 - 88.1, z / 240.0 + 41.7);
  double w = nB.noise2(x / 200.0 + 12.4, z / 200.0 - 33.0);
  if (b < -0.46) return w > 0.05 ? 4 : 1;
  if (b < -0.22) {
    if (w > 0.38) return 13;
    if (w < -0.38) return 9;
    return 1;
  }
  if (b > 0.50) return w > 0.0 ? 8 : w > -0.25 ? 16 : 12;   // ice starfield, dust wastelands, painted mountains
  if (b > 0.26) {
    if (w < -0.28) return 5;
    if (w > 0.34) return v > 0.0 ? 18 : 10;   // megalake grove, megalake
    return 2;
  }
  if (v > 0.46) return w > 0.05 ? 6 : 7;
  if (v > 0.20) return w < -0.30 ? 14 : 3;
  if (v < -0.38) return w > 0.15 ? 11 : w < -0.32 ? 17 : 15;   // lantern woods, umbra valley, dry shrubland
  return w > 0.42 ? 6 : 0;
}
// Better End's land (be::BETerrain::land) as ours: its vanilla End lands (highlands, midlands, small islands) and the
// centre take chorus forest's dressing, the nearest of ours
static int beLand(const be::BETerrain& T, int x, int z) { const int l = T.land(x, z); return l < 0 || l > 18 ? 0 : l; }
int EndGen::endBiome(int x, int z) const {
  if (be_) {
    auto C = beCols(floordiv(x, 16), floordiv(z, 16));
    return C->land[((z & 15) << 4) | (x & 15)];
  }
  return endBiomeOf(nA, nB, nC, x, z);
}
int endLandAt(i32 seed, int x, int z) {
  SimplexNoise nA(seed ^ 0xE4D01), nB(seed ^ 0xE4D02), nC(seed ^ 0xE4D03);
  return endBiomeOf(nA, nB, nC, x, z);
}
int endLandAt(i32 seed, int genVersion, int x, int z) {
  if (genVersion < 7) return endLandAt(seed, x, z);
  // one Better End terrain per seed for the sky, the spawns and the particles (cheap queries; it caches its biome map)
  static std::mutex mx; static i32 lastSeed = 0; static std::shared_ptr<const be::BETerrain> T;
  std::shared_ptr<const be::BETerrain> t;
  { std::lock_guard<std::mutex> lk(mx);
    if (!T || lastSeed != seed) { be::BETerrainOptions bo; bo.voidRing = EndGen::OUTER; bo.centralIsland = false; T = std::make_shared<be::BETerrain>((int64_t)seed, bo); lastSeed = seed; }
    t = T; }
  return beLand(*t, x, z);
}

// a chunk's columns of Better End land: the top and the lowest bottom of what stands there, and its land
std::shared_ptr<const EndGen::BECols> EndGen::beCols(int cx, int cz, const float* density) const {
  const u64 key = ((u64)(u32)cx << 32) | (u32)cz;
  { std::lock_guard<std::mutex> lk(beMx_); auto it = beCache_.find(key); if (it != beCache_.end()) return it->second; }
  thread_local std::vector<float> buf;
  if (!density) { buf.resize(be::BETerrain::kChunkVol); be_->chunkDensity(cx, cz, buf.data()); density = buf.data(); }
  auto C = std::make_shared<BECols>();
  int8_t lands[256]; be_->chunkLand(cx, cz, lands);
  for (int i = 0; i < 256; i++) {
    int top = -1, bot = -1;
    for (int y = DIM_H - 1; y >= 1; y--) if (density[(y << 8) | i] > 0) { top = y; break; }
    if (top > 0) for (int y = 1; y <= top; y++) if (density[(y << 8) | i] > 0) { bot = y; break; }
    C->top[i] = (i16)top; C->bot[i] = (i16)bot;
    C->land[i] = (i8)(lands[i] < 0 || lands[i] > 18 ? 0 : lands[i]);
  }
  std::lock_guard<std::mutex> lk(beMx_);
  auto [it, fresh] = beCache_.emplace(key, C);
  if (fresh) { beOrder_.push_back(key); while (beOrder_.size() > 6000) { beCache_.erase(beOrder_.front()); beOrder_.pop_front(); } }
  return it->second;
}
double EndGen::craterDepth(int x, int z) const {
  if (be_) return 0;   // Better End has no craters
  if (std::hypot((double)x, (double)z) < OUTER + 40 || nC.noise2(x / 320.0 + 51.3, z / 320.0 - 17.9) < 0.2) return 0;   // only in and round the barrens
  const int C = 48;
  int cx = jsFloor(x / (double)C), cz = jsFloor(z / (double)C);
  double best = 0;
  for (int gz = cz - 1; gz <= cz + 1; gz++) for (int gx = cx - 1; gx <= cx + 1; gx++) {
    u32 h = hash32(gx, gz, seed_, 0xC4A7);
    if ((h & 255) > 150) continue;
    double ox = gx * C + 8 + ((h >> 8) & 31), oz = gz * C + 8 + ((h >> 13) & 31);
    if (endBiome((int)ox, (int)oz) != 2) continue;
    double R = 8 + ((h >> 18) & 15) * 0.7, d = std::hypot(x - ox, z - oz) / R;
    if (d < 1) best = std::max(best, (1 - d * d) * R * 0.45);
    else if (d < 1.25 && best <= 0) best = std::min(best, -(1.25 - d) * 8);   // the rim
  }
  return best;
}

void EndGen::outerFeatures(int bx, int bz, ChunkWriter& P) const {
  auto stone = [&](int x, int y, int z) { return ((y * 3 + (x ^ z)) & 7) == 0 ? B.null_stone : B.end_stone; };   // banded
  // spires: tapering needles of end stone, null-stone banded, an obsidian tip
  const int SC = 14;
  for (int gz = jsFloor((bz - 6) / (double)SC); gz <= jsFloor((bz + 21) / (double)SC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)SC); gx <= jsFloor((bx + 21) / (double)SC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x5917E);
      int sx = gx * SC + 2 + (int)(h % 10), sz = gz * SC + 2 + (int)((h >> 4) % 10);
      if (endBiome(sx, sz) != 1 || ((h >> 9) & 7) > 4) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 18 + (int)((h >> 12) % 40); float rb = 1.6f + ((h >> 18) & 7) * 0.35f;
      for (int dy = -2; dy <= H; dy++) {
        float r = rb * (1 - (float)std::max(0, dy) / (H + 2)) + 0.45f; int y = c.top + dy;
        if (y >= DIM_H - 2) break;
        for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
          if (dx * dx + dz * dz > r * r) continue;
          int x = sx + dx, z = sz + dz;
          if (P.in(x, z)) P.put(x, y, z, dy >= H - 1 ? B.obsidian : stone(x, y, z));
        }
      }
      // a few spires open a blossom: a wide cap of wart and a shroomlight heart
      if (((h >> 20) & 3) == 0 && B.warped_wart_block) {
        int y = c.top + H;
        if (y < DIM_H - 2) for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) {
          if (dx * dx + dz * dz > 10) continue;
          int x = sx + dx, z = sz + dz;
          if (!P.in(x, z)) continue;
          BlockId petal = B.blossom_cap ? B.blossom_cap : B.warped_wart_block;
          P.put(x, y, z, dx == 0 && dz == 0 && B.shroomlight ? B.shroomlight : petal);
          if (std::abs(dx) + std::abs(dz) <= 2 && y + 1 < DIM_H - 2) P.put(x, y + 1, z, petal);
        }
      }
    }
  // arches: a stone bow between two islands' tops
  const int AC = 72;
  for (int gz = jsFloor((bz - 30) / (double)AC); gz <= jsFloor((bz + 45) / (double)AC); gz++)
    for (int gx = jsFloor((bx - 30) / (double)AC); gx <= jsFloor((bx + 45) / (double)AC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0xA3C4);
      if ((h & 7) > 2) continue;
      int ax = gx * AC + 20 + (int)((h >> 3) % 32), az = gz * AC + 20 + (int)((h >> 8) % 32);
      if (std::hypot((double)ax, (double)az) < OUTER + 40) continue;
      bool alongX = (h >> 13) & 1; int R = 10 + (int)((h >> 14) % 12);
      int fx0 = alongX ? ax - R : ax, fz0 = alongX ? az : az - R, fx1 = alongX ? ax + R : ax, fz1 = alongX ? az : az + R;
      Isle a, b2; if (!island(fx0, fz0, a) || !island(fx1, fz1, b2)) continue;
      int base = std::min(a.top, b2.top);
      const float tube = 2.2f;
      for (int t = -R - 3; t <= R + 3; t++) for (int w = -3; w <= 3; w++) {
        int x = alongX ? ax + t : ax + w, z = alongX ? az + w : az + t;
        if (!P.in(x, z)) continue;
        for (int y = base - 2; y <= base + R + 4 && y < DIM_H - 2; y++) {
          double dy = y - base, dd = std::hypot((double)t, dy);   // distance from the arch's centre line (a half circle of radius R)
          double off = std::hypot(dd - R, (double)w);
          if (dy >= -2 && off <= tube) P.put(x, y, z, stone(x, y, z));
        }
      }
    }
  // floating shards: small upturned islands drifting above
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    int x = bx + lx, z = bz + lz;
    double m = nB.fbm2(x / 46.0 + 300.5, z / 46.0 - 200.5, 2);
    if (m < 0.42) continue;
    int top = 96 + END_DY + jsFloor(nA.noise2(x / 70.0, z / 70.0) * 14), bot = top - jsFloor((m - 0.42) * 40);
    int eb = endBiome(x, z);
    BlockId cap = B.end_stone;
    if (eb == 0) cap = B.chorus_turf ? B.chorus_turf : (B.end_moss ? B.end_moss : B.end_stone);
    else if (eb == 3) cap = B.glow_turf ? B.glow_turf : (B.end_moss ? B.end_moss : B.end_stone);
    else if (eb == 6) cap = B.umbrella_turf ? B.umbrella_turf : (B.end_moss ? B.end_moss : B.end_stone);
    else if (eb == 11) cap = B.lantern_turf ? B.lantern_turf : (B.end_moss ? B.end_moss : B.end_stone);
    else if (eb == 1 || eb == 4) cap = B.crystal_turf ? B.crystal_turf : (B.amethyst_block ? B.amethyst_block : B.end_stone);
    else if (eb == 8) cap = B.packed_ice ? B.packed_ice : B.end_stone;
    else if (eb == 5 && B.sulphur_block) cap = B.sulphur_block;
    else if (eb == 9 && B.amber_block) cap = B.amber_block;
    else if (eb == 13 && B.neon_moss) cap = B.neon_moss;
    else if (eb == 7 && B.shadow_soil) cap = B.shadow_soil;
    else if (eb == 14 && B.bone_block) cap = B.bone_block;
    for (int y = bot; y <= top; y++) P.put(x, y, z, y == top ? cap : B.end_stone);
  }
  beDress(bx, bz, P);   // Better End's trees and growths (gen/theend_be.cpp)
}

// Trees, springs, stars and ribs on the outer lands. Each one is its own cell so a chunk only
// builds the features whose footprint can reach it. Shapes are this engine's, from blocks it already has.
void EndGen::landDress(int bx, int bz, ChunkWriter& P) const {
  auto disc = [&](int x, int y, int z, int rad, BlockId id, int rad2 = -1, BlockId id2 = 0) {
    if (y <= 0 || y >= DIM_H - 2 || !id) return;
    int R = std::max(rad, rad2);
    for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      int d2 = dx * dx + dz * dz;
      BlockId put = 0;
      if (d2 <= rad * rad) put = id;
      else if (rad2 >= 0 && d2 <= rad2 * rad2) put = id2;
      if (!put) continue;
      int xx = x + dx, zz = z + dz;
      if (P.in(xx, zz)) P.put(xx, y, zz, put);
    }
  };
  auto trunk = [&](int x, int z, int y0, int h, BlockId id) {
    if (!id) return;
    for (int i = 0; i < h && y0 + i < DIM_H - 2; i++) if (P.in(x, z)) P.put(x, y0 + i, z, id);
  };
  // crystal mountains: amethyst needles with a bud at the tip
  const int CC = 12;
  for (int gz = jsFloor((bz - 8) / (double)CC); gz <= jsFloor((bz + 24) / (double)CC); gz++)
    for (int gx = jsFloor((bx - 8) / (double)CC); gx <= jsFloor((bx + 24) / (double)CC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0xC757);
      if ((h & 3) == 0) continue;
      int sx = gx * CC + 2 + (int)(h % 8), sz = gz * CC + 2 + (int)((h >> 4) % 8);
      if (endBiome(sx, sz) != 4) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 8 + (int)((h >> 10) % 18);
      for (int dy = 0; dy <= H; dy++) {
        int y = c.top + dy; if (y >= DIM_H - 2) break;
        int rad = dy > H - 3 ? 0 : dy > H / 2 ? 1 : 2;
        for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) {
          if (dx * dx + dz * dz > rad * rad + (rad > 0)) continue;
          int x = sx + dx, z = sz + dz;
          if (!P.in(x, z)) continue;
          BlockId id = dy == H && B.amethyst_cluster ? B.amethyst_cluster : dy > H - 4 && B.budding_amethyst ? B.budding_amethyst : B.amethyst_block ? B.amethyst_block : B.calcite;
          if (id) P.put(x, y, z, id);
        }
      }
    }
  // umbrella jungle and the glowing grass: a trunk and a wide cap, the glowshroom's smaller cousin beside it
  const int UC = 16;
  for (int gz = jsFloor((bz - 10) / (double)UC); gz <= jsFloor((bz + 26) / (double)UC); gz++)
    for (int gx = jsFloor((bx - 10) / (double)UC); gx <= jsFloor((bx + 26) / (double)UC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x0B32);
      int sx = gx * UC + 3 + (int)(h % 10), sz = gz * UC + 3 + (int)((h >> 5) % 10);
      int eb = endBiome(sx, sz);
      if (eb != 6 && !(eb == 3 && (h & 7) == 0)) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      bool shroom = eb == 3 || (h & 1);
      int H = shroom ? 5 + (int)((h >> 8) % 4) : 8 + (int)((h >> 8) % 6);
      BlockId stem = shroom ? (B.glowshroom_stem ? B.glowshroom_stem : (B.purpur_pillar ? B.purpur_pillar : B.chorus_plant))
                            : (B.dragon_log ? B.dragon_log : (B.purpur_pillar ? B.purpur_pillar : B.chorus_plant));
      trunk(sx, sz, c.top + 1, H, stem);
      int y = c.top + 1 + H;
      BlockId cap = shroom ? (B.glowshroom_cap ? B.glowshroom_cap : (B.warped_wart_block ? B.warped_wart_block : B.end_moss))
                           : (B.umbrella_cap ? B.umbrella_cap : (B.warped_wart_block ? B.warped_wart_block : B.end_moss));
      BlockId heart = B.shroomlight ? B.shroomlight : B.glowstone;
      int rad = shroom ? 3 : 5;
      disc(sx, y, sz, rad, cap);
      disc(sx, y + 1, sz, rad > 2 ? rad - 2 : 1, cap);
      if (!shroom && y > 1) {
        for (int dz = -6; dz <= 6; dz++) for (int dx = -6; dx <= 6; dx++) {
          int d2 = dx * dx + dz * dz;
          if (d2 < 20 || d2 > 36) continue;
          if ((((dx + 8) * 3 + (dz + 8) * 5 + (int)h) & 3) == 0) continue;
          int xx = sx + dx, zz = sz + dz;
          if (P.in(xx, zz)) P.put(xx, y - 1, zz, cap);
        }
      }
      if (heart && P.in(sx, sz) && y < DIM_H - 2) P.put(sx, y, sz, heart);
    }
  // ice starfield: stars of packed ice drifting over the shelves
  const int IC = 22;
  for (int gz = jsFloor((bz - 8) / (double)IC); gz <= jsFloor((bz + 24) / (double)IC); gz++)
    for (int gx = jsFloor((bx - 8) / (double)IC); gx <= jsFloor((bx + 24) / (double)IC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x1CE5);
      if ((h & 3) > 1) continue;
      int sx = gx * IC + 4 + (int)(h % 14), sz = gz * IC + 4 + (int)((h >> 6) % 14);
      if (endBiome(sx, sz) != 8) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int y = c.top + 8 + (int)((h >> 12) % 10);
      if (y >= DIM_H - 4) continue;
      BlockId ice = (h & 16) && B.blue_ice ? B.blue_ice : B.packed_ice ? B.packed_ice : B.snow_block;
      BlockId core = B.sea_lantern ? B.sea_lantern : B.glowstone;
      if (P.in(sx, sz)) P.put(sx, y, sz, core ? core : ice);
      for (int d = 1; d <= 3; d++) {
        const int arm[6][3] = {{d, 0, 0}, {-d, 0, 0}, {0, d, 0}, {0, -d, 0}, {0, 0, d}, {0, 0, -d}};
        for (auto& a : arm) {
          int x = sx + a[0], yy = y + a[1], z = sz + a[2];
          if (yy <= 0 || yy >= DIM_H - 2 || !P.in(x, z) || !ice) continue;
          if (!P.get(x, yy, z)) P.put(x, yy, z, d == 3 && B.snow_block ? B.snow_block : ice);
        }
      }
    }
  // lantern woods: thin pillars with a lamp, and a few vines
  const int LC = 14;
  for (int gz = jsFloor((bz - 6) / (double)LC); gz <= jsFloor((bz + 22) / (double)LC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)LC); gx <= jsFloor((bx + 22) / (double)LC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x1A47);
      if ((h & 3) == 0) continue;
      int sx = gx * LC + 2 + (int)(h % 10), sz = gz * LC + 2 + (int)((h >> 4) % 10);
      if (endBiome(sx, sz) != 11) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 7 + (int)((h >> 9) % 8);
      BlockId stem = B.lantern_log ? B.lantern_log : (B.purpur_pillar ? B.purpur_pillar : B.end_stone);
      trunk(sx, sz, c.top + 1, H, stem);
      int y = c.top + 1 + H;
      BlockId crown = B.amber_block ? B.amber_block : (B.glowstone ? B.glowstone : stem);
      disc(sx, y, sz, 2, crown);
      if (y + 1 < DIM_H - 2 && P.in(sx, sz)) P.put(sx, y + 1, sz, B.end_rod ? B.end_rod : B.glowstone);
      if (B.cave_vines) for (int k = 1; k <= 3 && (h >> (12 + k)) & 1; k++) {
        int vx = sx + ((h >> 16) & 1 ? 1 : -1), vz = sz;
        if (P.in(vx, vz) && c.top + 1 + H - k > c.top) P.put(vx, y - k, vz, B.cave_vines);
      }
    }
  // dragon graveyards: a rib of bone over a scatter of bone and obsidian
  const int GC = 24;
  for (int gz = jsFloor((bz - 8) / (double)GC); gz <= jsFloor((bz + 24) / (double)GC); gz++)
    for (int gx = jsFloor((bx - 8) / (double)GC); gx <= jsFloor((bx + 24) / (double)GC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0xB0E);
      if ((h & 3) > 1) continue;
      int sx = gx * GC + 6 + (int)(h % 12), sz = gz * GC + 6 + (int)((h >> 5) % 12);
      if (endBiome(sx, sz) != 14) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int R = 4 + (int)((h >> 10) % 3);
      bool alongX = (h >> 14) & 1;
      for (int t = -R; t <= R; t++) {
        double u = (double)t / R;
        int rise = (int)std::lround((1.0 - u * u) * (R + 2));
        int x = sx + (alongX ? t : 0), z = sz + (alongX ? 0 : t);
        int y = c.top + 1 + rise;
        if (y < DIM_H - 2 && P.in(x, z) && B.bone_block) P.put(x, y, z, B.bone_block);
        if (rise > 1 && y - 1 < DIM_H && P.in(x, z) && B.bone_block) P.put(x, y - 1, z, B.bone_block);
      }
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        if ((dx * dx + dz * dz) > 5) continue;
        int x = sx + dx, z = sz + dz;
        if (!P.in(x, z)) continue;
        if (B.bone_block && ((h >> (dx + 8 + dz)) & 1)) P.put(x, c.top, z, B.bone_block);
        else if (dx == 0 && dz == 0) P.put(x, c.top, z, B.obsidian);
      }
    }
  // sulphur springs: a hot pool and a vent
  const int SC = 18;
  for (int gz = jsFloor((bz - 6) / (double)SC); gz <= jsFloor((bz + 22) / (double)SC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)SC); gx <= jsFloor((bx + 22) / (double)SC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x5057);
      if ((h & 3) == 0) continue;
      int sx = gx * SC + 3 + (int)(h % 12), sz = gz * SC + 3 + (int)((h >> 4) % 12);
      if (endBiome(sx, sz) != 5) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        if (dx * dx + dz * dz > 5) continue;
        int x = sx + dx, z = sz + dz;
        if (!P.in(x, z)) continue;
        if (B.sulphur_block) P.put(x, c.top, z, B.sulphur_block);
        if (dx * dx + dz * dz <= 2 && B.magma_block) P.put(x, c.top, z, B.magma_block);
      }
      if (P.in(sx, sz) && B.steam_vent) P.put(sx, c.top + 1, sz, B.steam_vent);
    }
  // neon oasis: short crystal stands. Amber hills: glowstone knobs.
  const int NC = 15;
  for (int gz = jsFloor((bz - 6) / (double)NC); gz <= jsFloor((bz + 22) / (double)NC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)NC); gx <= jsFloor((bx + 22) / (double)NC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x0E0A);
      int sx = gx * NC + 2 + (int)(h % 10), sz = gz * NC + 2 + (int)((h >> 4) % 10);
      int eb = endBiome(sx, sz);
      if (eb != 13 && eb != 9) continue;
      if ((h & 3) == 0) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 2 + (int)((h >> 8) % 4);
      BlockId id = eb == 9 ? (B.amber_block ? B.amber_block : (B.glowstone ? B.glowstone : B.gilded_blackstone))
                           : (B.amethyst_block ? B.amethyst_block : B.calcite);
      trunk(sx, sz, c.top + 1, H, id);
      int y = c.top + 1 + H;
      if (y < DIM_H - 2 && P.in(sx, sz)) {
        BlockId tip = eb == 9 ? (B.gold_block ? B.gold_block : B.shroomlight) : (B.amethyst_cluster ? B.amethyst_cluster : B.sea_lantern);
        if (tip) P.put(sx, y, sz, tip);
      }
    }
  // shadow woods: dark stems and a low wart canopy
  const int HC = 17;
  for (int gz = jsFloor((bz - 6) / (double)HC); gz <= jsFloor((bz + 22) / (double)HC); gz++)
    for (int gx = jsFloor((bx - 6) / (double)HC); gx <= jsFloor((bx + 22) / (double)HC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0x5ADE);
      if ((h & 3) > 1) continue;
      int sx = gx * HC + 2 + (int)(h % 12), sz = gz * HC + 2 + (int)((h >> 5) % 12);
      if (endBiome(sx, sz) != 7) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 5 + (int)((h >> 9) % 4);
      BlockId stem = B.dragon_log ? B.dragon_log : (B.warped_stem ? B.warped_stem : B.null_stone);
      trunk(sx, sz, c.top + 1, H, stem);
      BlockId leaf = B.dragon_leaves ? B.dragon_leaves : (B.warped_wart_block ? B.warped_wart_block : B.null_stone);
      disc(sx, c.top + 1 + H, sz, 2, leaf);
      if (B.sculk && P.in(sx, sz) && (h & 32)) P.put(sx, c.top, sz, B.sculk);
    }
  // blossoming spires: a pale canopy over a thin trunk, its own cell so it does not move the other features
  const int BC = 20;
  for (int gz = jsFloor((bz - 8) / (double)BC); gz <= jsFloor((bz + 24) / (double)BC); gz++)
    for (int gx = jsFloor((bx - 8) / (double)BC); gx <= jsFloor((bx + 24) / (double)BC); gx++) {
      u32 h = hash32(gx, gz, seed_, 0xB105);
      if ((h & 7) > 3) continue;
      int sx = gx * BC + 3 + (int)(h % 14), sz = gz * BC + 3 + (int)((h >> 5) % 14);
      if (endBiome(sx, sz) != 1) continue;
      Isle c; if (!island(sx, sz, c) || c.main) continue;
      int H = 11 + (int)((h >> 9) % 8);
      BlockId stem = B.purpur_pillar ? B.purpur_pillar : (B.chorus_plant ? B.chorus_plant : B.end_stone);
      trunk(sx, sz, c.top + 1, H, stem);
      BlockId cap = B.blossom_cap ? B.blossom_cap : (B.umbrella_cap ? B.umbrella_cap : B.end_moss);
      int y = c.top + 1 + H;
      disc(sx, y, sz, 4, cap);
      disc(sx, y + 1, sz, 2, cap);
      if (B.shroomlight && P.in(sx, sz) && y < DIM_H - 2) P.put(sx, y, sz, B.shroomlight);
    }
}

int EndGen::fountainY() const { Isle c; return (island(0, 0, c) ? c.top : 64 + END_DY) + 1; }

// a chorus plant: a stem that branches as it climbs, a flower at each tip
static void chorusTree(Mulberry32& rnd, int x, int y, int z, int depth, std::vector<std::array<int, 4>>& out, BlockId plant, BlockId flower) {
  int h = depth == 0 ? 2 + jsFloor(rnd() * 3) : 1 + jsFloor(rnd() * 3);
  for (int i = 0; i < h; i++) out.push_back({x, y + i, z, plant});
  int top = y + h - 1;
  int n = depth >= 3 ? 0 : depth == 0 ? 1 + jsFloor(rnd() * 3) : jsFloor(rnd() * 2.6);
  for (int b = 0; b < n && out.size() < 70; b++) {
    int d = jsFloor(rnd() * 4), dx = DIRX[d], dz = DIRZ[d];
    out.push_back({x + dx, top, z + dz, plant});
    chorusTree(rnd, x + dx, top + 1, z + dz, depth + 1, out, plant, flower);
  }
  if (!n) out.push_back({x, top + 1, z, flower});
}
bool EndGen::chorusAt(int wx, int wz, std::vector<std::array<int, 4>>& out) const {
  if (std::hypot((double)wx, (double)wz) < OUTER + 20 || hashFloat(wx, wz, seed_, 77) > 0.01) return false;
  {
    int eb = endBiome(wx, wz);
    // Groves in the chorus forest, a thinning on the glowing grass, lantern woods and the neon oasis.
    // The other lands grow their own trees.
    bool grove = eb == 0 || eb == 3 || eb == 11 || eb == 13;
    if (!grove) return false;
    if (eb == 3 && hashFloat(wx, wz, seed_, 79) < 0.55) return false;
    if (eb == 11 && hashFloat(wx, wz, seed_, 79) < 0.72) return false;
    if (eb == 13 && hashFloat(wx, wz, seed_, 79) < 0.5) return false;
  }
  Isle c; if (!island(wx, wz, c)) return false;
  static const int D[4][2] = {{3, 0}, {-3, 0}, {0, 3}, {0, -3}};
  for (auto& d : D) { Isle o; if (!island(wx + d[0], wz + d[1], o) || std::abs(o.top - c.top) > 2) return false; }
  Mulberry32 rnd(hash32(wx, wz, seed_, 78));
  chorusTree(rnd, wx, c.top + 1, wz, 0, out, B.chorus_plant, B.chorus_flower);
  return true;
}

void EndGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * 16, bz = ch.cz * 16;
  // Better End's islands are solid wherever its density is above 0 (several islands may stand over each other)
  const bool beChunk = be_ && std::hypot(bx + 8.0, bz + 8.0) > OUTER - 30;
  thread_local std::vector<float> dens;
  if (beChunk) { dens.resize(be::BETerrain::kChunkVol); be_->chunkDensity(ch.cx, ch.cz, dens.data()); beCols(ch.cx, ch.cz, dens.data()); }
  auto beSolid = [&](int lx, int y, int lz) { return y >= 1 && y < DIM_H && dens[(y << 8) | (lz << 4) | lx] > 0; };
  for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
    if (beChunk && std::hypot((double)(bx + lx), (double)(bz + lz)) >= OUTER) {
      for (int y = 1; y < DIM_H; y++) if (beSolid(lx, y, lz)) b.blocks[colIdx(lx, y, lz)] = B.end_stone;
      continue;
    }
    Isle c; if (!island(bx + lx, bz + lz, c)) continue;
    for (int y = std::max(1, c.bot); y <= c.top; y++) b.blocks[colIdx(lx, y, lz)] = B.end_stone;
  }
  StructurePlan S0; S0.seed = (u32)(seed_ ^ 0xE4D);
  ChunkWriter P(b, ch, S0, *this);
  if (std::abs(bx) < 64 && std::abs(bz) < 64) {
    for (const Pillar& pl : pillars_) {
      const int reach = betterEnd() ? 6 : pl.rad;   // (Better End's bases and tops are up to 11 across)
      if (pl.x + reach < bx || pl.x - reach > bx + 15 || pl.z + reach < bz || pl.z - reach > bz + 15) continue;
      if (bePillar(pl, &P, nullptr)) continue;
      Isle c; int base = (island(pl.x, pl.z, c) ? c.top : 60 + END_DY) - 8;
      for (int z = pl.z - pl.rad; z <= pl.z + pl.rad; z++) for (int x = pl.x - pl.rad; x <= pl.x + pl.rad; x++) {
        if ((x - pl.x) * (x - pl.x) + (z - pl.z) * (z - pl.z) > pl.rad * pl.rad + 1) continue;
        for (int y = base; y <= pl.h; y++) P.put(x, y, z, B.obsidian);
      }
      P.put(pl.x, pl.h + 1, pl.z, B.bedrock);
      if (pl.cage) for (int y = pl.h + 1; y <= pl.h + 4; y++) for (int z = pl.z - 2; z <= pl.z + 2; z++) for (int x = pl.x - 2; x <= pl.x + 2; x++) {
        bool edge = std::abs(x - pl.x) == 2 || std::abs(z - pl.z) == 2 || y == pl.h + 4;
        if (edge) P.put(x, y, z, B.iron_bars);
      }
    }
    // the exit portal's bowl, dark until the dragon falls
    const int fy = fountainY();
    if (bx <= 4 && bx + 15 >= -4 && bz <= 4 && bz + 15 >= -4) {
      for (int z = -4; z <= 4; z++) for (int x = -4; x <= 4; x++) {
        double d = std::hypot((double)x, (double)z);
        if (d > 3.6) continue;
        P.put(x, fy - 1, z, B.bedrock);
        if (d > 2.6) P.put(x, fy, z, B.bedrock); else P.put(x, fy, z, AIR);
        for (int y = fy + 1; y < fy + 5; y++) P.put(x, y, z, AIR);
      }
      for (int y = fy; y <= fy + 3; y++) P.put(0, y, 0, B.bedrock);
    }
  }
  // the obsidian platform where the stronghold portal lets you down
  if (PLAT_X + 2 >= bx && PLAT_X - 2 <= bx + 15 && PLAT_Z + 2 >= bz && PLAT_Z - 2 <= bz + 15) {
    P.fill(PLAT_X - 2, PLAT_Y, PLAT_Z - 2, PLAT_X + 2, PLAT_Y, PLAT_Z + 2, B.obsidian);
    P.fill(PLAT_X - 2, PLAT_Y + 1, PLAT_Z - 2, PLAT_X + 2, PLAT_Y + 3, PLAT_Z + 2, AIR);
  }
  // out past the gulf: each land's ground, then its spires, arches and floating shards
  if (std::hypot(bx + 8.0, bz + 8.0) > OUTER - 30) {
    for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
      int x = bx + lx, z = bz + lz; Isle c;
      if (!island(x, z, c) || c.main) continue;
      const int eb = endBiome(x, z);
      if (beChunk) {   // every island standing in the column gets its land's ground, the lower ones too
        for (int y = c.top; y >= std::max(1, c.bot); ) {
          if (!beSolid(lx, y, lz) || beSolid(lx, y + 1, lz)) { y--; continue; }
          int y2 = y; while (y2 > 1 && beSolid(lx, y2 - 1, lz)) y2--;
          beSurface(eb, x, z, Isle{y, y2 - 1, false}, P);
          y = y2 - 1;
        }
        continue;
      }
      beSurface(eb, x, z, c, P);
    }
    if (beChunk) beStructures(bx, bz, P);   // Better End's ruins, houses, stumps and fallen trees in their lands
    outerFeatures(bx, bz, P);
    // each land's plants, last, on the ground the trees left free
    for (int lz = 0; lz < 16; lz++) for (int lx = 0; lx < 16; lx++) {
      int x = bx + lx, z = bz + lz; Isle c;
      if (island(x, z, c) && !c.main) bePlants(endBiome(x, z), x, z, c, P);
    }
  }
  // out on the islands: chorus groves
  if (std::hypot(bx + 8.0, bz + 8.0) > OUTER - 30) {
    std::vector<std::array<int, 4>> tree;
    for (int wz = bz - 6; wz < bz + 22; wz++) for (int wx = bx - 6; wx < bx + 22; wx++) {
      tree.clear(); if (!chorusAt(wx, wz, tree)) continue;
      for (auto& p : tree) if (P.in(p[0], p[2]) && p[1] > 0 && p[1] < DIM_H && !P.get(p[0], p[1], p[2])) P.put(p[0], p[1], p[2], (BlockId)p[3]);
    }
  }
  structures.build(b, ch);
  b.store(ch);
}

TerrainColumn EndGen::terrainColumn(int x, int z) const {
  TerrainColumn t; Isle c;
  if (island(x, z, c)) { t.y = c.top; t.jsY = c.top - END_DY; }
  return t;
}

void EndGen::sample(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample();
  Isle c;
  if (!island(wx, wz, c)) s.height = -1;
  else { s.height = (i16)c.top; s.floatBottom = (i16)std::max(1, c.bot); s.top = s.under = B.end_stone; }
  if (std::abs(wx) < 64 && std::abs(wz) < 64) for (const Pillar& pl : pillars_)
    if ((wx - pl.x) * (wx - pl.x) + (wz - pl.z) * (wz - pl.z) <= pl.rad * pl.rad + 1) {
      int topY;
      if (bePillar(pl, nullptr, &topY)) { s.height = (i16)topY; s.top = s.under = B.obsidian; }
      else { s.height = (i16)(pl.h + 1); s.top = B.bedrock; s.under = B.obsidian; }
      if (s.floatBottom < 0) { Isle pc; s.floatBottom = (i16)((island(pl.x, pl.z, pc) ? pc.top : 60 + END_DY) - 8); }
    }
}

} // namespace dq
