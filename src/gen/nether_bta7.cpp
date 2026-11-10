// gen/nether_bta7.cpp — terrain version 7: the large features and decorations of Better than Adventure 8.0's Nether,
// after its generator (ChunkDecoratorNether, the Nether's LargeFeatures; numbers from the research notes), written for
// this engine:
//   - lava floes: the Volcanic Islands' sea paved with chunk-sized islands of rack under brimsand, cracks of lava between
//   - rubyglass floes: slabs of crystalline netherrack in the crystal lands' crusted water
//   - the Shelf's brimsand terraces, 1-5 high, stepping at the cells' edges
//   - glowstone: rare (about one cluster in eight chunks, none in the Old World), hanging from roofs
//   - giant rubyglass crystals: tilted prisms of rubyglass column (light 0) with a few nodes, on floors and roofs, a
//     crystalline foot, growths round them
//   - hourglass pillars from floor to roof on a 32-block grid: brimstone in the hot lands, slate in the Old World,
//     rack in the Crag and the Shelf, now and then rubyglass in the crystal lands
//   - sulphur pockets in the Sulfur Pools' brimstone and glowing brimthaw on its walls
//   - the Old World: a layer of ash on every open top above the sea, embers buried under it here and there
//   - bone piles everywhere, magmatic boulders (Volcanic), sulfuric boulders (Sulfur Pools), soul catchers (Old World)
#include "gen/dimgens.h"
#include "gen/nether_bta.h"
#include "gen/carvers.h"

namespace dq {

namespace {
// Worley cells of `size` blocks: the distance from (x, z) to the edge between its cell and the next, and the cell's hash
struct Cell { double border; u32 hash; };
Cell worley(int x, int z, int size, i32 seed) {
  const int gx = floordiv(x, size), gz = floordiv(z, size);
  double d1 = 1e9, d2 = 1e9; u32 h1 = 0;
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    u32 h = hash32(gx + dx, gz + dz, seed, 0x3011);
    double px = (gx + dx) * size + ((h & 0xFFFF) / 65535.0) * size, pz = (gz + dz) * size + ((h >> 16) / 65535.0) * size;
    double d = std::hypot(x - px, z - pz);
    if (d < d1) { d2 = d1; d1 = d; h1 = h; } else if (d < d2) d2 = d;
  }
  return {(d2 - d1) * 0.5, h1};
}
}

void DepthsGen::btaFeatures7(GenBuf& b, int cx, int cz, const u8* atSea) const {
  const NetherClimate& C = *cave_;
  const int bx = cx * 16, bz = cz * 16, SEA = NetherClimate::OCEAN_Y;
  auto at = [&](int x, int y, int z) { return b.at(x, y, z); };
  auto set = [&](int x, int y, int z, BlockId id) { if (id && x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < DIM_H - 1) b.blocks[colIdx(x, y, z)] = id; };
  auto solidish = [&](BlockId id) { return id && id != 0xFFFF && id != B.lava && id != B.water && id != B.acid && id != B.fire; };
  auto fluidOrAir = [&](BlockId id) { return id == AIR || id == B.lava || id == B.water || id == B.rubyglass_crystal; };
  const BlockId cobble = B.cobbled_netherrack ? B.cobbled_netherrack : B.emberstone;
  const BlockId brimsand = B.brimsand ? B.brimsand : B.cinder_sand, brimstone = B.nether_brimstone ? B.nether_brimstone : B.basalt;
  auto rack = [&](int x, int y, int z) { return hashFloat(bx + x, y, bz + z, seed_ ^ 0x7A) < 0.5 ? B.netherrack : cobble; };
  const SimplexNoise& nF = C.nFuzz;

  // ---- caves: BTA carves the Nether with the classic beta cave carver (CavesLargeFeature 64..256), not the Nether's ----
  {
    CarveRules R;
    R.carvable.assign(MAX_BLOCKS, 0); R.stopper.assign(MAX_BLOCKS, 0);
    for (BlockId id : {B.netherrack, cobble, B.basalt, B.ash, B.soul_sand, B.rubyglass_crystal, B.gloomstone, B.cobbled_gloomstone, B.ember, brimsand, brimstone, B.sulphur_block})
      if (id) R.carvable[id] = 1;
    for (BlockId id : {B.lava, B.water}) if (id) R.stopper[id] = 1;   // a segment that would touch the sea is skipped
    R.lava = 0; R.minY = 65; R.maxY = 247; R.hell = false;
    R.seed = (i64)((u64)(i64)seed_ * 6364136223846793005ULL + 0xB7A7);
    carveCaves(b, cx, cz, R);
  }

  // ---- the sea's floes and the Shelf's terraces (one Worley cell = one chunk, like BTA's) ----
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x, wz = bz + z;
    const u8 land = atSea[z * 16 + x];
    if (land != NB_VOLCANIC && land != NB_CRYSTAL && land != NB_CRYSTAL_PLAINS && land != NB_SHELF) continue;
    const Cell c = worley(wx, wz, 16, seed_);
    const double n5 = nF.noise2(wx / 5.0, wz / 5.0) * 0.5 + 0.5;
    if (land == NB_VOLCANIC) {
      if (c.hash % 15 == 0 || c.border < 1.2 + n5 * 0.8) continue;   // the lava cracks, ~3 wide
      const int hm = 1 + (int)((c.hash >> 8) % 3), h = (int)(n5 * hm);
      for (int y = SEA - 3; y <= SEA + h - 2; y++) if (fluidOrAir(at(x, y, z))) set(x, y, z, hashFloat(wx, y, wz, seed_ ^ 0x9E) < 0.05 && B.magma_block ? B.magma_block : rack(x, y, z));
      for (int y = std::max(SEA - 2, SEA + h - 1); y <= SEA + h; y++) if (fluidOrAir(at(x, y, z))) set(x, y, z, brimsand);
    } else if (land == NB_SHELF) {
      const int h = 1 + (int)((c.hash >> 8) % 5);
      for (int y = 76; y <= SEA + h - 1; y++) if (at(x, y, z) == AIR || at(x, y, z) == B.water) set(x, y, z, brimsand);
    } else {
      if (c.border < 2.6 + nF.noise2(wx / 15.0, wz / 15.0) * 0.4) continue;   // 5-6 blocks of crusted water between the floes
      const int depth = (int)((nF.noise2(x / 10.0 + cx * 1.618, z / 10.0 - cz * 0.7) * 0.5 + 0.5) * 6);
      for (int y = SEA - (depth + 2); y <= SEA - 1; y++) if (fluidOrAir(at(x, y, z))) set(x, y, z, B.crystalline_netherrack ? B.crystalline_netherrack : cobble);
      for (int y = SEA; y <= SEA + depth - 1; y++) if (fluidOrAir(at(x, y, z))) set(x, y, z, rack(x, y, z));
    }
  }

  JRandom rnd(chunkSeed(seed_, cx, cz, 0x4E77));
  auto land = [&](int x, int z) { return atSea[z * 16 + x]; };
  auto floorAt = [&](int x, int z, int y0, int y1, int& y) {
    for (int yy = y1; yy >= y0; yy--) if (at(x, yy, z) == AIR && solidish(at(x, yy - 1, z))) { y = yy; return true; }
    return false;
  };
  auto roofAt = [&](int x, int z, int y0, int y1, int& y) {
    for (int yy = y0; yy <= y1; yy++) if (at(x, yy, z) == AIR && solidish(at(x, yy + 1, z))) { y = yy; return true; }
    return false;
  };

  // ---- glowstone: rare, from the roofs (BTA seeds it at random points that touch a ceiling) ----
  if (B.glowstone && rnd.nextInt(8) == 0) {
    const int x = 2 + rnd.nextInt(12), z = 2 + rnd.nextInt(12);
    int y = 0;
    if (land(x, z) != NB_OLDWORLD && roofAt(x, z, 110, 236, y)) {
      set(x, y, z, B.glowstone);
      for (int i = 0; i < 260; i++) {
        int px = x + rnd.nextInt(6) - rnd.nextInt(6), py = y - rnd.nextInt(10), pz = z + rnd.nextInt(6) - rnd.nextInt(6);
        if (px < 0 || px > 15 || pz < 0 || pz > 15 || at(px, py, pz) != AIR) continue;
        int nb = 0;
        static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (auto& d : D) if (at(px + d[0], py + d[1], pz + d[2]) == B.glowstone) nb++;
        if (nb == 1) set(px, py, pz, B.glowstone);
      }
    }
  }

  // ---- giant rubyglass crystals: tilted prisms (kept within the chunk so no neighbour has to know them) ----
  const BlockId col = B.rubyglass_column ? B.rubyglass_column : B.red_crystal;
  for (int n = 0; n < 6 && col; n++) {
    const int x = 4 + rnd.nextInt(8), z = 4 + rnd.nextInt(8);
    const u8 l = land(x, z);
    const bool forest = l == NB_CRYSTAL;
    if (!forest && !(l == NB_CRYSTAL_PLAINS && rnd.nextInt(3) == 0)) continue;
    const bool up = n % 2 == 0;
    int y = 0;
    if (up ? !floorAt(x, z, SEA, 170, y) : !roofAt(x, z, 120, 236, y)) continue;
    const int len = (forest ? 9 : 5) + rnd.nextInt(forest ? 12 : 5);
    const double tilt = rnd.nextInt(1000) / 1000.0 * 0.35, yaw = rnd.nextInt(1000) / 1000.0 * 6.2832;
    const double dx = std::sin(tilt) * std::cos(yaw), dz = std::sin(tilt) * std::sin(yaw), dy = std::cos(tilt) * (up ? 1 : -1);
    const double rad = len > 14 ? 1.5 : 1.0;
    // a crystalline foot
    for (int fz = -2; fz <= 2; fz++) for (int fx = -2; fx <= 2; fx++) if (fx * fx + fz * fz <= 5) {
      int yy = up ? y - 1 : y + 1;
      if (solidish(at(x + fx, yy, z + fz)) && B.crystalline_netherrack) set(x + fx, yy, z + fz, B.crystalline_netherrack);
    }
    for (int s = 0; s < len; s++) {
      const double cxp = x + 0.5 + dx * s, cyp = y + 0.5 + dy * s, czp = z + 0.5 + dz * s;
      const double r = rad * (s > len - 3 ? 0.5 : 1.0);
      const int R = (int)std::ceil(r);
      for (int oz = -R; oz <= R; oz++) for (int oy = -R; oy <= R; oy++) for (int ox = -R; ox <= R; ox++) {
        if (ox * ox + oy * oy + oz * oz > r * r + 0.25) continue;
        int px = (int)std::floor(cxp + ox), py = (int)std::floor(cyp + oy), pz = (int)std::floor(czp + oz);
        if (px < 0 || px > 15 || pz < 0 || pz > 15 || py <= SEA - 2 || py >= DIM_H - 4) continue;
        if (at(px, py, pz) != AIR) continue;
        set(px, py, pz, B.rubyglass_node && hashFloat(bx + px, py, bz + pz, seed_ ^ 0x40DE) < 0.01 ? B.rubyglass_node : col);
      }
    }
  }
  // growths about the crystal lands' floors
  for (int n = 0; n < 24 && B.rubyglass_growth; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    if (land(x, z) != NB_CRYSTAL && land(x, z) != NB_CRYSTAL_PLAINS) continue;
    int y = 0;
    if (floorAt(x, z, SEA, 180, y)) set(x, y, z, B.rubyglass_growth);
  }

  // ---- hourglass pillars on the 32-block grid (each one whole on every chunk it reaches) ----
  for (int gz = floordiv(bz - 8, 32); gz <= floordiv(bz + 24, 32); gz++)
    for (int gx = floordiv(bx - 8, 32); gx <= floordiv(bx + 24, 32); gx++) {
      const u32 ph = hash32(gx, gz, 0x7101, seed_);
      const int px = gx * 32 + (int)(ph >> 8 & 31), pz = gz * 32 + (int)(ph >> 16 & 31);
      const u8 l = C.biomeAt(px, SEA, pz);
      int chance = (l == NB_VOLCANIC || l == NB_SULFUR || l == NB_OLDWORLD) ? 2 : (l == NB_CRAG || l == NB_SHELF) ? 4 : 8;
      if ((ph & 255) % chance != 0) continue;
      const int r0 = 3 + (int)((ph >> 24) % 5);
      if (px + r0 + 1 < bx || px - r0 - 1 > bx + 15 || pz + r0 + 1 < bz || pz - r0 - 1 > bz + 15) continue;
      // floor and roof at the pillar's centre (from the density: the same answer on every chunk)
      int floorY = -1, roofY = -1;
      for (int y = 170; y > SEA; y--) if (!C.solidAt(px, y, pz) && C.solidAt(px, y - 1, pz)) { floorY = y; break; }
      if (floorY < 0) continue;
      for (int y = floorY + 4; y < 240; y++) if (C.solidAt(px, y, pz)) { roofY = y - 1; break; }
      if (roofY < 0 || roofY - floorY < 8 || roofY - floorY > 90) continue;
      BlockId mat = l == NB_OLDWORLD ? B.slate : (l == NB_VOLCANIC || l == NB_SULFUR) ? brimstone : l == NB_CRYSTAL || l == NB_CRYSTAL_PLAINS ? col : B.netherrack;
      for (int y = floorY - 2; y <= roofY + 2; y++) {
        const double t = (double)(y - floorY) / (roofY - floorY);
        const double r = r0 * (0.42 + 0.58 * (2 * t - 1) * (2 * t - 1));
        const int R = (int)std::ceil(r);
        for (int oz = -R; oz <= R; oz++) for (int ox = -R; ox <= R; ox++) {
          if (ox * ox + oz * oz > r * r) continue;
          const int lx = px + ox - bx, lz = pz + oz - bz;
          if (lx < 0 || lx > 15 || lz < 0 || lz > 15 || at(lx, y, lz) != AIR) continue;
          BlockId m = mat;
          if (mat == B.netherrack) m = rack(lx, y, lz);
          else if (mat == brimstone && B.brimthaw && hashFloat(px + ox, y, pz + oz, seed_ ^ 0xB7) < 0.06) m = B.brimthaw;
          set(lx, y, lz, m);
        }
      }
    }

  // ---- the Sulfur Pools: sulphur pockets in the brimstone, brimthaw on the walls ----
  for (int n = 0; n < 40 && B.sulphur_block; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16), y = SEA - 8 + rnd.nextInt(100);
    if (land(x, z) != NB_SULFUR || at(x, y, z) != brimstone) continue;
    const int r = 1 + rnd.nextInt(2);
    for (int oy = -r; oy <= r; oy++) for (int oz = -r; oz <= r; oz++) for (int ox = -r; ox <= r; ox++)
      if (ox * ox + oy * oy + oz * oz <= r * r + 1 && at(x + ox, y + oy, z + oz) == brimstone) set(x + ox, y + oy, z + oz, B.sulphur_block);
  }
  for (int n = 0; n < 220 && B.brimthaw; n++) {
    const int x = 1 + rnd.nextInt(14), z = 1 + rnd.nextInt(14), y = SEA + rnd.nextInt(120);
    if (land(x, z) != NB_SULFUR || at(x, y, z) != brimstone) continue;
    if (at(x + 1, y, z) == AIR || at(x - 1, y, z) == AIR || at(x, y, z + 1) == AIR || at(x, y, z - 1) == AIR) set(x, y, z, B.brimthaw);
  }

  // ---- the Old World: ash over every open top above the sea, embers buried under it ----
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    for (int y = SEA + 1; y < 236; y++) {
      if (at(x, y, z) != AIR) continue;
      const BlockId under = at(x, y - 1, z);
      if (!solidish(under) || under == B.ash_layer || under == B.glowstone) continue;
      if (C.biomeAt(bx + x, y, bz + z) != NB_OLDWORLD) continue;
      if (B.ash_layer) set(x, y, z, B.ash_layer);
      if (B.ember && hashFloat(bx + x, y, bz + z, seed_ ^ 0xE3B) < 0.02) set(x, y - 1, z, B.ember);
    }
  }

  // ---- springs: lava breaking out of the walls of the hot lands (lavafalls), water in the crystal lands ----
  auto wallSpring = [&](BlockId liquid, int x, int y, int z) {
    if (!liquid || x < 1 || x > 14 || z < 1 || z > 14 || y <= SEA || y > 240) return;
    if (!solidish(at(x, y + 1, z)) || !solidish(at(x, y - 1, z)) || !solidish(at(x, y, z))) return;
    int solid = 0, air = 0;
    for (int d = 0; d < 4; d++) { BlockId n = at(x + DIRX[d], y, z + DIRZ[d]); if (n == AIR) air++; else if (solidish(n)) solid++; }
    if (solid == 3 && air == 1) { set(x, y, z, liquid); b.meta[colIdx(x, y, z)] = 0; }
  };
  for (int n = 0; n < 24; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16), y = SEA + 4 + rnd.nextInt(140);
    const u8 l = land(x, z);
    if (l == NB_VOLCANIC || l == NB_CRAG || l == NB_SHELF) wallSpring(B.lava, x, y, z);
    else if ((l == NB_CRYSTAL || l == NB_CRYSTAL_PLAINS) && n < 16) wallSpring(B.water, x, y, z);
  }
  // ---- terraces: a ring of floors filled with lava (ringed in brimstone) or acid (ringed in brimsand) ----
  auto terrace = [&](int cxl, int czl, int r, BlockId fluid, BlockId ring, bool mound) {
    if (!fluid) return;
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      const double d = std::hypot(x - cxl, z - czl);
      if (d > r) continue;
      for (int y = 236; y > SEA; y--) {
        if (at(x, y, z) != AIR || !solidish(at(x, y - 1, z)) || at(x, y - 1, z) == fluid) continue;
        if (d > r - 1.2) set(x, y - 1, z, ring);
        else { set(x, y - 1, z, fluid); set(x, y - 2, z, ring); }
      }
    }
    if (mound && B.glowstone) {   // glowstone rising out of the acid
      int y = 0;
      if (floorAt(cxl, czl, SEA, 236, y)) for (int k = 0; k < 2 + rnd.nextInt(2); k++) set(cxl, y + k - 1, czl, B.glowstone);
    }
  };
  {
    const int x = 4 + rnd.nextInt(8), z = 4 + rnd.nextInt(8);
    const u8 l = land(x, z);
    if ((l == NB_VOLCANIC || l == NB_CRAG) && rnd.nextInt(3) == 0) terrace(x, z, 4 + rnd.nextInt(4), B.lava, brimstone, false);
    else if (l == NB_SULFUR && rnd.nextInt(2) == 0) terrace(x, z, 6 + rnd.nextInt(5), B.acid, brimsand, true);
  }
  // ---- a lava lake now and then on a floor (WorldGenLakes) ----
  if (B.lava && rnd.nextInt(4) == 0) {
    const int x = 4 + rnd.nextInt(8), z = 4 + rnd.nextInt(8);
    int y = 0;
    if (land(x, z) != NB_OLDWORLD && land(x, z) != NB_CRYSTAL && land(x, z) != NB_CRYSTAL_PLAINS && floorAt(x, z, SEA + 2, 220, y)) {
      const double rx = 2.5 + rnd.nextInt(3), rz = 2.5 + rnd.nextInt(3);
      for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) {
        const double e = (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz);
        if (e > 1) continue;
        const int px = x + dx, pz = z + dz;
        if (px < 1 || px > 14 || pz < 1 || pz > 14 || at(px, y, pz) != AIR || !solidish(at(px, y - 1, pz))) continue;
        set(px, y - 1, pz, B.lava);
        if (e < 0.4 && solidish(at(px, y - 2, pz))) set(px, y - 2, pz, B.lava);
      }
    }
  }
  // ---- vents: a basalt disc round a thermal vent; spire vents of basalt and coal with two vents on top ----
  const BlockId vent = B.steam_vent;
  for (int n = 0; n < 3 && vent && B.basalt; n++) {
    const int x = 2 + rnd.nextInt(12), z = 2 + rnd.nextInt(12);
    const u8 l = land(x, z);
    if (l != NB_VOLCANIC && l != NB_CRAG && l != NB_SULFUR) continue;
    int y = 0;
    if (!floorAt(x, z, SEA, 220, y)) continue;
    if (n < 2) {
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) if (dx * dx + dz * dz <= 5 && solidish(at(x + dx, y - 1, z + dz))) set(x + dx, y - 1, z + dz, B.basalt);
      set(x, y - 1, z, vent);
    } else {
      const int h = 3 + rnd.nextInt(4);
      for (int k = 0; k < h; k++) for (int dz = 0; dz <= 1; dz++) for (int dx = 0; dx <= 1; dx++)
        if (at(x + dx, y + k, z + dz) == AIR) set(x + dx, y + k, z + dz, B.basalt_coal_ore && rnd.nextInt(5) == 0 ? B.basalt_coal_ore : B.basalt);
      set(x, y + h, z, vent); set(x, y + h + 1, z, vent);
    }
  }
  // magma boulders: a mound of magma with nether coal in it (Volcanic)
  for (int n = 0; n < 2 && B.magma_block; n++) {
    const int x = 2 + rnd.nextInt(12), z = 2 + rnd.nextInt(12);
    int y = 0;
    if (land(x, z) != NB_VOLCANIC || !floorAt(x, z, SEA, 220, y)) continue;
    for (int oy = -1; oy <= 1; oy++) for (int oz = -2; oz <= 2; oz++) for (int ox = -2; ox <= 2; ox++)
      if (ox * ox + oz * oz + oy * oy * 3 <= 4 && (at(x + ox, y + oy, z + oz) == AIR || oy < 0))
        set(x + ox, y + oy, z + oz, B.nether_coal_ore && rnd.nextInt(6) == 0 ? B.nether_coal_ore : B.magma_block);
  }
  // ---- roof spires hanging from the ceilings, in the land's stone ----
  for (int n = 0; n < 3; n++) {
    const int x = 2 + rnd.nextInt(12), z = 2 + rnd.nextInt(12);
    int y = 0;
    if (!roofAt(x, z, SEA + 20, 240, y)) continue;
    const u8 l = land(x, z);
    const BlockId m = l == NB_OLDWORLD ? B.slate : (l == NB_VOLCANIC || l == NB_SULFUR) ? brimstone : l == NB_SHELF ? B.basalt : B.netherrack;
    const int len = 5 + rnd.nextInt(10);
    for (int k = 0; k < len; k++) {
      const double r = 2.2 * (1 - (double)k / len);
      const int R = (int)std::ceil(r);
      for (int oz = -R; oz <= R; oz++) for (int ox = -R; ox <= R; ox++)
        if (ox * ox + oz * oz <= r * r && at(x + ox, y - k, z + oz) == AIR) set(x + ox, y - k, z + oz, m == B.netherrack ? rack(x + ox, y - k, z + oz) : m);
    }
  }
  // ---- the Old World: pockets of soul sand in its gloomstone ----
  for (int n = 0; n < 40 && B.soul_sand; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16), y = SEA + rnd.nextInt(140);
    if (land(x, z) != NB_OLDWORLD) continue;
    const BlockId here = at(x, y, z);
    if (here != B.gloomstone && here != B.cobbled_gloomstone && here != B.ash) continue;
    for (int oy = -1; oy <= 1; oy++) for (int oz = -2; oz <= 2; oz++) for (int ox = -2; ox <= 2; ox++) {
      const BlockId h2 = at(x + ox, y + oy, z + oz);
      if ((h2 == B.gloomstone || h2 == B.cobbled_gloomstone || h2 == B.ash) && ox * ox + oz * oz + oy * oy <= 4) set(x + ox, y + oy, z + oz, B.soul_sand);
    }
  }

  // ---- scatter: bone piles, boulders, soul catchers ----
  for (int n = 0; n < 10; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    int y = 0;
    if (!floorAt(x, z, SEA, 200, y) || at(x, y - 1, z) == B.ash_layer) continue;
    const u8 l = land(x, z);
    BlockId put = B.bone_pile;
    if (l == NB_VOLCANIC && rnd.nextInt(2) == 0) put = B.magmatic_boulder;
    else if (l == NB_SULFUR && rnd.nextInt(2) == 0) put = B.sulfuric_boulder;
    else if (l == NB_OLDWORLD && rnd.nextInt(2) == 0) put = B.soul_catcher;
    else if (rnd.nextInt(3) != 0) continue;
    set(x, y, z, put);
  }
  // fire on the hot lands' rack (BTA sets the ground under each fire to netherrack)
  static const BlockId sulfFire = reg().blockId("sulfuric_fire");
  for (int n = 0; n < 8 && B.fire; n++) {
    const int x = rnd.nextInt(16), z = rnd.nextInt(16);
    const u8 l = land(x, z);
    if (l != NB_VOLCANIC && l != NB_CRAG && l != NB_SULFUR) continue;
    int y = 0;
    if (!floorAt(x, z, SEA, 200, y)) continue;
    if (l == NB_SULFUR && sulfFire) { set(x, y - 1, z, B.nether_brimstone ? B.nether_brimstone : B.netherrack); set(x, y, z, sulfFire); }   // yellow, on brimstone
    else { set(x, y - 1, z, B.netherrack); set(x, y, z, B.fire); }
  }
}

} // namespace dq
