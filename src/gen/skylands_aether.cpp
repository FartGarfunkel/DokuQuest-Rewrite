// gen/skylands_aether.cpp — terrain version 7: the Skylands as The Aether makes its sky (the mod's worldgen data and
// features, rebuilt here from their numbers; no art or code of the mod's is used):
//   - islands from the Aether's 3D density (gen/aether_noise.h), Aether y 18-105 lifted AETHER_DY: lumpy terraced
//     land 100-500 blocks across with bays, holes and arches, stacked now and then, no caves
//   - every floor: Aether grass over about three blocks of Aether dirt, holystone below; ceilings bare holystone
//   - skyroot trees (the oak's shape) and 1 % golden oaks (ten logs, branches, a ragged ball of gold leaves), as many as
//     the land asks (forest 3, woodland 2.5, grove 1, meadow 0.5 a chunk); a crystal tree on its islet 1 chunk in 50
//   - no aerclouds (the mod's cold, blue and golden cloud chains and ribbons are left out: only the islands are land)
//   - quicksoil aprons under thin low rims, ponds, ores (aether dirt, icestone, ambrosium, zanite, gravitite) and the
//     ground cover: dense grass, white and purple flowers, berry bushes
#include "gen/dimgens.h"
#include "gen/aether_noise.h"
#include "core/registry.h"
#include "core/itemstack.h"

namespace dq {
using namespace vmap;

namespace {
struct AB {
  BlockId grass, dirt, holy, mossy, quick, ice, skyLog, skyLeaf, goldLog, goldLeaf, crystalLeaf, crystalFruit, ambro, zanite, gravi,
          cold, blue, gold, white, purple, berry, tallGrass, water, carved, sentry, chest;
};
const AB& ab() {
  static const AB a = [] {
    auto id = [](const char* k, const char* alt = nullptr) { BlockId b = reg().blockId(k); return b || !alt ? b : reg().blockId(alt); };
    AB r;
    r.grass = id("aether_grass_block", "sky_grass"); r.dirt = id("aether_dirt", "dirt"); r.holy = id("holystone", "skystone");
    r.mossy = id("mossy_holystone", "holystone"); r.quick = id("quicksoil", "glimmersand"); r.ice = id("icestone", "packed_ice");
    r.skyLog = id("skyroot_log", "birch_log"); r.skyLeaf = id("skyroot_leaves", "birch_leaves");
    r.goldLog = id("golden_oak_log", "skyroot_log"); r.goldLeaf = id("golden_oak_leaves", "skyroot_leaves");
    r.crystalLeaf = id("crystal_leaves", "skyroot_leaves"); r.crystalFruit = id("crystal_fruit_leaves");
    r.ambro = id("ambrosium_ore"); r.zanite = id("zanite_ore"); r.gravi = id("gravitite_ore");
    r.cold = id("cold_aercloud", "cloud_puff"); r.blue = id("blue_aercloud", "bounce_cloud"); r.gold = id("golden_aercloud", "sun_cloud");
    r.white = id("white_flower", "oxeye_daisy"); r.purple = id("purple_flower", "allium"); r.berry = id("berry_bush");
    r.tallGrass = id("tall_grass"); r.water = id("water");
    r.carved = id("carved_stone", "stone_bricks"); r.sentry = id("sentry_stone", "sea_lantern"); r.chest = id("chest");
    return r;
  }();
  return a;
}
inline double lerpd(double t, double a, double b) { return a + t * (b - a); }
}

// the density at a block from the corner columns round it (the Aether interpolates between cell corners)
static double aetherAt(const double* c00, const double* c10, const double* c01, const double* c11, double fx, double fz, int y) {
  if (y <= 8 || y >= 128) return -1;
  const int cy = y >> 2; const double fy = (y & 3) / 4.0;
  auto at = [&](const double* c, int i) { return c[std::min(i, AetherNoise::CY - 1)]; };
  const double a = lerpd(fx, at(c00, cy), at(c10, cy)), b = lerpd(fx, at(c01, cy), at(c11, cy));
  const double a1 = lerpd(fx, at(c00, cy + 1), at(c10, cy + 1)), b1 = lerpd(fx, at(c01, cy + 1), at(c11, cy + 1));
  return lerpd(fy, lerpd(fz, a, b), lerpd(fz, a1, b1));
}

bool SkylandsGen::aetherColumn(int wx, int wz, int& top, int& bottom) const {
  const int x0 = floordiv(wx, 8) * 8, z0 = floordiv(wz, 8) * 8;
  const double fx = (wx - x0) / 8.0, fz = (wz - z0) / 8.0;
  const double *c00 = aether_->cornerColumn(x0, z0), *c10 = aether_->cornerColumn(x0 + 8, z0);
  const double *c01 = aether_->cornerColumn(x0, z0 + 8), *c11 = aether_->cornerColumn(x0 + 8, z0 + 8);
  top = -1; bottom = -1;
  for (int y = 120; y > 8; y--) if (aetherAt(c00, c10, c01, c11, fx, fz, y) > 0) { top = y; break; }
  if (top < 0) return false;
  for (int y = 9; y <= top; y++) if (aetherAt(c00, c10, c01, c11, fx, fz, y) > 0) { bottom = y; break; }
  top += AETHER_DY; bottom += AETHER_DY;
  return true;
}

void SkylandsGen::generateAether(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  for (int i = 0; i < 256; i++) { ch.climate[i * 2] = 160; ch.climate[i * 2 + 1] = 190; }
  const AB& K = ab();
  const int bx = ch.cx * CS, bz = ch.cz * CS, DY = AETHER_DY;
  auto inb = [](int x, int y, int z) { return x >= 0 && x < 16 && z >= 0 && z < 16 && y > 0 && y < DIM_H - 1; };
  auto at = [&](int x, int y, int z) -> BlockId { return inb(x, y, z) ? b.blocks[colIdx(x, y, z)] : (BlockId)0xFFFF; };
  auto put = [&](int x, int y, int z, BlockId id) { if (id && inb(x, y, z)) b.blocks[colIdx(x, y, z)] = id; };
  auto putAir = [&](int x, int y, int z, BlockId id) { if (id && inb(x, y, z) && b.blocks[colIdx(x, y, z)] == AIR) b.blocks[colIdx(x, y, z)] = id; };

  // ---- the islands: holystone wherever the density is above 0 ----
  const double* C[3][3];
  for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) C[i][k] = aether_->cornerColumn(bx + i * 8, bz + k * 8);
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int ix = x >> 3, iz = z >> 3; const double fx = (x & 7) / 8.0, fz = (z & 7) / 8.0;
    for (int y = 9; y < 128; y++)
      if (aetherAt(C[ix][iz], C[ix + 1][iz], C[ix][iz + 1], C[ix + 1][iz + 1], fx, fz, y) > 0) b.blocks[colIdx(x, y + DY, z)] = K.holy;
  }
  // ---- every floor: Aether grass, then about three blocks of Aether dirt (vanilla's surface depth) ----
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = bx + x, wz = bz + z;
    const int depth = (int)(nHill.noise2(wx / 64.0 + 17.0, wz / 64.0 - 3.0) * 2.75 + 3 + hashFloat(wx, wz, seed_, 0xD1) * 0.25);
    int left = -1;
    for (int y = 128 + DY; y > DY; y--) {
      BlockId& id = b.blocks[colIdx(x, y, z)];
      if (id != K.holy) { left = -1; continue; }
      if (b.blocks[colIdx(x, y + 1, z)] == AIR && left < 0) { id = K.grass; left = std::max(0, depth); continue; }
      if (left > 0) { id = K.dirt; left--; }
    }
  }

  Mulberry32 rnd(hash32(seed_, ch.cx, ch.cz, 0xAE01));
  auto ri = [&](int n) { return n <= 0 ? 0 : (int)(rnd() * n); };
  // ---- ores (vanilla OreFeature blobs in holystone) ----
  auto vein = [&](BlockId id, int size, int count, int y0, int y1) {
    if (!id) return;
    for (int c = 0; c < count; c++) {
      double x = ri(16), y = y0 + ri(y1 - y0), z = ri(16);
      for (int s = 0; s < size; s++) {
        const int ix = jsFloor(x), iy = jsFloor(y) + DY, iz = jsFloor(z);
        if (at(ix, iy, iz) == K.holy) put(ix, iy, iz, id);
        x += rnd() * 2 - 1; y += rnd() * 1.4 - 0.7; z += rnd() * 2 - 1;
      }
    }
  };
  vein(K.dirt, 33, 20, 0, 128); vein(K.ice, 32, 10, 0, 128); vein(K.ambro, 16, 20, 0, 128);
  vein(K.zanite, 5, 14, 0, 75); vein(K.gravi, 3, 5, 0, 74); vein(K.gravi, 4, 7, 0, 30);

  // ---- quicksoil aprons under one-block grass rims low on the islands (1 chunk in 5) ----
  if (rnd() < 0.2 && K.quick) {
    std::vector<std::pair<int, int>> spots;
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++)
      for (int y = 18; y < 48; y++) if (at(x, y + DY, z) == AIR && at(x, y + DY + 1, z) == K.grass && at(x, y + DY + 2, z) == AIR) { spots.push_back({x * 256 + z, y}); break; }
    for (auto& [xz, y] : spots) {
      const int sx = xz >> 8, sz = xz & 255;
      for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (dx * dx + dz * dz <= 12) putAir(sx + dx, y + DY, sz + dz, K.quick);
    }
  }

  // (no aerclouds: the islands are the only land in the sky. Clouds are the cloud layer's picture, not blocks to stand
  // on, so nothing up here is solid but the islands.)

  // ---- ponds (1 chunk in 15): a bowl of water in flat grass ----
  if (rnd() < 1 / 15.0 && K.water) {
    const int cx0 = 5 + ri(6), cz0 = 5 + ri(6);
    int sy = -1;
    for (int y = 128 + DY; y > DY; y--) if (at(cx0, y, cz0) == K.grass) { sy = y; break; }
    if (sy > 0) {
      const double rx = 2.5 + rnd() * 2, rz = 2.5 + rnd() * 2;
      for (int dz = -5; dz <= 5; dz++) for (int dx = -5; dx <= 5; dx++) {
        if ((dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) > 1) continue;
        const int x = cx0 + dx, z = cz0 + dz;
        if (at(x, sy, z) != K.grass || at(x + 1, sy, z) == AIR || at(x - 1, sy, z) == AIR || at(x, sy, z + 1) == AIR || at(x, sy, z - 1) == AIR) continue;
        put(x, sy, z, K.water);
        if ((dx * dx) / (rx * rx) + (dz * dz) / (rz * rz) < 0.4 && at(x, sy - 1, z) != AIR) put(x, sy - 1, z, K.water);
      }
    }
  }

  // ---- trees: skyroot (the oak's shape), 1 % golden oak ----
  auto floorTop = [&](int x, int z, int layer) {
    int found = 0;
    for (int y = 128 + DY; y > DY; y--)
      if (at(x, y, z) == K.grass && at(x, y + 1, z) == AIR && at(x, y + 2, z) == AIR && at(x, y + 3, z) == AIR && at(x, y + 4, z) == AIR)
        if (found++ == layer) return y + 1;
    return -1;
  };
  auto skyroot = [&](int x, int y, int z) {
    const int H = 4 + ri(3);
    for (int k = 0; k < H; k++) put(x, y + k, z, K.skyLog);
    for (int ly = H - 3; ly <= H; ly++) {
      const int r = ly >= H - 1 ? 1 : 2;
      for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        const bool corner = std::abs(dx) == r && std::abs(dz) == r;
        if (corner && (ly == H || rnd() < 0.5)) continue;
        putAir(x + dx, y + ly, z + dz, K.skyLeaf);
      }
    }
  };
  auto goldenOak = [&](int x, int y, int z) {
    for (int k = 0; k < 10; k++) put(x, y + k, z, K.goldLog);
    for (int i = 5; i <= 8; i++) {
      if (rnd() > 0.667) continue;
      const int sx = ri(3) - 1, sz = ri(3) - 1, len = rnd() < 0.5 ? 1 : 2, slant = i / 4 - 1;
      for (int s = 1; s <= len; s++) put(x + sx * s, y + i + slant * s, z + sz * s, K.goldLog);
    }
    for (int row = 4; row <= 11; row++) for (int dz = -4; dz <= 4; dz++) for (int dx = -4; dx <= 4; dx++)
      if (dx * dx + (row - 8) * (row - 8) + dz * dz <= 12 + ri(5)) putAir(x + dx, y + row, z + dz, K.goldLeaf);
  };
  const int land = skyBiome(bx + 8, bz + 8);
  const double perChunk = land == 1 ? 3.05 : land == 3 ? 2.55 : land == 2 ? 1.05 : 0.5;
  for (int layer = 0; layer < 2; layer++) {
    int n = (int)perChunk + (rnd() < perChunk - (int)perChunk ? 1 : 0);
    if (layer == 1) n = n / 2;
    for (int t = 0; t < n; t++) {
      const bool gold = rnd() < 0.01;
      const int x = gold ? 4 + ri(8) : 2 + ri(12), z = gold ? 4 + ri(8) : 2 + ri(12);
      const int y = floorTop(x, z, layer);
      if (y < 0) continue;
      if (gold) goldenOak(x, y, z); else skyroot(x, y, z);
    }
  }
  // ---- a crystal tree on its islet, floating (1 chunk in 50) ----
  if (rnd() < 0.02) {
    const int x = 6 + ri(4), z = 6 + ri(4), y = 32 + ri(64) + DY;
    bool clear = true;
    for (int dy = -2; dy <= 8 && clear; dy++) for (int dz = -2; dz <= 2 && clear; dz++) for (int dx = -2; dx <= 2; dx++) if (at(x + dx, y + dy, z + dz) != AIR) { clear = false; break; }
    if (clear) {
      for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
        const int m = std::abs(dx) + std::abs(dz);
        if (m <= 2) { put(x + dx, y, z + dz, K.grass); put(x + dx, y - 1, z + dz, K.holy); }
        if (m <= 1) put(x + dx, y - 2, z + dz, K.holy);
      }
      for (int k = 1; k <= 7; k++) put(x, y + k, z, K.skyLog);
      for (int dy : {3, 6}) for (int k = 0; k < 4; k++) putAir(x + DIRX[k], y + dy, z + DIRZ[k], K.skyLog);
      auto layerShape = [&](int dy, int kind) {
        for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
          const int m = std::abs(dx) + std::abs(dz);
          bool in = kind == 0 ? m == 0 : kind == 1 ? m <= 1 : kind == 2 ? m <= 2 : !(std::abs(dx) == 2 && std::abs(dz) == 2);
          if (in) putAir(x + dx, y + dy, z + dz, K.crystalFruit && (hash32(x + dx, y + dy, z + dz, 0xC4F7) & 7) == 0 ? K.crystalFruit : K.crystalLeaf);
        }
      };
      layerShape(8, 0); layerShape(7, 1); layerShape(6, 2); layerShape(5, 1); layerShape(4, 2); layerShape(3, 3);
    }
  }
  // ---- ground cover: dense grass, patches of white and purple flowers, berry bushes ----
  const bool whiteP = rnd() < 0.125, purpleP = rnd() < 0.0625, berryP = rnd() < 0.125;
  const int fx = ri(16), fz = ri(16);
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    for (int y = 128 + DY; y > DY; y--) {
      if (at(x, y, z) != K.grass || at(x, y + 1, z) != AIR) continue;
      const double r = hashFloat(bx + x, y, bz + z, seed_ ^ 0xAE5);
      const int d2 = (x - fx) * (x - fx) + (z - fz) * (z - fz);
      if (whiteP && d2 < 30 && r < 0.3) put(x, y + 1, z, K.white);
      else if (purpleP && d2 < 30 && r > 0.7) put(x, y + 1, z, K.purple);
      else if (berryP && d2 < 18 && r < 0.12 && K.berry) put(x, y + 1, z, K.berry);
      else if (r < 0.32 && K.tallGrass) put(x, y + 1, z, K.tallGrass);
    }
  }
  // ---- springs: water breaking out of the island walls, falling off the sides into the sky (30 tries a chunk) ----
  for (int n = 0; n < 30 && K.water; n++) {
    const int x = 1 + ri(14), z = 1 + ri(14), y = 8 + ri(120) + DY;
    auto rock = [&](BlockId id) { return id == K.holy || id == K.dirt || id == K.grass || id == K.mossy; };
    if (!rock(at(x, y + 1, z)) || !rock(at(x, y - 1, z)) || !rock(at(x, y, z))) continue;
    int solid = 0, air = 0;
    for (int d = 0; d < 4; d++) { const BlockId nb = at(x + DIRX[d], y, z + DIRZ[d]); if (nb == AIR) air++; else if (rock(nb)) solid++; }
    if (solid == 3 && air == 1) { put(x, y, z, K.water); b.meta[colIdx(x, y, z)] = 0; }
  }
  // the bronze, silver and gold dungeons are structures (gen/structures_aether.cpp)
  structures.build(b, ch);
  b.store(ch);
}

void SkylandsGen::sampleAether(int wx, int wz, ColumnSample& s) const {
  s = ColumnSample(); s.temp = 160; s.humid = 190;
  int top, bot;
  if (!aetherColumn(wx, wz, top, bot)) { s.height = -1; return; }
  const AB& K = ab();
  s.height = (i16)top; s.top = K.grass; s.under = K.holy; s.floatBottom = (i16)bot;
}

void SkylandsGen::treesAether(int cx, int cz, std::vector<TreeSample>& out) const {
  const AB& K = ab();
  const int land = skyBiome(cx * 16 + 8, cz * 16 + 8);
  const double perChunk = land == 1 ? 3.05 : land == 3 ? 2.55 : land == 2 ? 1.05 : 0.5;
  Mulberry32 tr(hash32(seed_, cx, cz, 0xAE77));
  const int n = (int)perChunk + (tr() < perChunk - (int)perChunk ? 1 : 0);
  for (int i = 0; i < n; i++) {
    const int x = cx * 16 + 2 + (int)(tr() * 12), z = cz * 16 + 2 + (int)(tr() * 12);
    int top, bot;
    if (!aetherColumn(x, z, top, bot)) continue;
    const int H = 4 + (int)(tr() * 3);
    TreeSample t{x, top + 1, z, TREE_BIRCH, false, {}};
    for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) {
      if (std::abs(dx) == 2 && std::abs(dz) == 2) continue;
      const bool inner = std::abs(dx) <= 1 && std::abs(dz) <= 1 && !(std::abs(dx) == 1 && std::abs(dz) == 1);
      t.cols.push_back({(i8)dx, (i8)dz, (i16)(top + 1 + (inner ? H + 1 : H - 1)), (i16)(top + 1 + H - 3), K.skyLeaf, (BlockId)(dx == 0 && dz == 0 ? K.skyLog : 0)});
    }
    out.push_back(std::move(t));
  }
}

} // namespace dq
