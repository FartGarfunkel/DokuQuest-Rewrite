// gen/skylands.cpp — the Skylands (world/worldgen.js generateSkylands): floating islands of skystone with sky
// grass, tall grass and flowers, aether crystal ore and cloud-pine trees. Shapes are the original's, lifted
// SKY_DY (120) so they float high in the band; the band's bottom 30 is the cloud bank (vmap::cloudBank).
#include "gen/dimgens.h"

namespace dq {
using namespace vmap;

SkylandsGen::SkylandsGen(const WorldGenOptions& o)
  : structures(*this), seed_(o.seed), nCont(o.seed ^ 0x1234), nHill(o.seed ^ 0x2345), nCave1(o.seed ^ 0x89ab) {
  B.resolve();
}

int SkylandsGen::islandTop(int wx, int wz) const {
  double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
  if (mask <= 0.05) return -1;
  // the top row is always solid where the mask allows (0.6 + n > 0 for |n| <= 0.4)
  return 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10) + SKY_DY;
}

void SkylandsGen::generate(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  for (int i = 0; i < 256; i++) { ch.climate[i * 2] = 160; ch.climate[i * 2 + 1] = 190; }
  const int bx = ch.cx * CS, bz = ch.cz * CS;
  for (int z = 0; z < CS; z++) for (int x = 0; x < CS; x++) {
    const int wx = bx + x, wz = bz + z;
    const double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
    if (mask <= 0.05) continue;
    const int topY = 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10);
    const double thick = (mask - 0.05) * 60;
    int firstTop = -1;
    const int lowest = std::max(21, topY - (int)std::ceil(thick * 1.0) - 1);
    for (int y = std::min(110, topY); y >= lowest; y--) {
      const int dist = topY - y;
      const double n = nCave1.noise3(wx / 30.0, y / 20.0, wz / 30.0) * 0.4;
      if (dist >= 0 && dist < thick * (0.6 + n)) {
        BlockId id = B.skystone;
        if (firstTop < 0) { firstTop = y; id = B.sky_grass; }
        else if (firstTop - y < 3) id = B.dirt;
        b.blocks[colIdx(x, y + SKY_DY, z)] = id;
      }
    }
    if (firstTop > 0) {
      const double r = hashFloat(wx, wz, seed_, 41);
      if (r < 0.12) b.blocks[colIdx(x, firstTop + 1 + SKY_DY, z)] = B.tall_grass;
      else if (r < 0.14) b.blocks[colIdx(x, firstTop + 1 + SKY_DY, z)] = B.flower_yellow;
    }
  }
  Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 43));
  for (int v = 0; v < 4; v++) {
    double x = rnd() * 16, y = 40 + rnd() * 60, z = rnd() * 16;
    for (int k = 0; k < 5; k++) {
      int ix = jsFloor(x), iy = jsFloor(y), iz = jsFloor(z);
      if (ix >= 0 && ix < 16 && iz >= 0 && iz < 16) { BlockId& cur = b.blocks[colIdx(ix, iy + SKY_DY, iz)]; if (cur == B.skystone) cur = B.aether_crystal_ore; }
      x += rnd() * 2 - 1; y += rnd() * 2 - 1; z += rnd() * 2 - 1;
    }
  }
  // a few cloud-pine trees
  Mulberry32 tr(hash32(seed_, ch.cx, ch.cz, 44));
  int nt = tr() < 0.6 ? 1 + jsFloor(tr() * 2) : 0;
  for (int i = 0; i < nt; i++) {
    int x = 2 + jsFloor(tr() * 12), z = 2 + jsFloor(tr() * 12), y = -1;
    for (int yy = 110 + SKY_DY; yy > 20 + SKY_DY; yy--) if (b.blocks[colIdx(x, yy, z)] == B.sky_grass) { y = yy + 1; break; }
    if (y < 0) continue;
    int H = 4 + jsFloor(tr() * 3);
    for (int k = 0; k < H; k++) b.blocks[colIdx(x, y + k, z)] = B.birch_log;
    for (int ly = H - 2; ly <= H + 1; ly++) {
      int r = ly >= H ? 1 : 2;
      for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        int xx = x + dx, zz = z + dz;
        if (xx < 0 || xx > 15 || zz < 0 || zz > 15) continue;
        if (std::abs(dx) == r && std::abs(dz) == r) continue;
        BlockId& cur = b.blocks[colIdx(xx, y + ly, zz)];
        if (cur == AIR) cur = B.cloud_leaves;
      }
    }
  }
  structures.build(b, ch);
  b.store(ch);
}

TerrainColumn SkylandsGen::terrainColumn(int x, int z) const {
  TerrainColumn t; t.y = islandTop(x, z); t.jsY = t.y < 0 ? -1 : t.y - SKY_DY; t.temp = 160 / 127.0 - 128 / 127.0; t.humid = 190 / 127.0 - 128 / 127.0;
  return t;
}

void SkylandsGen::sample(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample(); s.temp = 160; s.humid = 190;
  const double mask = nCont.fbm2(wx / 180.0, wz / 180.0, 3);
  if (mask <= 0.05) { s.height = -1; return; }
  const int topY = 70 + jsFloor(nHill.fbm2(wx / 60.0, wz / 60.0, 3) * 10);
  const double thick = (mask - 0.05) * 60, n = nCave1.noise3(wx / 30.0, (topY - thick * 0.5) / 20.0, wz / 30.0) * 0.4;
  s.height = (i16)(topY + SKY_DY); s.top = B.sky_grass; s.under = B.dirt;
  s.floatBottom = (i16)(std::max(21, topY - std::max(0, (int)std::ceil(thick * (0.6 + n)) - 1)) + SKY_DY);
}

void SkylandsGen::trees(int cx, int cz, std::vector<TreeSample>& out) const {
  Mulberry32 tr(hash32(seed_, cx, cz, 44));
  int nt = tr() < 0.6 ? 1 + jsFloor(tr() * 2) : 0;
  for (int i = 0; i < nt; i++) {
    int x = cx * 16 + 2 + jsFloor(tr() * 12), z = cz * 16 + 2 + jsFloor(tr() * 12), H = 4 + jsFloor(tr() * 3);
    int top = islandTop(x, z);
    if (top < 0) continue;
    TreeSample t{x, top + 1, z, TREE_BIRCH, false, {}};
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
      bool inner = std::abs(dx) <= 1 && std::abs(dz) <= 1 && !(std::abs(dx) == 1 && std::abs(dz) == 1);
      t.cols.push_back({(i8)dx, (i8)dz, (i16)(top + 1 + (inner ? H + 2 : H)), (i16)(top + 1 + H - 2), B.cloud_leaves, (BlockId)(dx == 0 && dz == 0 ? B.birch_log : 0)});
    }
    out.push_back(std::move(t));
  }
}

} // namespace dq
