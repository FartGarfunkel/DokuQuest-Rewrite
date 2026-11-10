// gen/theend_be.cpp — the outer End as Better End 3.2.5 has it (github.com/quiqueck/BetterEnd, MIT; docs/CREDITS.md):
// each of the sixteen lands takes its Better End biome's ground (BE's surface rules: the turf, the moss/mycelium
// dither, the sulphuric bands, the painted mountains' strata, flavolite and violecite layers in the cliffs), its
// trees and big growths (mossy glowshrooms, pythadendrons, dragon trees, lucernias, umbrella trees, jellyshrooms,
// tenaneas, helix trees, lacugroves, amaranitas, aurora crystals, neon cacti, ice stars, sulphur springs, dragon bones)
// and its plants. Shapes are rebuilt from BE's feature recipes in voxels; the blocks are BE's (tools/port/betterend_blocks.py).
//   0 chorus forest   1 blossoming spires   2 foggy mushroomland   3 glowing grasslands   4 crystal mountains
//   5 sulphur springs 6 umbrella jungle     7 shadow forest        8 ice starfield        9 amber land
//  10 megalake       11 lantern woods      12 painted mountains   13 neon oasis          14 dragon graveyards
//  15 dry shrubland  16 dust wastelands  17 umbra valley  18 megalake grove; and BE's cave biomes in the thick islands
#include "gen/dimgens.h"
#include "gen/structures.h"
#include "core/registry.h"

namespace dq {
using namespace vmap;

namespace {
inline BlockId pickB(BlockId a, BlockId b) { return a ? a : b; }
}

// BE's split / threshold condition: OpenSimplex(seed 4141) at 0.1 plus a jitter of ±0.4
bool EndGen::beSplit(int x, int z) const {
  return nB.noise2(x * 0.1 + 413.1, z * 0.1 - 41.4) + (hashFloat(x, z, seed_, 4141) - 0.5) * 0.8 > 0;
}

void EndGen::beSurface(int land, int x, int z, const Isle& c, ChunkWriter& P) const {
  const int top = c.top;
  auto put = [&](int y, BlockId id) { if (id && y > c.bot && y < DIM_H) P.put(x, y, z, id); };
  const float rf = (float)hashFloat(x, top, z, 41);
  // flavolite and violecite lie in thin bands through the stone (BE's flavolite_layer / violecite_layer), seen in cliffs
  const BlockId layer = (land == 0 || land == 7) ? pickB(B.violecite, B.end_stone) : pickB(B.flavolite, B.end_stone);
  const double band = nC.noise2(x / 48.0 - 9.1, z / 48.0 + 3.3);
  if (band > 0.1) for (int y = top - 3; y > c.bot; y--) {
    int k = (int)floormod(y + (int)std::lround(band * 9), 19);
    if (k < 2) put(y, layer);
  }
  switch (land) {
    case 0: put(top, pickB(B.chorus_nylium, B.end_stone)); break;
    case 1: put(top, pickB(B.pink_moss, B.end_stone)); break;
    case 2: put(top, beSplit(x, z) ? pickB(B.end_mycelium, B.end_moss) : pickB(B.end_moss, B.end_stone)); break;
    case 3: put(top, pickB(B.end_moss, B.end_stone)); break;
    case 4: put(top, beSplit(x, z) && rf < 0.35f ? pickB(B.end_moss, B.end_stone) : pickB(B.crystal_moss, B.end_stone)); break;
    case 5: {   // SulphuricSurfaceNoise: end stone / flavolite / sulphuric rock / brimstone, five deep
      double v = nA.noise2(x * 0.03 + 77.0, z * 0.03 - 5.0) + nB.noise2(x * 0.1 - 3.0, z * 0.1 + 19.0) * 0.3 + (rf - 0.5) * 0.2;
      BlockId id = v < -0.6 ? B.end_stone : v < -0.3 ? pickB(B.flavolite, B.end_stone) : v < 0.5 ? pickB(B.sulphuric_rock, B.end_stone) : pickB(B.brimstone, B.end_stone);
      for (int y = top; y > top - 5; y--) put(y, id);
      break;
    }
    case 6: put(top, pickB(B.jungle_moss, B.end_stone)); break;
    case 7: put(top, pickB(B.shadow_grass, B.end_stone)); break;
    case 8: put(top, rf < 0.18f ? pickB(B.emerald_ice, B.packed_ice) : pickB(B.dense_snow, B.snow_block)); break;
    case 9: {
      put(top, pickB(B.amber_moss, B.end_stone));
      // amber ore lies thick under the moss (BE: 60 tries a chunk)
      for (int y = top - 1; y > top - 6; y--) if (hashFloat(x, y, z, 0xA3B) < 0.09) put(y, pickB(B.amber_ore, B.end_stone));
      break;
    }
    case 10: {
      const int waterY = 54 + END_DY;
      if (top >= waterY - 1) put(top, beSplit(x, z) ? pickB(B.endstone_dust, B.end_stone) : pickB(B.end_moss, B.end_stone));
      else {
        put(top, pickB(B.endstone_dust, B.end_stone));
        if (B.water) for (int y = top + 1; y <= waterY && y < DIM_H - 2; y++) P.put(x, y, z, B.water);
      }
      break;
    }
    case 11: case 15: put(top, pickB(B.rutiscus, B.end_stone)); break;
    case 12: {   // VerticalBandNoise: [end stone, flavolite, violecite][(y/4 + noise·4) mod 3], under a coat of dust
      for (int y = top - 1; y > top - 14 && y > c.bot; y--) {
        int idx = (int)std::floor(y / 4.0 + nA.noise2(x * 0.06 + y * 0.13, z * 0.06 - 2.0) * 4);
        int k = (int)floormod(idx, 3);
        put(y, k == 0 ? B.end_stone : k == 1 ? pickB(B.flavolite, B.end_stone) : pickB(B.violecite, B.end_stone));
      }
      put(top, pickB(B.endstone_dust, B.end_stone));
      break;
    }
    case 13: {
      BlockId t = beSplit(x, z) ? pickB(B.end_moss, B.end_stone) : pickB(B.endstone_dust, B.end_stone);
      put(top, t);
      if (t == B.endstone_dust) for (int y = top - 1; y > top - 5; y--) put(y, t);
      break;
    }
    case 14: put(top, pickB(B.sangnum, B.end_stone)); break;
    case 16: for (int y = top; y > top - 6; y--) put(y, pickB(B.endstone_dust, B.end_stone)); break;   // dunes of dust, five deep
    case 17: {   // UmbraSurfaceNoise: pallidium covers over umbralith
      double v = nA.noise2(x * 0.03 - 41.0, z * 0.03 + 7.0) + nB.noise2(x * 0.1 + 2.0, z * 0.1 - 9.0) * 0.3 + (rf - 0.5) * 0.2;
      BlockId topB = v > 0.4 ? B.pallidium_full : v > 0.15 ? B.pallidium_heavy : v > -0.15 ? B.pallidium_thin : v > -0.4 ? B.pallidium_tiny : B.umbralith;
      for (int y = top - 1; y > top - 6; y--) put(y, pickB(B.umbralith, B.end_stone));
      put(top, pickB(topB, pickB(B.umbralith, B.end_stone)));
      break;
    }
    case 18: put(top, pickB(B.end_moss, B.end_stone)); break;
    default: break;
  }
  // an end lake in the foggy mushroomland: the dent the island left filled with water
  if (land == 2) {
    double cd = craterDepth(x, z);
    if (cd > 0.5 && B.water) {
      int surf = top + (int)std::lround(cd) - 1;
      put(top, pickB(B.endstone_dust, B.end_stone));
      for (int y = top + 1; y <= surf && y < DIM_H - 2; y++) P.put(x, y, z, B.water);
    }
  }
}

// one tree or growth per cell of `cell` blocks (hash h), when the cell's spot is on land `want`
template <class F>
static void cells(int bx, int bz, int cell, int reach, u32 salt, i32 seed, F&& f) {
  for (int gz = jsFloor((bz - reach) / (double)cell); gz <= jsFloor((bz + 15 + reach) / (double)cell); gz++)
    for (int gx = jsFloor((bx - reach) / (double)cell); gx <= jsFloor((bx + 15 + reach) / (double)cell); gx++) {
      u32 h = hash32(gx, gz, seed, (i32)salt);
      int sx = gx * cell + (int)(h % (u32)cell), sz = gz * cell + (int)((h >> 8) % (u32)cell);
      f(h, sx, sz);
    }
}

void EndGen::beDress(int bx, int bz, ChunkWriter& P) const {
  auto set = [&](int x, int y, int z, BlockId id, bool keep = false) {
    if (!id || y <= 0 || y >= DIM_H - 1 || !P.in(x, z)) return;
    if (keep) { BlockId o = P.get(x, y, z); if (o && o != B.water) return; }
    P.put(x, y, z, id);
  };
  auto ball = [&](int cx, int cy, int cz, double r, BlockId id, bool keep = true, double squash = 1.0) {
    int R = (int)std::ceil(r);
    for (int dy = -R; dy <= R; dy++) for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      double d = dx * dx + dz * dz + (dy * squash) * (dy * squash);
      if (d <= r * r) set(cx + dx, cy + dy, cz + dz, id, keep);
    }
  };
  auto ground = [&](int x, int z, int want, Isle& c) { return island(x, z, c) && !c.main && endBiome(x, z) == want; };
  auto col = [&](int x, int z, int y0, int h, BlockId id) { for (int i = 0; i < h; i++) set(x, y0 + i, z, id); };
  // a trunk that leans as it climbs (BE's spline trunks): returns the top
  auto bentTrunk = [&](int x, int z, int y0, int h, BlockId id, u32 hh, int thick) {
    double ax = ((hh >> 3) & 7) / 7.0 - 0.5, az = ((hh >> 6) & 7) / 7.0 - 0.5;
    int lx = x, lz = z;
    for (int i = 0; i < h; i++) {
      double t = (double)i / std::max(1, h - 1);
      lx = x + (int)std::lround(ax * 4 * t * t); lz = z + (int)std::lround(az * 4 * t * t);
      set(lx, y0 + i, lz, id);
      if (thick > 1 && i < h * 2 / 3) { set(lx + 1, y0 + i, lz, id); set(lx, y0 + i, lz + 1, id); set(lx + 1, y0 + i, lz + 1, id); }
    }
    return std::array<int, 3>{lx, y0 + h, lz};
  };

  // ---- 2 foggy mushroomland: mossy glowshrooms (a leaning mossy log, a wide blue cap with glowing gills and fur) ----
  cells(bx, bz, 15, 12, 0x6105, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0 || !ground(sx, sz, 2, c)) return;
    if (craterDepth(sx, sz) > 0.5) return;
    bool big = (h & 12) == 0;
    int H = big ? 14 + (int)((h >> 12) % 8) : 7 + (int)((h >> 12) % 5);
    auto t = bentTrunk(sx, sz, c.top + 1, H, pickB(B.mossy_glowshroom_log, B.glowshroom_stem), h, big ? 2 : 1);
    double R = big ? 6.5 + ((h >> 16) & 3) : 3.5 + ((h >> 16) & 1);
    int Ri = (int)std::ceil(R);
    for (int dz = -Ri; dz <= Ri; dz++) for (int dx = -Ri; dx <= Ri; dx++) {
      double d = std::sqrt((double)(dx * dx + dz * dz));
      if (d > R) continue;
      int dome = (int)std::lround(std::sqrt(std::max(0.0, R * R - d * d)) * 0.42);
      int x = t[0] + dx, z = t[2] + dz, y = t[1] + dome - 1;
      set(x, y, z, pickB(B.mossy_glowshroom_cap, B.glowshroom_cap));
      if (d < R - 1.2) set(x, y - 1, z, pickB(B.mossy_glowshroom_hymenophore, B.shroomlight), true);
      else if (((h >> ((dx + dz) & 15)) & 1) && B.mossy_glowshroom_fur) { set(x, y - 1, z, B.mossy_glowshroom_fur, true); if ((h >> ((dx * 3 + dz) & 15)) & 1) set(x, y - 2, z, B.mossy_glowshroom_fur, true); }
    }
  });
  // ---- 0 chorus forest: pythadendrons (a purple trunk forking into branches, each ending in a ball of leaves) ----
  cells(bx, bz, 13, 10, 0x9171, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) > 1 || !ground(sx, sz, 0, c)) return;
    BlockId log = pickB(B.pythadendron_log, B.purpur_pillar), lv = pickB(B.pythadendron_leaves, B.chorus_flower);
    int H = 5 + (int)((h >> 10) % 4);
    col(sx, sz, c.top + 1, H, log);
    int y = c.top + H;
    int n = 2 + (int)((h >> 14) % 2);
    for (int i = 0; i < n; i++) {
      double a = ((h >> (17 + i * 3)) & 7) / 8.0 * 6.2832 + i * 2.1;
      int L = 3 + (int)((h >> (20 + i)) & 1) * 2;
      int ex = sx, ez = sz, ey = y;
      for (int s = 1; s <= L; s++) { ex = sx + (int)std::lround(std::cos(a) * s * 0.8); ez = sz + (int)std::lround(std::sin(a) * s * 0.8); ey = y + s; set(ex, ey, ez, log); }
      ball(ex, ey + 1, ez, 2.3, lv);
    }
  });
  // ---- 7 shadow forest: dragon trees (tall dark trunks, wide flat crowns) ----
  cells(bx, bz, 14, 12, 0xD7A6, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) > 1 || !ground(sx, sz, 7, c)) return;
    int H = 10 + (int)((h >> 10) % 7);
    auto t = bentTrunk(sx, sz, c.top + 1, H, pickB(B.dragon_tree_log, B.dragon_log), h, 1);
    BlockId lv = pickB(B.dragon_tree_leaves, B.dragon_leaves);
    for (int k = 0; k < 3; k++) {
      double R = 5.5 - k * 1.6;
      int Ri = (int)std::ceil(R);
      for (int dz = -Ri; dz <= Ri; dz++) for (int dx = -Ri; dx <= Ri; dx++)
        if (dx * dx + dz * dz <= R * R && hashFloat(t[0] + dx, t[1] + k, t[2] + dz, 7) < 0.9) set(t[0] + dx, t[1] - 1 + k, t[2] + dz, lv, true);
    }
    // roots
    for (int k = 0; k < 4; k++) { int dx = DIRX[k], dz = DIRZ[k]; set(sx + dx, c.top + 1, sz + dz, pickB(B.dragon_tree_log, B.dragon_log), true); }
  });
  // ---- 11 lantern woods (and 15's bushes): lucernias with filalux hanging under the crown ----
  cells(bx, bz, 12, 9, 0x1C57, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0) return;
    if (!island(sx, sz, c) || c.main) return;
    int land = endBiome(sx, sz);
    if (land != 11 && !(land == 15 && (h & 12) == 0)) return;
    bool bush = land == 15;
    int H = bush ? 1 : 5 + (int)((h >> 10) % 4);
    col(sx, sz, c.top + 1, H, pickB(B.lucernia_log, B.lantern_log));
    int y = c.top + 1 + H;
    ball(sx, y, sz, bush ? 1.6 : 3.2, pickB(B.lucernia_leaves, B.leaves), true, 1.3);
    if (!bush && B.filalux) for (int k = 0; k < 6; k++) {
      int dx = (int)((h >> (k * 2)) % 5) - 2, dz = (int)((h >> (k * 2 + 10)) % 5) - 2;
      int len = 2 + (int)((h >> (k + 20)) & 3);
      for (int j = 1; j <= len; j++) set(sx + dx, y - 3 - j, sz + dz, B.filalux, true);
    }
  });
  // ---- 6 umbrella jungle: umbrella trees (domed membranes, glowing clusters under the rim) and jellyshrooms ----
  cells(bx, bz, 15, 12, 0x0B32, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if (!ground(sx, sz, 6, c)) return;
    if (h & 1) {   // umbrella tree
      int H = 9 + (int)((h >> 10) % 6);
      auto t = bentTrunk(sx, sz, c.top + 1, H, pickB(B.umbrella_tree_log, B.dragon_log), h, 1);
      BlockId mem = pickB(B.umbrella_tree_membrane, B.umbrella_cap);
      double R = 4.5 + ((h >> 14) & 3) * 0.6;
      int Ri = (int)std::ceil(R);
      for (int dz = -Ri; dz <= Ri; dz++) for (int dx = -Ri; dx <= Ri; dx++) {
        double d = std::sqrt((double)(dx * dx + dz * dz)); if (d > R) continue;
        int y = t[1] + (int)std::lround((1 - d / R) * 2.2);
        set(t[0] + dx, y, t[2] + dz, mem);
        if (d > R - 1.3 && ((h >> ((dx * 5 + dz) & 15)) & 1) && B.umbrella_tree_cluster) set(t[0] + dx, y - 1, t[2] + dz, B.umbrella_tree_cluster, true);
      }
    } else {       // jellyshroom
      int H = 3 + (int)((h >> 10) % 4);
      col(sx, sz, c.top + 1, H, pickB(B.jellyshroom_log, B.glowshroom_stem));
      int y = c.top + 1 + H;
      double R = 2.4 + ((h >> 14) & 1);
      int Ri = (int)std::ceil(R);
      for (int dy = 0; dy <= Ri; dy++) for (int dz = -Ri; dz <= Ri; dz++) for (int dx = -Ri; dx <= Ri; dx++) {
        double d = dx * dx + dz * dz + dy * dy * 2.2;
        if (d <= R * R && d >= (R - 1.1) * (R - 1.1)) set(sx + dx, y + dy - 1, sz + dz, pickB(B.jellyshroom_cap_purple, B.umbrella_cap), true);
      }
    }
  });
  // ---- 1 blossoming spires: tenaneas (a pink crown with glowing flowers hanging from it) ----
  cells(bx, bz, 14, 10, 0xB105, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0 || !ground(sx, sz, 1, c)) return;
    int H = 7 + (int)((h >> 10) % 5);
    auto t = bentTrunk(sx, sz, c.top + 1, H, pickB(B.tenanea_log, B.purpur_pillar), h, 1);
    ball(t[0], t[1], t[2], 3.3, pickB(B.tenanea_leaves, B.blossom_cap), true, 1.4);
    if (B.tenanea_flowers) for (int k = 0; k < 7; k++) {
      int dx = (int)((h >> (k * 2)) % 7) - 3, dz = (int)((h >> (k * 2 + 9)) % 7) - 3;
      int len = 1 + (int)((h >> (k + 18)) & 3);
      for (int j = 1; j <= len; j++) set(t[0] + dx, t[1] - 3 - j, t[2] + dz, B.tenanea_flowers, true);
    }
  });
  // ---- 9 amber land: helix trees (a trunk that winds as it rises, a crown of helix leaves) ----
  cells(bx, bz, 16, 10, 0x4E11, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) > 1 || !ground(sx, sz, 9, c)) return;
    int H = 10 + (int)((h >> 10) % 8);
    BlockId log = pickB(B.helix_tree_log, B.log);
    double ph = ((h >> 14) & 7) * 0.8;
    int x = sx, z = sz;
    for (int i = 0; i < H; i++) {
      double a = ph + i * 0.55; x = sx + (int)std::lround(std::cos(a) * 1.6); z = sz + (int)std::lround(std::sin(a) * 1.6);
      set(x, c.top + 1 + i, z, log); set(sx, c.top + 1 + i, sz, log);
    }
    ball(sx, c.top + 1 + H, sz, 3.0, pickB(B.helix_tree_leaves, B.leaves), true, 1.5);
  });
  // ---- 10 megalake (its shores) and 18 megalake grove (all over): lacugroves (a trunk in a cone of leaves) ----
  cells(bx, bz, 11, 8, 0x1AC6, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if (!island(sx, sz, c) || c.main) return;
    const int land = endBiome(sx, sz);
    if (land == 10) { if ((h & 3) > 1 || c.top < 54 + END_DY - 1) return; }
    else if (land != 18 || (h & 3) == 0) return;
    int H = 6 + (int)((h >> 10) % 6);
    col(sx, sz, c.top + 1, H + 1, pickB(B.lacugrove_log, B.log));
    BlockId lv = pickB(B.lacugrove_leaves, B.leaves);
    for (int i = 0; i < H; i++) {
      double R = 0.6 + (H - i) * 0.42;
      int y = c.top + 3 + i, Ri = (int)std::ceil(R);
      for (int dz = -Ri; dz <= Ri; dz++) for (int dx = -Ri; dx <= Ri; dx++) if (dx * dx + dz * dz <= R * R) set(sx + dx, y, sz + dz, lv, true);
    }
  });
  // ---- 14 dragon graveyards: amaranitas (a hyphae stem, a red cap, lanterns and fur beneath), bone ribs, obsidian ----
  cells(bx, bz, 18, 12, 0xA3A, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if (!ground(sx, sz, 14, c)) return;
    int kind = h & 3;
    if (kind == 0) {   // a large amaranita
      int H = 5 + (int)((h >> 10) % 4);
      col(sx, sz, c.top + 1, H, pickB(B.amaranita_hyphae, B.bone_block));
      int y = c.top + 1 + H;
      double R = 3.4;
      for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++) {
        double d = std::sqrt((double)(dx * dx + dz * dz)); if (d > R) continue;
        set(sx + dx, y + (d < 2 ? 1 : 0), sz + dz, pickB(B.amaranita_cap, B.red_mushroom_block));
        if (d > R - 1.1) set(sx + dx, y - 1, sz + dz, (dx + dz) & 1 ? pickB(B.amaranita_lantern, B.shroomlight) : pickB(B.amaranita_fur, B.amaranita_cap), true);
      }
    } else if (kind == 1) {   // a dragon's rib
      int R = 4 + (int)((h >> 10) % 3); bool alongX = (h >> 14) & 1;
      for (int t = -R; t <= R; t++) {
        double u = (double)t / R; int rise = (int)std::lround((1.0 - u * u) * (R + 2));
        int x = sx + (alongX ? t : 0), z = sz + (alongX ? 0 : t);
        set(x, c.top + 1 + rise, z, pickB(B.dragon_bone_block, B.bone_block));
        if (rise > 1) set(x, c.top + rise, z, pickB(B.dragon_bone_block, B.bone_block));
      }
    } else if (kind == 2) {   // an obsidian boulder
      ball(sx, c.top + 1, sz, 1.8 + ((h >> 12) & 1), B.obsidian, false, 1.2);
    }
  });
  // ---- 4 crystal mountains: aurora crystals in needles ----
  cells(bx, bz, 12, 6, 0xC757, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0 || !ground(sx, sz, 4, c)) return;
    int H = 5 + (int)((h >> 10) % 12);
    double lean = ((h >> 20) & 7) / 7.0 - 0.5;
    for (int dy = 0; dy <= H; dy++) {
      int rad = dy > H - 3 ? 0 : dy > H / 2 ? 1 : 2;
      int ox = (int)std::lround(lean * dy * 0.35);
      for (int dz = -rad; dz <= rad; dz++) for (int dx = -rad; dx <= rad; dx++) if (dx * dx + dz * dz <= rad * rad + (rad > 0)) set(sx + dx + ox, c.top + dy, sz + dz, pickB(B.aurora_crystal, B.amethyst_block));
    }
  });
  // ---- 13 neon oasis: neon cacti ----
  cells(bx, bz, 9, 2, 0x0E0A, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0 || !ground(sx, sz, 13, c)) return;
    int H = 2 + (int)((h >> 8) % 4);
    col(sx, sz, c.top + 1, H, pickB(B.neon_cactus, B.sea_lantern));
    if (H > 3) { int dx = (h >> 12) & 1 ? 1 : -1; set(sx + dx, c.top + 3, sz, pickB(B.neon_cactus, B.sea_lantern), true); set(sx + dx, c.top + 4, sz, pickB(B.neon_cactus, B.sea_lantern), true); }
  });
  // ---- 3 glowing grasslands: glowing pillars (a blue vine stem under a luminophor) ----
  cells(bx, bz, 10, 2, 0x6107, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 7) > 2 || !ground(sx, sz, 3, c)) return;
    int H = 1 + (int)((h >> 8) % 3);
    col(sx, sz, c.top + 1, H, pickB(B.blue_vine, B.end_rod));
    set(sx, c.top + 1 + H, sz, pickB(B.glowing_pillar_luminophor, B.blue_vine_lantern), true);
  });
  // ---- 8 ice starfield: ice stars of dense snow and emerald ice, drifting over the shelves ----
  cells(bx, bz, 22, 6, 0x1CE5, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) > 1 || !ground(sx, sz, 8, c)) return;
    int y = c.top + 8 + (int)((h >> 12) % 12);
    if (y >= DIM_H - 6) return;
    int arm = 2 + (int)((h >> 16) % 3);
    BlockId core = pickB(B.ancient_emerald_ice, B.blue_ice), mid = pickB(B.dense_emerald_ice, B.packed_ice), tip = pickB(B.emerald_ice, B.ice);
    set(sx, y, sz, core);
    static const int A[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto& a : A) for (int d = 1; d <= arm; d++) set(sx + a[0] * d, y + a[1] * d, sz + a[2] * d, d == arm ? tip : mid, true);
  });
  // ---- 17 umbra valley: umbralith arches (a bow of stone between two feet) ----
  cells(bx, bz, 26, 14, 0x0A7C, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) != 0 || !ground(sx, sz, 17, c)) return;
    const int R = 5 + (int)((h >> 10) % 5); const bool alongX = (h >> 15) & 1; const double th = 1.2 + ((h >> 17) & 1) * 0.6;
    for (int t = -R; t <= R; t++) {
      const double u = (double)t / R; const int rise = (int)std::lround(std::sqrt(std::max(0.0, 1 - u * u)) * (R + 3));
      for (int k = -1; k <= 1; k++) {
        const int x = sx + (alongX ? t : k), z = sz + (alongX ? k : t);
        for (int dy = 0; dy < th + 1; dy++) set(x, c.top + rise + dy, z, pickB(B.umbralith, B.end_stone));
        if (std::abs(t) >= R - 1) for (int y = c.top; y < c.top + rise; y++) set(x, y, z, pickB(B.umbralith, B.end_stone));
      }
    }
  });
  // ---- BE's cave biomes in the thick islands: lush aurora, smaragdant, jade, and plain end caves ----
  cells(bx, bz, 24, 12, 0xCA7E, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 1) || !island(sx, sz, c) || c.main || c.top - c.bot < 16) return;
    const int kind = (int)((h >> 4) % 4);   // 0 lush aurora, 1 smaragdant, 2 jade, 3 empty end cave
    const double rx = 5 + ((h >> 8) & 3), rz = 5 + ((h >> 10) & 3), ry = 3 + ((h >> 12) & 1);
    const int cy = c.top - (int)((c.top - c.bot) * 0.55);
    const int R = (int)std::ceil(std::max(rx, rz));
    for (int dz = -R; dz <= R; dz++) for (int dx = -R; dx <= R; dx++) {
      const int x = sx + dx, z = sz + dz;
      if (!P.in(x, z)) continue;
      Isle o; if (!island(x, z, o) || o.main) continue;
      for (int dy = -(int)ry; dy <= (int)ry; dy++) {
        const double e = (dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) + (dy * dy) / (ry * ry);
        const int y = cy + dy;
        if (e > 1 || y <= o.bot + 2 || y >= o.top - 3) continue;
        P.put(x, y, z, AIR);
      }
      // dress the pocket: floors, ceilings and walls of its kind
      for (int y = cy - (int)ry - 1; y <= cy + (int)ry + 1; y++) {
        if (y <= o.bot || y >= o.top || P.get(x, y, z) == AIR) continue;
        const bool floor = P.get(x, y + 1, z) == AIR, ceil = P.get(x, y - 1, z) == AIR;
        if (!floor && !ceil) {
          if (kind == 2) { const int band = (int)floormod(y + (int)std::lround(nA.noise2(x * 0.2, z * 0.2) * 1.5), 3);
            P.put(x, y, z, band == 0 ? pickB(B.virid_jadestone, B.end_stone) : band == 1 ? pickB(B.azure_jadestone, B.end_stone) : pickB(B.sandy_jadestone, B.end_stone)); }
          continue;
        }
        const double r = hashFloat(x, y, z, 0xCA71);
        if (floor) {
          if (kind == 0) { P.put(x, y, z, pickB(B.cave_moss, B.end_stone)); if (r < 0.3) set(x, y + 1, z, B.cave_grass, true); else if (r < 0.34) set(x, y + 1, z, B.aurora_crystal, true); }
          else if (kind == 1) { if (r < 0.15) set(x, y + 1, z, B.smaragdant_crystal_shard, true); else if (r < 0.18) { set(x, y + 1, z, B.smaragdant_crystal, true); set(x, y + 2, z, B.smaragdant_crystal, true); } }
          else if (kind == 2) P.put(x, y, z, pickB(B.azure_jadestone, B.end_stone));
        }
        if (ceil) {
          if (kind == 0) P.put(x, y, z, pickB(B.cave_moss, B.end_stone));
          if (r < (kind == 3 ? 0.18 : 0.08)) set(x, y - 1, z, B.end_stone_stalactite, true);
        }
      }
    }
  });
  // ---- 5 sulphur springs: hot pools (water ringed by brimstone) and vents ----
  cells(bx, bz, 16, 4, 0x5057, seed_, [&](u32 h, int sx, int sz) {
    Isle c; if ((h & 3) == 0 || !ground(sx, sz, 5, c)) return;
    int R = 2 + (int)((h >> 10) % 3);
    for (int dz = -R - 1; dz <= R + 1; dz++) for (int dx = -R - 1; dx <= R + 1; dx++) {
      int d2 = dx * dx + dz * dz;
      if (d2 > (R + 1) * (R + 1)) continue;
      int x = sx + dx, z = sz + dz;
      Isle o; if (!island(x, z, o) || o.top != c.top) continue;
      if (d2 <= R * R && B.water) { set(x, c.top, z, B.water); set(x, c.top - 1, z, pickB(B.brimstone, B.end_stone)); }
      else set(x, c.top, z, pickB(B.brimstone, B.end_stone));
    }
    if ((h >> 14) & 1) { set(sx + R + 2, c.top + 1, sz, pickB(B.steam_vent, B.magma_block), true); }
  });
}

// each land's plants (BE's L0-N scatters): on top of its turf, where the air is free
void EndGen::bePlants(int land, int x, int z, const Isle& c, ChunkWriter& P) const {
  const int y = c.top + 1;
  if (y >= DIM_H - 2 || P.get(x, y, z) != AIR) return;
  BlockId under = P.get(x, c.top, z);
  if (!under || under == B.water) {
    // under water: charnia on the lake floors (megalake, foggy lakes, sulphur pools)
    if (under == B.water && (land == 10 || land == 2 || land == 5)) {
      int fy = c.top; while (fy > c.bot && P.get(x, fy, z) == B.water) fy--;
      if (P.get(x, fy + 1, z) == B.water && hashFloat(x, fy, z, 0xC4A) < 0.12) {
        BlockId ch = land == 5 ? pickB(B.charnia_green, B.charnia_orange) : ((x ^ z) & 1) ? B.charnia_cyan : B.charnia_purple;
        if (ch) P.put(x, fy + 1, z, ch);
      }
    }
    return;
  }
  const float r = (float)hashFloat(x, y, z, 0xBE71);
  auto any = [&](std::initializer_list<BlockId> l) { BlockId out = 0; int n = 0; for (BlockId b : l) if (b) n++; if (!n) return out; int k = (int)(hashFloat(z, x, y, 0xBE72) * n); for (BlockId b : l) if (b && k-- == 0) return b; return out; };
  BlockId put = 0;
  switch (land) {
    case 0: if (r < 0.12f) put = B.chorus_grass; else if (r < 0.14f) put = B.tail_moss; break;
    case 1: if (r < 0.14f) put = B.bushy_grass; else if (r < 0.17f) put = B.twisted_moss; break;
    case 2: if (r < 0.08f) put = B.umbrella_moss; else if (r < 0.14f) put = B.creeping_moss; else if (r < 0.17f) put = B.cyan_moss; break;
    case 3: if (r < 0.16f) put = any({B.blooming_cooksonia, B.salteago, B.vaiolush_fern, B.fracturn}); else if (r < 0.19f) put = any({B.umbrella_moss, B.creeping_moss}); break;
    case 4: if (r < 0.09f && under == B.crystal_moss) put = B.crystal_grass; break;
    case 6: if (r < 0.13f) put = B.jungle_grass_be; else if (r < 0.18f) put = B.twisted_umbrella_moss; else if (r < 0.21f) put = B.cyan_moss; break;
    case 7: if (r < 0.09f) put = B.shadow_plant; else if (r < 0.12f) put = B.murkweed; else if (r < 0.15f) put = B.needlegrass; break;
    case 9: if (r < 0.15f) put = B.amber_grass; break;
    case 10: if (r < 0.06f) put = B.umbrella_moss; else if (r < 0.11f) put = B.creeping_moss; break;
    case 11: if (r < 0.06f) put = B.aeridium; else if (r < 0.12f) put = B.lamellarium; break;
    case 13: if (under == B.end_moss) { if (r < 0.07f) put = B.umbrella_moss; else if (r < 0.11f) put = B.creeping_moss; } break;
    case 14: if (r < 0.06f) put = B.globulagus; else if (r < 0.11f) put = B.clawfern; break;
    case 15: if (r < 0.2f) put = any({B.orango, B.aeridium, B.lutebus, B.lamellarium}); break;
    case 17: if (r < 0.14f && under != B.umbralith) put = B.inflexia; break;
    case 18: if (r < 0.06f) put = B.umbrella_moss; else if (r < 0.11f) put = B.creeping_moss; else if (r < 0.13f) put = B.bushy_grass; break;
    default: break;
  }
  if (put && reg().tSolid[under]) P.put(x, y, z, put);
}

} // namespace dq
