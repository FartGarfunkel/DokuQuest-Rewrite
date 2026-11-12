// gen/skylands.cpp — the Skylands (world/worldgen.js generateSkylands): floating islands of skystone with sky
// grass, tall grass and flowers, aether crystal ore and cloud-pine trees. Shapes are the original's, lifted
// SKY_DY (120) so they float high in the band; the band's bottom 30 is the cloud bank (vmap::cloudBank).
#include "gen/dimgens.h"
#include "gen/aether_noise.h"

namespace dq {
using namespace vmap;

SkylandsGen::SkylandsGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed), nCont(o.seed ^ 0x1234), nHill(o.seed ^ 0x2345), nCave1(o.seed ^ 0x89ab) {
  B.resolve();
  if (o.genVersion >= 7) aether_ = std::make_shared<AetherNoise>((u64)(u32)o.seed * 0x9E3779B97F4A7C15ULL + 0xAE);
}

int SkylandsGen::islandTop(int wx, int wz) const {
  if (aether_) { int t, b; return aetherColumn(wx, wz, t, b) ? t : -1; }
  double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
  if (mask <= 0.05) return -1;
  // the top row is always solid where the mask allows (0.6 + n > 0 for |n| <= 0.4)
  return 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10) + SKY_DY;
}

double SkylandsGen::islandMask(int wx, int wz) const {
  if (aether_) { int t, b; return aetherColumn(wx, wz, t, b) ? 0.3 : -1.0; }
  return nCont.fbm2(wx / 180.0, wz / 180.0, 3);
}
// how far an island reaches down under a column: deep in the middle, so each hangs to a point like an upturned hill
double SkylandsGen::underside(double mask) const { return mask <= 0.05 ? 0 : std::pow(mask - 0.05, 1.35) * 105; }
// Meadow, forest, grove, woodland. Large regions, so a flight crosses one and then the next.
static int skyBiomeOf(const SimplexNoise& nHill, const SimplexNoise& nCont, int wx, int wz) {
  double a = nHill.noise2(wx / 280.0 + 3.1, wz / 280.0 - 8.4);
  double b = nCont.noise2(wx / 210.0 - 20.0, wz / 210.0 + 14.0);
  if (a > 0.22) return 1;
  if (a < -0.22) return 0;
  if (b > 0.12) return 3;
  return 2;
}
int SkylandsGen::skyBiome(int wx, int wz) const { return skyBiomeOf(nHill, nCont, wx, wz); }
int skyLandAt(i32 seed, int x, int z) {
  SimplexNoise nHill(seed ^ 0x2345), nCont(seed ^ 0x1234);
  return skyBiomeOf(nHill, nCont, x, z);
}
bool SkylandsGen::sunwoodAt(int x, int z) const {
  int b = skyBiome(x, z);
  float p = b == 3 ? 0.55f : b == 1 ? 0.16f : b == 2 ? 0.1f : 0.03f;   // woodland is mostly sunwood, meadows almost never
  return B.sunwood_log && hashFloat(x, z, seed_, 47) < p;
}
int SkylandsGen::hillTop(int wx, int wz) const {
  if (aether_) { int t = islandTop(wx, wz); return t >= 0 ? t : 64 + AETHER_DY; }
  return 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10) + SKY_DY;
}

void SkylandsGen::generate(Chunk& ch) {
  if (aether_) { generateAether(ch); return; }
  GenBuf& b = GenBuf::local();
  for (int i = 0; i < 256; i++) { ch.climate[i * 2] = 160; ch.climate[i * 2 + 1] = 190; }
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  const BlockId mass = B.holystone ? B.holystone : B.skystone;
  const BlockId slick = B.quicksoil ? B.quicksoil : B.glimmersand;
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
    if (mask <= 0.05) continue;
    const int topY = 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10);
    const double thick = underside(mask);
    int firstTop = -1;
    const int lowest = std::max(21, topY - (int)std::ceil(thick * 1.0) - 1);
    for (int y = std::min(110, topY); y >= lowest; y--) {
      const int dist = topY - y;
      const double n = nCave1.noise3(wx / 30.0, y / 20.0, wz / 30.0) * 0.4;
      if (dist >= 0 && dist < thick * (0.6 + n)) {
        BlockId id = mass;
        if (firstTop < 0) { firstTop = y; id = mask < 0.1 && slick ? slick : B.sky_grass; }   // quicksoil (or glimmersand) round the rims
        else if (firstTop - y < 3) id = B.dirt;
        b.blocks[colIdx(x, y + SKY_DY, z)] = id;
      }
    }
    if (firstTop > 0 && b.blocks[colIdx(x, firstTop + SKY_DY, z)] == B.sky_grass) {
      const int biome = skyBiome(wx, wz);
      const double r = hashFloat(wx, wz, seed_, 41);
      const int fy = firstTop + 1 + SKY_DY;
      // Meadows are open grass and flowers. Forests keep a few. Groves and woodland sit between.
      const double grass = biome == 0 ? 0.42 : biome == 2 ? 0.22 : biome == 3 ? 0.16 : 0.08;
      const double bloom = grass + (biome == 0 ? 0.1 : 0.04);
      if (r < grass) b.blocks[colIdx(x, fy, z)] = B.tall_grass;
      else if (r < bloom && B.sky_bloom) b.blocks[colIdx(x, fy, z)] = B.sky_bloom;
      else if (r < bloom + 0.03) b.blocks[colIdx(x, fy, z)] = B.flower_yellow;
      else if (r < bloom + 0.05 && B.bluebells) b.blocks[colIdx(x, fy, z)] = B.bluebells;
      else if (r < bloom + 0.07 && B.marigold) b.blocks[colIdx(x, fy, z)] = B.marigold;
      else if (biome == 0 && r < bloom + 0.09 && B.azure_bluet) b.blocks[colIdx(x, fy, z)] = B.azure_bluet;
      else if (biome == 0 && r > 0.97 && B.berry_bush) b.blocks[colIdx(x, fy, z)] = B.berry_bush;
    }
  }
  // quicksoil shelves: a lip of glimmersand sticking out past the grass, the way those shelves overhang an island
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
    if (mask <= 0.0 || mask > 0.05 || !slick) continue;
    const int topY = 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10);
    for (int k = 0; k < 2; k++) {
      const int y = topY - k + SKY_DY;
      if (y <= 0 || y >= DIM_H) continue;
      BlockId& cur = b.blocks[colIdx(x, y, z)];
      if (cur == AIR) cur = slick;
    }
  }
  // islets drifting high over the big islands, and crystal islets among them
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const double m2 = nHill.fbm2(wx / 46.0 + 700.5, wz / 46.0 - 300.5, 2);
    if (m2 > 0.5) {
      const int top = 96 + jsFloor(nCont.noise2(wx / 50.0, wz / 50.0) * 6), depth = jsFloor((m2 - 0.5) * 34);
      for (int k = 0; k <= depth && top - k > 0; k++) b.blocks[colIdx(x, top - k + SKY_DY, z)] = k == 0 ? B.sky_grass : k < 2 ? B.dirt : mass;
      if (hashFloat(wx, wz, seed_, 45) < 0.05 && B.sky_bloom) b.blocks[colIdx(x, top + 1 + SKY_DY, z)] = B.sky_bloom;
    }
    // crystal islets: small calcite and amethyst islands, higher than the grass ones
    const double m3 = nCave1.fbm2(wx / 40.0 + 120.0, wz / 40.0 - 80.0, 2);
    if (m3 > 0.62 && B.amethyst_block) {
      const int top = 108 + jsFloor(nCont.noise2(wx / 44.0 + 9.0, wz / 44.0 - 4.0) * 4);
      const int depth = 2 + jsFloor((m3 - 0.62) * 26);
      for (int k = 0; k <= depth && top - k > 0; k++) {
        const int y = top - k + SKY_DY;
        if (y <= 0 || y >= DIM_H) continue;
        BlockId& cur = b.blocks[colIdx(x, y, z)];
        if (cur != AIR) continue;
        cur = k == 0 ? (B.calcite ? B.calcite : B.amethyst_block) : k < 3 ? B.amethyst_block : mass;
      }
      if (hashFloat(wx, wz, seed_, 93) < 0.22) {
        const int y = top + 1 + SKY_DY;
        if (y > 0 && y < DIM_H && b.blocks[colIdx(x, y, z)] == AIR) {
          const float bud = hashFloat(wx, wz, seed_, 94);
          BlockId tip = bud < 0.25 && B.amethyst_cluster ? B.amethyst_cluster : bud < 0.55 && B.large_amethyst_bud ? B.large_amethyst_bud : B.medium_amethyst_bud ? B.medium_amethyst_bud : B.small_amethyst_bud;
          if (tip) b.blocks[colIdx(x, y, z)] = tip;
        }
      }
    }
    // (the three cloud banks are gone: only the islands are land in the sky)
  }
  // meadow and grove ponds: one block of water in a dip of the grass
  if (B.water) for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const int biome = skyBiome(wx, wz);
    if (biome != 0 && biome != 2) continue;
    if (nCont.fbm2(wx / 180.0, wz / 180.0, 3) < 0.28) continue;
    if (nCave1.noise2(wx / 22.0 + 9.0, wz / 22.0 - 3.0) > -0.58) continue;
    for (int yy = 110 + SKY_DY; yy > 40 + SKY_DY; yy--) if (b.blocks[colIdx(x, yy, z)] == B.sky_grass) {
      b.blocks[colIdx(x, yy, z)] = B.water;
      break;
    }
  }
  Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 43));
  for (int v = 0; v < 4; v++) {
    double x = rnd() * 16, y = 40 + rnd() * 60, z = rnd() * 16;
    for (int k = 0; k < 5; k++) {
      int ix = jsFloor(x), iy = jsFloor(y), iz = jsFloor(z);
      if (ix >= 0 && ix < 16 && iz >= 0 && iz < 16) { BlockId& cur = b.blocks[colIdx(ix, iy + SKY_DY, iz)]; if (cur == B.skystone || (B.holystone && cur == B.holystone)) cur = B.aether_crystal_ore; }
      x += rnd() * 2 - 1; y += rnd() * 2 - 1; z += rnd() * 2 - 1;
    }
  }
  // driftstone in the islands' undersides (it falls up when freed: sim/blockfx.cpp)
  if (B.driftstone) for (int v = 0; v < 2; v++) {
    double x = rnd() * 16, y = 44 + rnd() * 40, z = rnd() * 16;
    for (int k = 0; k < 6; k++) {
      int ix = jsFloor(x), iy = jsFloor(y), iz = jsFloor(z);
      if (ix >= 0 && ix < 16 && iz >= 0 && iz < 16 && iy > 0) {
        BlockId& cur = b.blocks[colIdx(ix, iy + SKY_DY, iz)];
        const bool rock = cur == B.skystone || (B.holystone && cur == B.holystone);
        if (rock && b.blocks[colIdx(ix, iy + SKY_DY - 1, iz)] == AIR) cur = B.driftstone;
        else if (rock && k > 2) cur = B.driftstone;
      }
      x += rnd() * 2 - 1; y += rnd() * 1.4 - 0.7; z += rnd() * 2 - 1;
    }
  }
  // ambrosium, zanite and gravitite in the island stone. Hash only, so the tree rolls below stay put.
  if (B.ambrosium_ore || B.zanite_ore || B.gravitite_ore) {
    for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
      const int wx = bx + x, wz = bz + z;
      for (int y = 44; y < 110; y += 3) {
        const int yy = y + SKY_DY;
        if (yy <= 0 || yy >= DIM_H) continue;
        BlockId& cur = b.blocks[colIdx(x, yy, z)];
        if (cur != mass && cur != B.skystone) continue;
        const float h = hashFloat(wx, y, wz, seed_ ^ 0xA3E);
        if (h < 0.035f && B.gravitite_ore && y < 72) cur = B.gravitite_ore;
        else if (h < 0.08f && B.zanite_ore) cur = B.zanite_ore;
        else if (h < 0.15f && B.ambrosium_ore) cur = B.ambrosium_ore;
      }
    }
  }
  // cloud-pine, skyroot and golden oak. Meadows stay open, forests and woodland grow several.
  auto grow = [&](int x, int z, int H, bool sun) {
    int y = -1;
    for (int yy = 120 + SKY_DY; yy > 20 + SKY_DY; yy--) if (b.blocks[colIdx(x, yy, z)] == B.sky_grass) { y = yy + 1; break; }
    if (y < 0) return;
    const int biome = skyBiome(bx + x, bz + z);
    const bool gold = !sun && B.golden_oak_log && B.golden_oak_leaves && hashFloat(bx + x, bz + z, seed_, 77) < (biome == 3 ? 0.4f : 0.08f);
    const BlockId log = sun ? B.sunwood_log : gold ? B.golden_oak_log : (B.skyroot_log ? B.skyroot_log : B.birch_log);
    const BlockId leaf = sun ? B.sunwood_leaves : gold ? B.golden_oak_leaves : (B.skyroot_leaves ? B.skyroot_leaves : B.cloud_leaves);
    for (int k = 0; k < H; k++) if (y + k < DIM_H) b.blocks[colIdx(x, y + k, z)] = log;
    for (int ly = H - 2; ly <= H + 1; ly++) {
      int r = ly >= H ? 1 : 2;
      for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int xx = x + dx, zz = z + dz;
        if (xx < 0 || xx > 15 || zz < 0 || zz > 15 || y + ly >= DIM_H) continue;
        if (std::abs(dx) == r && std::abs(dz) == r) continue;
        BlockId& cur = b.blocks[colIdx(xx, y + ly, zz)];
        if (cur == AIR) cur = leaf;
      }
    }
  };
  Mulberry32 tr(hash32(seed_, ch.cx, ch.cz, 44));
  int nt = tr() < 0.6 ? 1 + jsFloor(tr() * 2) : 0;
  for (int i = 0; i < nt; i++) {
    int x = 2 + jsFloor(tr() * 12), z = 2 + jsFloor(tr() * 12);
    int biome = skyBiome(bx + x, bz + z);
    int H = 4 + jsFloor(tr() * 3) + (biome == 1 ? 2 : 0);
    if (biome == 0 && i > 0) continue;
    grow(x, z, H, sunwoodAt(bx + x, bz + z));
  }
  Mulberry32 ex(hash32(seed_, ch.cx, ch.cz, 144));
  int mid = skyBiome(bx + 8, bz + 8);
  int extra = mid == 1 ? 2 : mid == 3 ? 1 : 0;
  for (int i = 0; i < extra; i++) {
    int x = 2 + jsFloor(ex() * 12), z = 2 + jsFloor(ex() * 12);
    int biome = skyBiome(bx + x, bz + z);
    int H = 5 + jsFloor(ex() * 4);
    if (biome != 1 && biome != 3) continue;
    grow(x, z, H, sunwoodAt(bx + x, bz + z));
  }
  structures.build(b, ch);
  b.store(ch);
}

TerrainColumn SkylandsGen::terrainColumn(int x, int z) const {
  TerrainColumn t; t.y = islandTop(x, z); t.jsY = t.y < 0 ? -1 : t.y - (aether_ ? AETHER_DY : SKY_DY); t.temp = 160 / 127.0 - 128 / 127.0; t.humid = 190 / 127.0 - 128 / 127.0;
  return t;
}

void SkylandsGen::sample(int wx, int wz, ColumnSample& s) const {
  if (aether_) { sampleAether(wx, wz, s); return; }
  s = ColumnSample(); s.temp = 160; s.humid = 190;
  const double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
  if (mask <= 0.05) { s.height = -1; return; }
  const int topY = 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10);
  const double thick = underside(mask), n = nCave1.noise3(wx / 30.0, (topY - thick * 0.5) / 20.0, wz / 30.0) * 0.4;
  s.height = (i16)(topY + SKY_DY); s.top = B.sky_grass; s.under = B.holystone ? B.holystone : (B.skystone ? B.skystone : B.dirt);
  s.floatBottom = (i16)(std::max(21, topY - std::max(0, (int)std::ceil(thick * (0.6 + n)) - 1)) + SKY_DY);
}

void SkylandsGen::trees(int cx, int cz, std::vector<TreeSample>& out) const {
  if (aether_) { treesAether(cx, cz, out); return; }
  auto add = [&](int x, int z, int H, bool sun) {
    int top = islandTop(x, z);
    if (top < 0) return;
    const bool gold = !sun && B.golden_oak_log && B.golden_oak_leaves && hashFloat(x, z, seed_, 77) < (skyBiome(x, z) == 3 ? 0.4f : 0.08f);
    const BlockId log = sun ? B.sunwood_log : gold ? B.golden_oak_log : (B.skyroot_log ? B.skyroot_log : B.birch_log);
    const BlockId leaf = sun ? B.sunwood_leaves : gold ? B.golden_oak_leaves : (B.skyroot_leaves ? B.skyroot_leaves : B.cloud_leaves);
    TreeSample t{x, top + 1, z, TREE_BIRCH, false, {}};
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
      bool inner = std::abs(dx) <= 1 && std::abs(dz) <= 1 && !(std::abs(dx) == 1 && std::abs(dz) == 1);
      t.cols.push_back({(i8)dx, (i8)dz, (i16)(top + 1 + (inner ? H + 2 : H)), (i16)(top + 1 + H - 2), leaf, (BlockId)(dx == 0 && dz == 0 ? log : 0)});
    }
    out.push_back(std::move(t));
  };
  Mulberry32 tr(hash32(seed_, cx, cz, 44));
  int nt = tr() < 0.6 ? 1 + jsFloor(tr() * 2) : 0;
  for (int i = 0; i < nt; i++) {
    int x = cx * 16 + 2 + jsFloor(tr() * 12), z = cz * 16 + 2 + jsFloor(tr() * 12);
    int biome = skyBiome(x, z);
    int H = 4 + jsFloor(tr() * 3) + (biome == 1 ? 2 : 0);
    if (biome == 0 && i > 0) continue;
    add(x, z, H, sunwoodAt(x, z));
  }
  Mulberry32 ex(hash32(seed_, cx, cz, 144));
  int mid = skyBiome(cx * 16 + 8, cz * 16 + 8);
  int extra = mid == 1 ? 2 : mid == 3 ? 1 : 0;
  for (int i = 0; i < extra; i++) {
    int x = cx * 16 + 2 + jsFloor(ex() * 12), z = cz * 16 + 2 + jsFloor(ex() * 12);
    int biome = skyBiome(x, z);
    int H = 5 + jsFloor(ex() * 4);
    if (biome != 1 && biome != 3) continue;
    add(x, z, H, sunwoodAt(x, z));
  }
}

} // namespace dq
