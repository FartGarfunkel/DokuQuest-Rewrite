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
}

static inline double floorBias(double yo) { return (yo < 34 ? (34 - yo) / 18 : 0) + (yo > 80 ? (yo - 80) / 12 : 0); }
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
  return jsLerp(D[i], D[i + 1], t) + floorBias(y / NETHER_K) > 0.18;
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
    for (int y = netherY(84); y > netherY(31); y--) {
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
  const i32 s5 = seed_ ^ 5;
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    float* D = &Dall[(z * 16 + x) * LAYERS];
    float* N = &Nall[(z * 16 + x) * LAYERS];
    for (int L = 0; L < LAYERS; L++) {
      D[L] = (float)(grid(G1, x, z, L) + nCave2.noise3(wx / 16.0, L / 12.0, wz / 16.0) * 0.35);
      N[L] = 2.f;   // computed on demand
    }
    const double reg = nMount.noise2(wx / 90.0, wz / 90.0);
    for (int y = 0; y < DIM_H; y++) {
      BlockId id = AIR;
      const double yo = y / NETHER_K;
      if (y == 0 || y == DIM_H - 1 || (y < 3 && hashFloat(wx, y, wz, seed_) < 0.6) || (y > DIM_H - 4 && hashFloat(wx, y, wz, s5) < 0.6)) id = B.bedrock;
      else {
        int i; double t; rowOf(y, i, t);
        if (jsLerp(D[i], D[i + 1], t) + floorBias(yo) > 0.18) {
          if (N[i] > 1.5f) N[i] = (float)nFloor.noise3(wx / 12.0, i / 12.0, wz / 12.0);
          if (N[i + 1] > 1.5f) N[i + 1] = (float)nFloor.noise3(wx / 12.0, (i + 1) / 12.0, wz / 12.0);
          double n = jsLerp(N[i], N[i + 1], t), rg = jsLerp(grid(GR, x, z, i), grid(GR, x, z, i + 1), t);
          id = n > 0.55 ? B.glowstone : n < -0.6 ? B.obsidian : rg > 0.38 ? B.blackstone : rg < -0.48 ? B.basalt : reg > 0.15 ? B.netherrack : B.emberstone;
          if ((id == B.emberstone || id == B.netherrack) && yo >= LAVA_SEA - 2 && yo <= LAVA_SEA + 2 && hashFloat(wx, y, wz, 81) < 0.35) id = B.magma_block;
          if (id == B.blackstone && hashFloat(wx, y, wz, 82) < 0.012) id = B.gilded_blackstone;
        } else if (yo <= LAVA_SEA) id = B.lava;
      }
      b.blocks[colIdx(x, y, z)] = id;
    }
    // the floors: crimson and warped woods, soul sand valleys, ash elsewhere
    const double wood = nDetail.noise2(wx / 40.0, wz / 40.0), vale = nMount.noise2(wx / 90.0 + 31, wz / 90.0 - 17);
    const int Y90 = netherY(90), Y95 = netherY(95);
    for (int y = DIM_H - 2; y > 1; y--) {
      const int i = colIdx(x, y, z);
      const BlockId here = b.blocks[i], above = b.blocks[colIdx(x, y + 1, z)];
      if (here == AIR && wood > 0.35 && (above == B.netherrack || above == B.emberstone) && hashFloat(wx, y, wz, 79) < 0.06) {
        int len = 2 + jsFloor(hashFloat(wx, y, wz, 80) * 6);
        for (int k = 0; k < len && y - k > 1 && b.blocks[colIdx(x, y - k, z)] == AIR; k++) b.blocks[colIdx(x, y - k, z)] = B.weeping_vines;
        continue;
      }
      if ((here != B.emberstone && here != B.netherrack) || above != AIR) continue;
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
        if (r > 0.97 && y < Y90) { int len = 2 + jsFloor(hashFloat(wx, y, wz, 78) * 7); for (int k = 1; k <= len && b.blocks[colIdx(x, y + k, z)] == AIR; k++) b.blocks[colIdx(x, y + k, z)] = B.twisting_vines; }
        else if (r > 0.72 && y < Y95) b.blocks[colIdx(x, y + 1, z)] = r > 0.93 ? B.warped_fungus : r > 0.82 ? B.warped_roots : B.nether_sprouts;
      }
      else if (r < 0.25) b.blocks[i] = B.ash;
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
  // ores (walked in original rows, each row stretched onto the band rows it covers)
  Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 31));
  for (int v = 0; v < 10; v++) {
    double x = rnd() * 16, y = 10 + rnd() * 80, z = rnd() * 16;
    for (int k = 0; k < 6; k++) {
      int ix = jsFloor(x), iy = jsFloor(y), iz = jsFloor(z);
      if (ix >= 0 && ix < 16 && iz >= 0 && iz < 16 && iy > 0 && iy < 99) {
        for (int yy = (int)std::ceil(iy * NETHER_K); yy < (int)std::ceil((iy + 1) * NETHER_K); yy++) {
          BlockId& cur = b.blocks[colIdx(ix, yy, iz)];
          if (cur == B.emberstone) cur = v < 5 ? B.cinderite_ore : v < 8 ? B.quartz_ore : B.gold_ore;
          else if (cur == B.netherrack) cur = v < 4 ? B.quartz_ore : v < 8 ? B.nether_gold_ore : B.cinderite_ore;
        }
      }
      x += rnd() * 2 - 1; y += rnd() * 2 - 1; z += rnd() * 2 - 1;
    }
  }
  // ancient debris, deep and rare
  if (rnd() < 0.45) {
    int x = jsFloor(rnd() * 16), y = netherY(8 + jsFloor(rnd() * 14)), z = jsFloor(rnd() * 16);
    BlockId& cur = b.blocks[colIdx(x, y, z)];
    if (cur == B.emberstone || cur == B.netherrack || cur == B.basalt) cur = B.ancient_debris;
  }
  structures.build(b, ch);
  // the void crust at the floor (theend.js): break it with a cinderite pick to fall into the End
  if (B.void_crust) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    b.blocks[colIdx(x, 0, z)] = B.void_crust;
    for (int y = 1; y <= 2; y++) if (b.blocks[colIdx(x, y, z)] == B.bedrock) b.blocks[colIdx(x, y, z)] = B.blackstone ? B.blackstone : B.stone;
  }
  b.store(ch);
}

TerrainColumn DepthsGen::terrainColumn(int x, int z) const {
  float D[LAYERS]; densityColumn(x, z, D);
  TerrainColumn t;
  // the cavern floor: the first open space above the lava sea's bed
  int y = 1;
  while (y < DIM_H - 1 && solidFrom(D, y)) y++;
  t.y = y - 1;
  if (t.y < netherY(LAVA_SEA)) t.water = netherY(LAVA_SEA);
  t.jsY = (int)(t.y / NETHER_K);
  return t;
}

void DepthsGen::sample(int wx, int wz, ColumnSample& s) const {
  // cheap: nCave1 exact (no grid), floor found row by row from the lava sea up, the roof in steps of 3 rows
  auto dens = [&](int L) { return nCave1.noise3(wx / 48.0, L / 26.0, wz / 48.0) + nCave2.noise3(wx / 16.0, L / 12.0, wz / 16.0) * 0.35 + floorBias(L); };
  s = ColumnSample();
  int L = 20;
  while (L < 100 && dens(L) > 0.18) L++;
  int floorY = netherY(L) - 1;
  int R = L + 3;
  while (R < 100 && dens(R) <= 0.18) R += 3;
  s.height = (i16)floorY; s.floatBottom = (i16)std::min(DIM_H - 1, netherY(R));
  const double wood = nDetail.noise2(wx / 40.0, wz / 40.0), vale = nMount.noise2(wx / 90.0 + 31, wz / 90.0 - 17), reg = nMount.noise2(wx / 90.0, wz / 90.0);
  BlockId rock = reg > 0.15 ? B.netherrack : B.emberstone;
  s.top = vale < -0.4 && std::fabs(wood) < 0.35 ? B.soul_sand : wood > 0.35 ? B.crimson_nylium : wood < -0.35 ? B.warped_nylium : rock;
  s.under = rock;
  if (L <= LAVA_SEA) { s.water = (i16)netherY(LAVA_SEA); s.fluid = B.lava; }
}

} // namespace dq
