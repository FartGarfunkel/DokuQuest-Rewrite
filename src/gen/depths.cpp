// gen/depths.cpp — the Ember Depths (world/worldgen.js generateDepths, fungusTreesFor, depthsSolid, and the
// void crust of game/theend.js). The original's 100 rows are stretched 2.5x into the band: its noise is
// sampled at the original rows and interpolated between them (nCave1 and the region noise on a 4-block grid).
#include "gen/dimgens.h"

namespace dq {
using namespace vmap;

DepthsGen::DepthsGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed), nCave1(o.seed ^ 0x89ab), nCave2(o.seed ^ 0x9abc), nFloor(o.seed ^ 0xbcde),
    nHill(o.seed ^ 0x2345), nMount(o.seed ^ 0x4567), nDetail(o.seed ^ 0x3456) {
  B.resolve();
  CarveRules& R = hell_;
  R.carvable.assign(MAX_BLOCKS, 0); R.stopper.assign(MAX_BLOCKS, 0);
  for (BlockId id : {B.netherrack, B.emberstone, B.blackstone, B.basalt, B.soul_sand, B.soul_soil, B.magma_block, B.gilded_blackstone, B.obsidian})
    if (id) R.carvable[id] = 1;
  R.lava = 0; R.minY = 6; R.maxY = DIM_H - 8; R.hell = true;
  R.seed = (i64)((u64)(i64)seed_ * 6364136223846793005ULL + 0x4E7E4);
}

static inline void rowOf(int y, int& i, double& t) {
  double yo = y / NETHER_K; i = jsFloor(yo); t = yo - i;
  if (i >= DepthsGen::LAYERS - 1) { i = DepthsGen::LAYERS - 2; t = 1; }
}

void DepthsGen::densityColumn(int wx, int wz, float* D) const {
  int gx = floordiv(wx, 4) * 4, gz = floordiv(wz, 4) * 4;
  double tx = (wx - gx) / 4.0, tz = (wz - gz) / 4.0;
  for (int L = 0; L < LAYERS; L++) {
    float c00 = (float)nCave1.noise3(gx / 48.0, L / 26.0, gz / 48.0), c10 = (float)nCave1.noise3((gx + 4) / 48.0, L / 26.0, gz / 48.0);
    float c01 = (float)nCave1.noise3(gx / 48.0, L / 26.0, (gz + 4) / 48.0), c11 = (float)nCave1.noise3((gx + 4) / 48.0, L / 26.0, (gz + 4) / 48.0);
    double c1 = jsLerp(jsLerp(c00, c10, tx), jsLerp(c01, c11, tx), tz);
    D[L] = (float)(c1 + nCave2.noise3(wx / 16.0, L / 12.0, wz / 16.0) * 0.35);
  }
}
bool DepthsGen::solidFrom(const float* D, int y) {
  if (y <= 0 || y >= DIM_H - 1) return true;
  int i; double t; rowOf(y, i, t);
  return jsLerp(D[i], D[i + 1], t) + bias(y) > 0.18;
}
bool DepthsGen::solidAt(int x, int y, int z) const {
  thread_local int lx = INT32_MIN, lz = INT32_MIN; thread_local const DepthsGen* lg = nullptr; thread_local float D[LAYERS];
  if (lx != x || lz != z || lg != this) { densityColumn(x, z, D); lx = x; lz = z; lg = this; }
  return solidFrom(D, y);
}

void DepthsGen::fungusTrees(int cx, int cz, std::vector<TreeSpec>& out) const {
  Mulberry32 rnd(hash32(seed_, cx, cz, 0xf00));
  float D[LAYERS];
  for (int k = 0; k < 3; k++) {
    int x = cx * 16 + jsFloor(rnd() * 16), z = cz * 16 + jsFloor(rnd() * 16); double h = rnd();
    double wood = nDetail.noise2(x / 40.0, z / 40.0);
    if (std::fabs(wood) < 0.42) continue;
    densityColumn(x, z, D);
    for (int y = ROOF_Y; y > LAVA_Y; y--) {
      if (solidFrom(D, y) && !solidFrom(D, y + 1) && !solidFrom(D, y + 2) && !solidFrom(D, y + 6)) {
        TreeSpec t; t.x = x; t.y = y + 1; t.z = z; t.ySeed = y + 1; t.kind = wood > 0 ? TREE_CRIMSON : TREE_WARPED; t.h = h;
        out.push_back(t); break;
      }
    }
  }
}

void DepthsGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  // the original rows' noise: nCave1 and the rock regions on the 4-block grid, the rest per column
  thread_local std::vector<float> G1, GR, Dall, Nall;
  G1.resize(25 * LAYERS); GR.resize(25 * LAYERS); Dall.resize(256 * LAYERS); Nall.resize(256 * LAYERS);
  for (int iz = 0; iz < 5; iz++) for (int ix = 0; ix < 5; ix++) {
    int x = bx + ix * 4, z = bz + iz * 4;
    for (int L = 0; L < LAYERS; L++) {
      G1[(iz * 5 + ix) * LAYERS + L] = (float)nCave1.noise3(x / 48.0, L / 26.0, z / 48.0);
      GR[(iz * 5 + ix) * LAYERS + L] = (float)nHill.noise3(x / 60.0, L / 40.0, z / 60.0);
    }
  }
  auto grid = [&](const std::vector<float>& G, int x, int z, int L) {
    int ix = x >> 2, iz = z >> 2; double tx = (x & 3) / 4.0, tz = (z & 3) / 4.0;
    float c00 = G[(iz * 5 + ix) * LAYERS + L], c10 = G[(iz * 5 + ix + 1) * LAYERS + L], c01 = G[((iz + 1) * 5 + ix) * LAYERS + L], c11 = G[((iz + 1) * 5 + ix + 1) * LAYERS + L];
    return jsLerp(jsLerp(c00, c10, tx), jsLerp(c01, c11, tx), tz);
  };
  struct Biome { double wood, vale, delta; };
  auto biomeAt = [&](int wx, int wz) {
    return Biome{nDetail.noise2(wx / 40.0, wz / 40.0), nMount.noise2(wx / 90.0 + 31, wz / 90.0 - 17), nMount.noise2(wx / 70.0 - 53, wz / 70.0 + 29)};
  };
  auto isDelta = [](const Biome& bi) { return bi.delta > 0.42 && std::fabs(bi.wood) < 0.35 && bi.vale >= -0.4; };
  const i32 s5 = seed_ ^ 5;
  // 1. the rock: open caverns over a low lava sea (Minecraft's Nether shape), bedrock at the floor and the roof
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    float* D = &Dall[(z * 16 + x) * LAYERS];
    float* N = &Nall[(z * 16 + x) * LAYERS];
    for (int L = 0; L < LAYERS; L++) {
      D[L] = (float)(grid(G1, x, z, L) + nCave2.noise3(wx / 16.0, L / 12.0, wz / 16.0) * 0.35);
      N[L] = 2.f;   // computed on demand
    }
    const double reg = nMount.noise2(wx / 90.0, wz / 90.0);
    const bool delta = isDelta(biomeAt(wx, wz));
    for (int y = 0; y < DIM_H; y++) {
      BlockId id = AIR;
      if (y == 0 || y == DIM_H - 1 || (y < 5 && hashFloat(wx, y, wz, seed_) < 0.75 - y * 0.15) ||
          (y > DIM_H - 6 && hashFloat(wx, y, wz, s5) < 0.75 - (DIM_H - 1 - y) * 0.15)) id = B.bedrock;
      else {
        int i; double t; rowOf(y, i, t);
        if (jsLerp(D[i], D[i + 1], t) + bias(y) > 0.18) {
          if (N[i] > 1.5f) N[i] = (float)nFloor.noise3(wx / 12.0, i / 12.0, wz / 12.0);
          if (N[i + 1] > 1.5f) N[i + 1] = (float)nFloor.noise3(wx / 12.0, (i + 1) / 12.0, wz / 12.0);
          double n = jsLerp(N[i], N[i + 1], t), rg = jsLerp(grid(GR, x, z, i), grid(GR, x, z, i + 1), t);
          if (delta) id = n < -0.75 ? B.obsidian : n > 0.1 ? B.basalt : B.blackstone;
          else id = n < -0.75 ? B.obsidian : rg > 0.55 ? B.blackstone : rg < -0.62 ? B.basalt : reg > -0.35 ? B.netherrack : B.emberstone;
          if ((id == B.emberstone || id == B.netherrack || id == B.blackstone) && y >= LAVA_Y - 4 && y <= LAVA_Y + 3 && hashFloat(wx, y, wz, 81) < 0.35) id = B.magma_block;
          if (id == B.blackstone && hashFloat(wx, y, wz, 82) < 0.012) id = B.gilded_blackstone;
        } else if (y <= LAVA_Y) id = B.lava;
      }
      b.blocks[colIdx(x, y, z)] = id;
      b.meta[colIdx(x, y, z)] = 0;
    }
  }
  // 2. Minecraft's Nether tunnels through the rock (MapGenCavesHell)
  carveCaves(b, ch.cx, ch.cz, hell_);
  // 3. the floors and roofs: crimson and warped woods, soul sand valleys, basalt deltas, the wastes' soul sand and
  //    gravel round the lava sea; vines hang from roofs as whole strands (a tip under a run of body)
  const int Y90 = netherY(90), Y95 = netherY(95);
  auto rockish = [&](BlockId id) { return id == B.netherrack || id == B.emberstone || id == B.blackstone || id == B.basalt; };
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const Biome bi = biomeAt(wx, wz);
    const double wood = bi.wood, vale = bi.vale;
    const bool delta = isDelta(bi);
    const double patch = nFloor.noise3(wx / 14.0, 7.5, wz / 14.0);
    for (int y = DIM_H - 7; y > 5; y--) {
      const int i = colIdx(x, y, z);
      const BlockId here = b.blocks[i], above = b.blocks[colIdx(x, y + 1, z)];
      if (here == AIR && wood > 0.35 && rockish(above) && hashFloat(wx, y, wz, 79) < 0.06) {
        int len = 2 + jsFloor(hashFloat(wx, y, wz, 80) * 7);
        if (hashFloat(wx, y, wz, 83) < 0.15) len += 6 + jsFloor(hashFloat(wx, y, wz, 84) * 10);
        int k = 0;
        while (k < len && y - k > LAVA_Y + 1 && b.blocks[colIdx(x, y - k, z)] == AIR) b.blocks[colIdx(x, y - k++, z)] = B.weeping_vines;
        if (above != B.netherrack && above != B.emberstone) b.blocks[colIdx(x, y + 1, z)] = B.nether_wart_block;
        y -= k;
        continue;
      }
      if (!rockish(here) || above != AIR || y <= LAVA_Y - 1) continue;
      const double r = hashFloat(wx, y, wz, 77);
      if (vale < -0.4 && std::fabs(wood) < 0.35) {
        b.blocks[i] = r < 0.55 ? B.soul_sand : B.soul_soil;
        if (r > 0.995 && y < Y90) for (int k = 1; k <= 3; k++) b.blocks[colIdx(x, y + k, z)] = B.bone_block;
        else if (r > 0.97 && y < Y95) b.blocks[colIdx(x, y + 1, z)] = B.soul_fire;
        continue;
      }
      if (wood > 0.35) { b.blocks[i] = B.crimson_nylium; if (r > 0.75 && y < Y95) b.blocks[colIdx(x, y + 1, z)] = r > 0.93 ? B.crimson_fungus : B.crimson_roots; }
      else if (wood < -0.35) {
        b.blocks[i] = B.warped_nylium;
        if (r > 0.97 && y < Y90) {
          int len = 2 + jsFloor(hashFloat(wx, y, wz, 78) * 7);
          if (hashFloat(wx, y, wz, 85) < 0.2) len += 5 + jsFloor(hashFloat(wx, y, wz, 86) * 9);
          for (int k = 1; k <= len && y + k < DIM_H - 6 && b.blocks[colIdx(x, y + k, z)] == AIR; k++) b.blocks[colIdx(x, y + k, z)] = B.twisting_vines;
        }
        else if (r > 0.72 && y < Y95) b.blocks[colIdx(x, y + 1, z)] = r > 0.93 ? B.warped_fungus : r > 0.82 ? B.warped_roots : B.nether_sprouts;
      }
      else if (delta) {
        b.blocks[i] = r < 0.12 ? B.magma_block : r < 0.55 ? B.basalt : B.blackstone;
        if (r > 0.975 && y < Y90) { int len = 1 + jsFloor(hashFloat(wx, y, wz, 87) * 6); for (int k = 1; k <= len && b.blocks[colIdx(x, y + k, z)] == AIR; k++) b.blocks[colIdx(x, y + k, z)] = B.basalt; }
      }
      else if (y <= LAVA_Y + 5) b.blocks[i] = patch > 0 ? B.soul_sand : B.gravel;      // the lava sea's shores
      else if (patch > 0.55) b.blocks[i] = B.soul_sand;
      else if (patch < -0.62) b.blocks[i] = B.gravel;
      else if (r < 0.06) b.blocks[i] = B.ash;
    }
  }
  // huge fungi, worked out per chunk so the ones on a border appear whole on both sides
  std::vector<TreeSpec> trees; std::vector<TreePart> parts;
  for (int ncz = ch.cz - 1; ncz <= ch.cz + 1; ncz++) for (int ncx = ch.cx - 1; ncx <= ch.cx + 1; ncx++) {
    trees.clear(); fungusTrees(ncx, ncz, trees);
    for (const TreeSpec& t : trees) {
      parts.clear(); treeParts(B, seed_, t, parts);
      for (const TreePart& p : parts) {
        int lx = (int)p.x - bx, lz = (int)p.z - bz, py = (int)p.y;
        if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || py >= DIM_H - 9 || py < 1) continue;
        BlockId& cur = b.blocks[colIdx(lx, py, lz)];
        if (cur == AIR) cur = p.id;
      }
    }
  }
  glowstone(b, ch.cx, ch.cz);
  ores(b, ch.cx, ch.cz);
  structures.build(b, ch);
  // the void crust at the floor (theend.js): break it with a cinderite pick to fall into the End
  if (B.void_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) b.blocks[colIdx(x, 0, z)] = B.void_crust;
  b.store(ch);
}

// WorldGenGlowStone1: clusters grown down from the roof, each block touching exactly one other
void DepthsGen::glowstone(GenBuf& b, int cx, int cz) const {
  JRandom rnd(chunkSeed(seed_, cx, cz, 0x6105));
  const int n = 5 + rnd.nextInt(6);
  for (int k = 0; k < n; k++) {
    const int x = 3 + rnd.nextInt(10), z = 3 + rnd.nextInt(10);
    int y = FLOOR_Y + rnd.nextInt(ROOF_Y + 30 - FLOOR_Y);
    if (b.blocks[colIdx(x, y, z)] != AIR) continue;
    int up = 0;
    while (up < 24 && y + 1 < DIM_H - 2 && b.blocks[colIdx(x, y + 1, z)] == AIR) { y++; up++; }
    const BlockId roof = b.blocks[colIdx(x, y + 1, z)];
    if (roof != B.netherrack && roof != B.emberstone && roof != B.blackstone && roof != B.basalt) continue;
    b.blocks[colIdx(x, y, z)] = B.glowstone;
    const int tries = 300 + rnd.nextInt(300);
    for (int t = 0; t < tries; t++) {
      const int ax0 = rnd.nextInt(4), gx = x + ax0 - rnd.nextInt(4), gy = y - rnd.nextInt(10), az0 = rnd.nextInt(4), gz = z + az0 - rnd.nextInt(4);
      if (gx < 0 || gx > 15 || gz < 0 || gz > 15 || gy < 2) continue;
      if (b.blocks[colIdx(gx, gy, gz)] != AIR) continue;
      int touch = 0;
      for (int d = 0; d < 6; d++) {
        static const int O[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int ax = gx + O[d][0], ay = gy + O[d][1], az = gz + O[d][2];
        if (ax < 0 || ax > 15 || az < 0 || az > 15) continue;
        if (b.blocks[colIdx(ax, ay, az)] == B.glowstone) touch++;
      }
      if (touch == 1) b.blocks[colIdx(gx, gy, gz)] = B.glowstone;
    }
  }
}

// ore veins (Minecraft 1.16 rates, the original's ores in its own rock), seeded per source chunk over the 3x3
void DepthsGen::ores(GenBuf& b, int cx, int cz) const {
  static thread_local std::vector<u8> rack, ember, deep;
  if (rack.empty()) {
    rack.assign(MAX_BLOCKS, 0); ember.assign(MAX_BLOCKS, 0); deep.assign(MAX_BLOCKS, 0);
    rack[B.netherrack] = 1; ember[B.emberstone] = 1;
    for (BlockId id : {B.netherrack, B.emberstone, B.basalt, B.blackstone}) deep[id] = 1;
  }
  for (int ncz = cz - 1; ncz <= cz + 1; ncz++) for (int ncx = cx - 1; ncx <= cx + 1; ncx++) {
    JRandom rnd(chunkSeed(seed_, ncx, ncz, 0x0E6));
    auto veins = [&](int n, int size, int lo, int hi, BlockId id, const std::vector<u8>& rep) {
      for (int k = 0; k < n; k++) {
        const int x = ncx * 16 + rnd.nextInt(16), y = lo + rnd.nextInt(hi - lo), z = ncz * 16 + rnd.nextInt(16);
        oreVein(b, cx, cz, rnd, x, y, z, size, id, rep.data());
      }
    };
    veins(16, 13, 10, DIM_H - 20, B.quartz_ore, rack);
    veins(10, 10, 10, DIM_H - 20, B.nether_gold_ore, rack);
    veins(8, 9, 10, DIM_H - 20, B.cinderite_ore, ember);
    veins(4, 8, 10, DIM_H - 20, B.quartz_ore, ember);
    veins(2, 7, 10, DIM_H - 20, B.gold_ore, ember);
    veins(1, 3, 8, 40, B.ancient_debris, deep);       // ancient debris: deep, under the lava sea
    if (rnd.nextInt(2) == 0) veins(1, 2, 8, DIM_H - 40, B.ancient_debris, deep);
  }
}

TerrainColumn DepthsGen::terrainColumn(int x, int z) const {
  float D[LAYERS]; densityColumn(x, z, D);
  TerrainColumn t;
  // the cavern floor: the first open space above the lava sea's bed
  int y = 1;
  while (y < DIM_H - 1 && solidFrom(D, y)) y++;
  t.y = y - 1;
  if (t.y < LAVA_Y) t.water = LAVA_Y;
  t.jsY = (int)(t.y / NETHER_K);
  return t;
}

void DepthsGen::sample(int wx, int wz, ColumnSample& s) const {
  // cheap: nCave1 exact (no grid), floor found row by row from the lava sea up, the roof in steps of 3 rows
  auto dens = [&](int L) { return nCave1.noise3(wx / 48.0, L / 26.0, wz / 48.0) + nCave2.noise3(wx / 16.0, L / 12.0, wz / 16.0) * 0.35 + bias(netherY(L)); };
  s = ColumnSample();
  int L = 12;
  while (L < 100 && dens(L) > 0.18) L++;
  int floorY = netherY(L) - 1;
  int R = L + 3;
  while (R < 100 && dens(R) <= 0.18) R += 3;
  s.height = (i16)floorY; s.floatBottom = (i16)std::min(DIM_H - 1, netherY(R));
  const double wood = nDetail.noise2(wx / 40.0, wz / 40.0), vale = nMount.noise2(wx / 90.0 + 31, wz / 90.0 - 17), reg = nMount.noise2(wx / 90.0, wz / 90.0);
  BlockId rock = reg > -0.35 ? B.netherrack : B.emberstone;
  s.top = vale < -0.4 && std::fabs(wood) < 0.35 ? B.soul_sand : wood > 0.35 ? B.crimson_nylium : wood < -0.35 ? B.warped_nylium : rock;
  s.under = rock;
  if (netherY(L) <= LAVA_Y) { s.water = (i16)LAVA_Y; s.fluid = B.lava; }
}

} // namespace dq
