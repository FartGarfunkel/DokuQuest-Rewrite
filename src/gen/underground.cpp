// gen/underground.cpp — the Overworld below the surface, Minecraft Beta style (heights counted from the band floor,
// which is Minecraft's y 0: bedrock 0-4, sea 64): beta carver caves and ravines (gen/carvers.cpp), rock blobs and
// ore veins (WorldGenMinable) at Minecraft's heights and rates plus the original's ores (game/metals.js) at
// matching depths, lava lakes in the deep (WorldGenLakes) and water/lava springs (WorldGenLiquids).
// Veins are seeded per source chunk and written from the 3x3 neighbourhood, so they run across chunk borders.
#include "gen/overworld.h"
#include "gen/carvers.h"

namespace dq {

// ---- carving: worm caves, rooms, ravines ----------------------------------------------------------------------
void OverworldGen::initUnderground() {
  CarveRules& R = carve_;
  R.carvable.assign(MAX_BLOCKS, 0); R.stopper.assign(MAX_BLOCKS, 0);
  for (BlockId id : {B.stone, B.slate, B.dirt, B.grass, B.snowy_grass, B.podzol, B.coarse_dirt, B.dirt_with_dry_grass,
                     B.dirt_with_coniferous_litter, B.dirt_with_rainforest_litter, B.mud, B.peat, B.sandstone, B.desert_stone,
                     B.desert_sandstone, B.snow_block, B.granite, B.diorite, B.andesite, B.tuff, B.marble, B.limestone,
                     B.calcite, B.dripstone_block})
    if (id) R.carvable[id] = 1;
  for (BlockId id : {B.water, B.ice}) if (id) R.stopper[id] = 1;
  R.grass = B.grass; R.dirt = B.dirt; R.lava = B.lava; R.lavaY = 10; R.minY = 1; R.maxY = DIM_H - 8;
  R.seed = (i64)((u64)(i64)seed_ * 341873128712ULL + 0x0CA7E5);
  rockRep_.assign(MAX_BLOCKS, 0); oreRep_.assign(MAX_BLOCKS, 0);
  for (BlockId id : {B.stone, B.slate}) if (id) rockRep_[id] = oreRep_[id] = 1;
  for (BlockId id : {B.granite, B.diorite, B.andesite, B.tuff}) if (id) oreRep_[id] = 1;
}

void OverworldGen::carve(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  // tree trunks keep their footing (both chunks a crown spans must agree the tree stands)
  i16 protect[256];
  for (int i = 0; i < 256; i++) protect[i] = DIM_H;
  std::vector<TreeSpec> ts; treesForChunk(cx, cz, ts);
  for (const TreeSpec& t : ts) {
    const int lx = t.x - cx * 16, lz = t.z - cz * 16;
    if (lx < 0 || lx > 15 || lz < 0 || lz > 15) continue;
    protect[lz * 16 + lx] = (i16)std::max(1, cols[lz * 16 + lx].H - 4);
  }
  CarveRules r = carve_; r.protect = protect;
  carveCaves(b, cx, cz, r);
  carveRavines(b, cx, cz, r);
}

// ---- rock blobs and ore veins -----------------------------------------------------------------------------------
void OverworldGen::placeOres(GenBuf& b, int cx, int cz, Mulberry32&) const {
  const int bx = cx * 16, bz = cz * 16;
  // tries per chunk, vein size, band heights [lo, hi) (Minecraft Beta 1.7 / 1.8 values; the original's ores at
  // the depths metals.js gave them, scaled to the band)
  struct Ore { BlockId id; double tries; int size, lo, hi; bool ore; };
  const Ore ORES[] = {
    {B.dirt, 16, 33, 1, 128, false}, {B.gravel, 8, 33, 1, 128, false},
    {B.granite, 6, 33, 1, 80, false}, {B.diorite, 6, 33, 1, 80, false}, {B.andesite, 6, 33, 1, 80, false},
    {B.tuff, 3, 33, 1, 20, false}, {B.marble, 0.6, 24, 30, 110, false}, {B.limestone, 0.8, 24, 40, 100, false},
    {B.calcite, 0.4, 16, 12, 60, false}, {B.dripstone_block, 0.5, 20, 8, 48, false},
    {B.coal_ore, 20, 17, 1, 128, true}, {B.iron_ore, 20, 9, 1, 64, true}, {B.copper_ore, 10, 10, 16, 96, true},
    {B.tin_ore, 7, 8, 16, 72, true}, {B.gold_ore, 2, 9, 1, 32, true}, {B.redstone_ore, 8, 8, 1, 16, true},
    {B.diamond_ore, 1, 8, 1, 16, true}, {B.silver_ore, 3, 8, 1, 44, true}, {B.ruby_ore, 0.8, 5, 1, 30, true},
    {B.sapphire_ore, 0.8, 5, 1, 30, true}, {B.mithril_ore, 0.7, 5, 1, 22, true}, {B.mese_ore, 0.5, 5, 1, 15, true},
    {B.adamantite_ore, 0.3, 4, 1, 12, true},
  };
  for (int ncz = cz - 1; ncz <= cz + 1; ncz++) for (int ncx = cx - 1; ncx <= cx + 1; ncx++) {
    JRandom rnd(chunkSeed(seed_, ncx, ncz, 0x0E5));
    for (const Ore& o : ORES) {
      int n = (int)o.tries; if (rnd.nextFloat() < o.tries - n) n++;
      for (int k = 0; k < n; k++) {
        const int x = ncx * 16 + rnd.nextInt(16), y = o.lo + rnd.nextInt(o.hi - o.lo), z = ncz * 16 + rnd.nextInt(16);
        oreVein(b, cx, cz, rnd, x, y, z, o.size, o.id, o.ore ? oreRep_.data() : rockRep_.data());
      }
    }
    // lapis: Minecraft's triangle round 16
    const int x = ncx * 16 + rnd.nextInt(16), y = rnd.nextInt(16) + rnd.nextInt(16) + 1, z = ncz * 16 + rnd.nextInt(16);
    oreVein(b, cx, cz, rnd, x, y, z, 7, B.lapis_ore, oreRep_.data());
  }
  // emeralds: single blocks inside the mountains (Minecraft's extreme hills)
  if (column(bx + 8, bz + 8).biome == BIOME_MOUNTAINS) {
    JRandom rnd(chunkSeed(seed_, cx, cz, 0xE3E));
    const int n = 3 + rnd.nextInt(6);
    for (int k = 0; k < n; k++) {
      const int x = rnd.nextInt(16), z = rnd.nextInt(16), H = column(bx + x, bz + z).H;
      const int y = 4 + rnd.nextInt(std::max(1, std::min(H - 6, 180) - 4));
      BlockId& cur = b.blocks[colIdx(x, y, z)];
      if (cur == B.stone) cur = B.emerald_ore;
    }
  }
}

// ---- lava lakes in the deep (WorldGenLakes, inside the chunk) and springs (WorldGenLiquids) -----------------------
void OverworldGen::placeLakes(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  JRandom rnd(chunkSeed(seed_, cx, cz, 0x1A4E));
  if (rnd.nextInt(6) != 0) return;
  int y = 4 + rnd.nextInt(rnd.nextInt(44) + 8);   // mostly low down: Minecraft's lava level is ~10
  int minH = DIM_H; for (int i = 0; i < 256; i++) minH = std::min(minH, cols[i].H);
  if (y + 8 > minH - 6) return;
  while (y > 5 && b.blocks[colIdx(8, y, 8)] == AIR) y--;
  y -= 4;
  if (y < 2) return;
  bool cell[16 * 16 * 8] = {};
  const int blobs = rnd.nextInt(4) + 4;
  for (int j = 0; j < blobs; j++) {
    const double sx = rnd.nextDouble() * 6 + 3, sy = rnd.nextDouble() * 4 + 2, sz = rnd.nextDouble() * 6 + 3;
    const double px = rnd.nextDouble() * (16 - sx - 2) + 1 + sx / 2, py = rnd.nextDouble() * (8 - sy - 4) + 2 + sy / 2,
                 pz = rnd.nextDouble() * (16 - sz - 2) + 1 + sz / 2;
    for (int x = 1; x < 15; x++) for (int z = 1; z < 15; z++) for (int yy = 1; yy < 7; yy++) {
      const double dx = (x - px) / (sx / 2), dy = (yy - py) / (sy / 2), dz = (z - pz) / (sz / 2);
      if (dx * dx + dy * dy + dz * dz < 1) cell[(x * 16 + z) * 8 + yy] = true;
    }
  }
  auto C = [&](int x, int yy, int z) { return x >= 0 && x < 16 && z >= 0 && z < 16 && yy >= 0 && yy < 8 && cell[(x * 16 + z) * 8 + yy]; };
  for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) for (int yy = 0; yy < 8; yy++) {
    if (C(x, yy, z)) continue;
    if (!(C(x + 1, yy, z) || C(x - 1, yy, z) || C(x, yy + 1, z) || C(x, yy - 1, z) || C(x, yy, z + 1) || C(x, yy, z - 1))) continue;
    const BlockId id = b.blocks[colIdx(x, y + yy, z)];
    if (yy >= 4 && B.liquid[id]) return;
    if (yy < 4 && !B.solid[id] && id != B.lava) return;
  }
  for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) for (int yy = 0; yy < 8; yy++) if (C(x, yy, z)) {
    const int i = colIdx(x, y + yy, z);
    if (b.blocks[i] == B.bedrock || b.blocks[i] == B.ember_crust) continue;
    b.blocks[i] = yy >= 4 ? AIR : B.lava; b.meta[i] = 0;
  }
  // the lake's rim hardens to stone
  for (int x = 0; x < 16; x++) for (int z = 0; z < 16; z++) for (int yy = 0; yy < 8; yy++) {
    if (C(x, yy, z)) continue;
    if (!(C(x + 1, yy, z) || C(x - 1, yy, z) || C(x, yy + 1, z) || C(x, yy - 1, z) || C(x, yy, z + 1) || C(x, yy, z - 1))) continue;
    const int i = colIdx(x, y + yy, z);
    if ((yy < 4 || rnd.nextInt(2) != 0) && B.solid[b.blocks[i]] && b.blocks[i] != B.bedrock && b.blocks[i] != B.ember_crust) b.blocks[i] = B.stone;
  }
}

void OverworldGen::placeSprings(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  JRandom rnd(chunkSeed(seed_, cx, cz, 0x5B1));
  auto spring = [&](BlockId liquid, int x, int y, int z) {
    if (x < 1 || x > 14 || z < 1 || z > 14 || y < 2 || y >= cols[z * 16 + x].H - 3) return;
    if (b.blocks[colIdx(x, y + 1, z)] != B.stone || b.blocks[colIdx(x, y - 1, z)] != B.stone) return;
    const BlockId here = b.blocks[colIdx(x, y, z)];
    if (here != AIR && here != B.stone) return;
    int stone = 0, air = 0;
    for (int d = 0; d < 4; d++) {
      const BlockId n = b.blocks[colIdx(x + DIRX[d], y, z + DIRZ[d])];
      if (n == B.stone) stone++; else if (n == AIR) air++;
    }
    if (stone == 3 && air == 1) { const int i = colIdx(x, y, z); b.blocks[i] = liquid; b.meta[i] = 0; }
  };
  for (int k = 0; k < 50; k++) { int x = rnd.nextInt(16), y = rnd.nextInt(rnd.nextInt(120) + 8), z = rnd.nextInt(16); spring(B.water, x, y, z); }
  for (int k = 0; k < 20; k++) { int x = rnd.nextInt(16), y = rnd.nextInt(rnd.nextInt(rnd.nextInt(112) + 8) + 8), z = rnd.nextInt(16); spring(B.lava, x, y, z); }
}

} // namespace dq
