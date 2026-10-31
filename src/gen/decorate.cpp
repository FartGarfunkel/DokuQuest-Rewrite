// gen/decorate.cpp — the Overworld's plants, flowers and cave decoration (worldgen.js decorate, decorate2,
// decorateCaves). Elevation/depth tests use the original heights (OwColumn::h) so every column decides
// exactly as in the original; positions are band-local.
#include "gen/overworld.h"
#include "common/holiday.h"

namespace dq {
using namespace vmap;

void OverworldGen::decorate(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  const int bx = cx * 16, bz = cz * 16;
  const bool hw = halloween();   // October: three times the pumpkins
  const BlockId PLAIN_FLOWERS[] = {B.flower_yellow, B.flower_red, B.azure_bluet, B.oxeye_daisy, B.cornflower, B.red_tulip, B.orange_tulip, B.white_tulip, B.pink_tulip};
  const BlockId FOREST_FLOWERS[] = {B.lily_of_the_valley, B.allium, B.flower_red, B.lilac, B.rose_bush, B.peony};
  auto set = [&](int x, int y, int z, BlockId id, u8 meta = 0) { int i = colIdx(x, y, z); b.blocks[i] = id; b.meta[i] = meta; };
  auto tall = [&](int x, int y, int z, BlockId id) { if (y + 1 < DIM_H && b.blocks[colIdx(x, y + 1, z)] == AIR) { set(x, y, z, id, 0); set(x, y + 1, z, id, 1); } };
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const OwColumn& c = cols[z * 16 + x];
    const int y = c.H + 1, wx = bx + x, wz = bz + z;
    if (y >= DIM_H - 2) continue;
    const BlockId below = b.blocks[colIdx(x, y - 1, z)], here = b.blocks[colIdx(x, y, z)];
    const double r = hashFloat(wx, wz, seed_, 9), r2 = hashFloat(wx, wz, seed_, 19);
    // under water: sea grass, kelp, and coral in warm seas
    if (here == B.water && c.biome == BIOME_OCEAN) {
      const int depth = JS_SEA - c.h, depthB = SEA_LEVEL - c.H;
      if (c.temp > 0.3 && depth > 2 && depth < 10 && r < 0.12) {
        const BlockId corals[] = {B.tube_coral, B.brain_coral, B.bubble_coral, B.fire_coral, B.horn_coral};
        const BlockId blocksC[] = {B.tube_coral_block, B.brain_coral_block, B.bubble_coral_block, B.fire_coral_block, B.horn_coral_block};
        if (r < 0.04) set(x, y - 1, z, blocksC[jsFloor(r2 * 5)]);
        else set(x, y, z, corals[jsFloor(r2 * 5)], WLOG);
      } else if (r < 0.18) set(x, y, z, B.seagrass, WLOG);
      else if (r < 0.24 && depth > 4) {
        int kh = 1 + jsFloor(r2 * std::min(8, depthB - 2));
        for (int k = 0; k < kh; k++) if (b.blocks[colIdx(x, y + k, z)] == B.water) set(x, y + k, z, B.kelp, WLOG);
      }
      continue;
    }
    // lily pads on calm, shallow water near land
    if (here == B.water && y == SEA_LEVEL && c.h >= JS_SEA - 2 && c.humid > 0.2 && r < 0.05) { set(x, y + 1, z, B.lily_pad); continue; }
    if (here != AIR) continue;
    const double patch = nDetail.noise2(wx / 14.0, wz / 14.0);
    if (below == B.grass) {
      if (c.biome == BIOME_PLAINS) {
        if (patch > 0.45 && r < 0.35) set(x, y, z, PLAIN_FLOWERS[jsFloor(std::fmod(patch * 97, 1.0) * 9)]);
        else if (r < 0.16) set(x, y, z, B.tall_grass);
        else if (r < 0.185) tall(x, y, z, B.tall_grass_double);
        else if (r < 0.19) tall(x, y, z, B.sunflower);
        else if (r < (hw ? 0.1945 : 0.1915) && r2 < 0.5) set(x, y, z, B.pumpkin);
        else if (r < 0.205) set(x, y, z, PLAIN_FLOWERS[jsFloor(r2 * 3)]);
      } else if (c.biome == BIOME_FOREST) {
        if (r < 0.08) set(x, y, z, B.tall_grass);
        else if (r < 0.095) set(x, y, z, B.fern);
        else if (r < 0.105) set(x, y, z, B.berry_bush);
        else if (r < 0.115) { BlockId f = FOREST_FLOWERS[jsFloor(r2 * 6)]; if (B.dbl[f]) tall(x, y, z, f); else set(x, y, z, f); }
        else if (r < 0.12) set(x, y, z, r2 < 0.5 ? B.brown_mushroom : B.red_mushroom);
        else if (r < 0.1205 && c.temp > 0.2) set(x, y, z, B.melon);
      } else if (c.biome == BIOME_MOUNTAINS) {
        if (r < 0.05) set(x, y, z, B.tall_grass);
        else if (r < 0.065) set(x, y, z, r2 < 0.5 ? B.lavender : B.heather);
        else if (r < 0.072) set(x, y, z, B.azure_bluet);
      } else if (r < 0.06) set(x, y, z, B.tall_grass);
      // sugar cane on shores
      if (r > 0.97 && c.h <= JS_SEA + 2 && b.blocks[colIdx(x, y, z)] == AIR) {
        bool wet = false;
        for (int d = 0; d < 4; d++) { int nx = x + (d == 0) - (d == 1), nz = z + (d == 2) - (d == 3); if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && b.blocks[colIdx(nx, y - 1, nz)] == B.water) wet = true; }
        if (wet) for (int k = 0; k < 1 + jsFloor(r2 * 3); k++) set(x, y + k, z, B.sugar_cane);
      }
    } else if (below == B.snowy_grass || below == B.podzol) {
      if (r < 0.012) set(x, y, z, B.berry_bush);
      else if (r < 0.07) set(x, y, z, B.fern);
      else if (r < 0.08) tall(x, y, z, B.large_fern);
      else if (below == B.snowy_grass && r < 0.38) set(x, y, z, B.snow_layer, (u8)jsFloor(r2 * 2));
    } else if (below == B.mud) {
      if (r < 0.08) set(x, y, z, B.tall_grass); else if (r < 0.1) set(x, y, z, B.blue_orchid);
    } else if ((below == B.sand || below == B.coarse_dirt) && c.biome != BIOME_BEACH && c.biome != BIOME_OCEAN && r < 0.012 && r >= 0.006) {
      set(x, y, z, B.dead_bush);
    } else if (below == B.sand && c.biome == BIOME_DESERT && r < 0.006) {
      int hgt = 1 + jsFloor(hashFloat(wx, wz, seed_, 10) * 3);
      bool ok = true;
      for (int d = 0; d < 4; d++) { int nx = x + (d == 0) - (d == 1), nz = z + (d == 2) - (d == 3); if (nx >= 0 && nx < 16 && nz >= 0 && nz < 16 && b.blocks[colIdx(nx, y, nz)] != AIR) ok = false; }
      if (ok) for (int k = 0; k < hgt; k++) set(x, y + k, z, B.cactus);
    }
  }
  decorate2(b, cols, cx, cz);
  decorateCaves(b, cols, cx, cz);
  // mushrooms and amethyst in the dark below
  Mulberry32 rr(hash32(seed_, cx, cz, 91));
  for (int k = 0; k < 6; k++) {
    int x = jsFloor(rr() * 16), z = jsFloor(rr() * 16), yo = 12 + jsFloor(rr() * 40), y = owUnder(yo);
    BlockId bl = b.blocks[colIdx(x, y - 1, z)];
    if (b.blocks[colIdx(x, y, z)] == AIR && B.solid[bl] && bl != B.water)
      set(x, y, z, k < 4 ? (rr() < 0.5 ? B.brown_mushroom : B.red_mushroom) : (yo < 30 && rr() < 0.3 ? B.amethyst_cluster : B.brown_mushroom));
  }
}

void OverworldGen::decorate2(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  const int bx = cx * 16, bz = cz * 16;
  const bool hw = halloween();   // October: three times the pumpkin patches
  auto at = [&](int x, int y, int z) -> int { return (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y >= DIM_H) ? -1 : (int)b.blocks[colIdx(x, y, z)]; };
  auto set = [&](int x, int y, int z, BlockId id, u8 meta = 0) { if (x < 0 || x > 15 || z < 0 || z > 15 || y < 1 || y >= DIM_H) return; int i = colIdx(x, y, z); b.blocks[i] = id; b.meta[i] = meta; };
  auto waterBeside = [&](int x, int y, int z) { return at(x + 1, y, z) == B.water || at(x - 1, y, z) == B.water || at(x, y, z + 1) == B.water || at(x, y, z - 1) == B.water; };
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const OwColumn& c = cols[z * 16 + x];
    const int y = c.H + 1, wx = bx + x, wz = bz + z;
    if (y >= DIM_H - 3) continue;
    const int below = at(x, y - 1, z), here = at(x, y, z);
    const double r = hashFloat(wx, wz, seed_, 41), r2 = hashFloat(wx, wz, seed_, 42);
    const bool jungle = c.temp > 0.25 && c.humid > 0.3;
    const double patch = nDetail.noise2(wx / 22.0 + 7, wz / 22.0 - 3);
    // the warm sea: sea pickles and coral fans
    if (here == B.water && c.biome == BIOME_OCEAN && c.temp > 0.3) {
      const int depth = JS_SEA - c.h;
      if (depth > 2 && depth < 12 && r < 0.02) set(x, y, z, B.sea_pickle, WLOG | jsFloor(r2 * 4));
      else if (depth > 2 && depth < 10 && r > 0.985) {
        const BlockId fans[] = {B.tube_coral_fan, B.brain_coral_fan, B.bubble_coral_fan, B.fire_coral_fan, B.horn_coral_fan};
        set(x, y, z, fans[jsFloor(r2 * 5)], WLOG);
      }
      continue;
    }
    if (here != 0) continue;
    // shore plants: cattails at the shallows' edge, marram grass on dunes
    if ((below == B.grass || below == B.mud || below == B.sand) && c.h <= JS_SEA + 1 && waterBeside(x, y - 1, z) && r < 0.25 && at(x, y + 1, z) == 0) { set(x, y, z, B.cattail, 0); set(x, y + 1, z, B.cattail, 1); continue; }
    if ((below == B.sand || below == B.silver_sand) && c.biome == BIOME_BEACH && r < 0.08) { set(x, y, z, B.marram_grass); continue; }
    // loose stones on hills, beaches and mountain meadows
    if (below >= 0 && B.solid[below] && below != B.water && (c.biome == BIOME_MOUNTAINS || c.biome == BIOME_BEACH || (c.h > JS_SEA + 14 && c.biome != BIOME_DESERT)) && r > 0.988) { set(x, y, z, B.loose_stones); continue; }
    if (below == B.dirt_with_dry_grass) { if (r < 0.2) set(x, y, z, B.dry_grass); else if (r < 0.205) set(x, y, z, B.dead_bush); continue; }
    if (below == B.dirt_with_rainforest_litter || (jungle && below == B.grass)) {
      // bamboo groves stay out of villages and off other buildings (they'd poke through roofs, roads and walls)
      if (patch > 0.42 && r < 0.55) {
        if (!(structures.active() && structures.flagged(wx, wz, 0, SF_SURFACE))) { int h = 4 + jsFloor(r2 * 9); for (int k = 0; k < h && at(x, y + k, z) == 0; k++) set(x, y + k, z, B.bamboo); }
      }
      else if (r < 0.22) set(x, y, z, B.jungle_grass);
      else if (r < 0.225) set(x, y, z, B.melon);
      continue;
    }
    if (below == B.dirt_with_coniferous_litter) { if (r < 0.05) set(x, y, z, B.fern); else if (r < 0.06) set(x, y, z, B.brown_mushroom); continue; }
    if (below == B.grass) {
      if (patch > 0.6 && r < (hw ? 0.15 : 0.05) && (c.biome == BIOME_PLAINS || c.biome == BIOME_FOREST)) set(x, y, z, B.pumpkin, (u8)jsFloor(r2 * 4));   // facing meta is legacy again: carve lives in its own byte now
      else if (c.biome == BIOME_MOUNTAINS && c.humid > 0.15 && c.h < 78 && r < 0.18) set(x, y, z, B.pink_petals);
      else if (c.biome == BIOME_PLAINS && r > 0.9985) set(x, y, z, B.torchflower);
      else if (c.biome == BIOME_PLAINS && r > 0.997 && r < 0.9985 && at(x, y + 1, z) == 0) { set(x, y, z, B.pitcher_plant, 0); set(x, y + 1, z, B.pitcher_plant, 1); }
    }
  }
}

void OverworldGen::decorateCaves(GenBuf& b, const OwColumn* cols, int cx, int cz) const {
  const int bx = cx * 16, bz = cz * 16;
  auto at = [&](int x, int y, int z) -> int { return (x < 0 || x > 15 || z < 0 || z > 15 || y < 0 || y >= DIM_H) ? -1 : (int)b.blocks[colIdx(x, y, z)]; };
  auto set = [&](int x, int y, int z, BlockId id, u8 meta = 0) { int i = colIdx(x, y, z); b.blocks[i] = id; b.meta[i] = meta; };
  auto stony = [&](int id) { return id == B.stone || id == B.slate || id == B.granite || id == B.diorite || id == B.andesite || id == B.tuff || id == B.calcite || id == B.dripstone_block || id == B.dirt || id == B.gravel; };
  const i32 hs = seed_ ^ 0x51;
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const OwColumn& c = cols[z * 16 + x];
    const int wx = bx + x, wz = bz + z;
    const int top = std::min(c.H - 3, owUnder(100));
    for (int y = owUnder(6); y < top; y++) {
      if (b.blocks[colIdx(x, y, z)] != AIR) continue;
      const int above = at(x, y + 1, z), below = at(x, y - 1, z);
      const double r = hashFloat(wx, y, wz, hs), yo = owUnderO(y);
      const bool lush = yo > 18 && y < c.H - 8 && nCave3.noise3(wx / 40.0, yo / 24, wz / 40.0) > 0.42;
      if (lush) {
        // lush caves: moss underfoot, azalea and glow berries hanging from the roof
        if (stony(below)) { set(x, y - 1, z, B.moss_block); if (r < 0.25) set(x, y, z, B.moss_carpet); else if (r < 0.3) set(x, y, z, B.azalea); else if (r < 0.34) set(x, y, z, B.tall_grass); continue; }
        if (stony(above)) {
          if (r < 0.12) { int len = 1 + jsFloor(hashFloat(wx, y, wz, 7) * 5); for (int k = 0; k < len && at(x, y - k, z) == 0; k++) set(x, y - k, z, hashFloat(wx, y - k, wz, 8) < 0.25 ? B.cave_vines_lit : B.cave_vines); }
          else if (r > 0.995) set(x, y, z, B.spore_blossom);
          else if (r > 0.9) set(x, y + 1, z, B.moss_block);
        }
        continue;
      }
      // pointed dripstone off dripstone blocks
      if (above == B.dripstone_block && r < 0.3) { set(x, y, z, B.pointed_dripstone, 1); continue; }
      if (below == B.dripstone_block && r < 0.25) { set(x, y, z, B.pointed_dripstone, 0); continue; }
      // roots hanging into caves just under the surface
      if ((above == B.dirt || above == B.stone) && y > c.H - 9 && r < 0.08) { set(x, y, z, B.hanging_roots); continue; }
      // glow lichen on cave walls
      if (y < c.H - 6 && r < 0.012) {
        for (int d = 0; d < 4; d++) { int w = at(x + DIRX[d], y, z + DIRZ[d]); if (w > 0 && stony(w)) { set(x, y, z, B.glow_lichen, (u8)d); break; } }
      }
    }
  }
  // a rare amethyst geode: calcite and smooth basalt shells round a hollow of amethyst
  Mulberry32 rnd(hash32(seed_, cx, cz, 0x9e0));
  if (rnd() < 1.0 / 14) {
    double gcx = 4 + rnd() * 8, cy = (14 + rnd() * 26) * OW_UNDER, gcz = 4 + rnd() * 8, R = 3.6 + rnd() * 1.2;
    for (int y = jsFloor(cy - R - 1); y <= cy + R + 1; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      double d = std::sqrt((x + 0.5 - gcx) * (x + 0.5 - gcx) + ((y + 0.5 - cy) * 1.1) * ((y + 0.5 - cy) * 1.1) + (z + 0.5 - gcz) * (z + 0.5 - gcz));
      if (d > R + 1 || y < 2) continue;
      int cur = at(x, y, z); if (cur == B.bedrock || cur == B.lava || cur == B.water) continue;
      if (d > R) set(x, y, z, B.smooth_basalt);
      else if (d > R - 1) set(x, y, z, B.calcite);
      else if (d > R - 2) set(x, y, z, rnd() < 0.1 ? B.budding_amethyst : B.amethyst_block);
      else set(x, y, z, AIR);
    }
    for (int y = jsFloor(cy - R); y <= cy + R; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      int bl = at(x, y - 1, z);
      if (at(x, y, z) == 0 && (bl == B.budding_amethyst || bl == B.amethyst_block) &&
          std::sqrt((x + 0.5 - gcx) * (x + 0.5 - gcx) + (y + 0.5 - cy) * (y + 0.5 - cy) + (z + 0.5 - gcz) * (z + 0.5 - gcz)) < R) {
        double q = rnd(); if (q < 0.25) set(x, y, z, q < 0.08 ? B.amethyst_cluster : q < 0.16 ? B.large_amethyst_bud : B.small_amethyst_bud);
      }
    }
  }
}

} // namespace dq
