// gen/nether_bn.cpp — see nether_bn.h
#include "gen/nether_bn.h"
#include "gen/nether_lands.h"
#include "gen/gendefs.h"
#include "gen/nethergen/nethergen.h"
#include "core/registry.h"
#include "world/chunk.h"
#include <cmath>
#include <map>
#include <memory>
#include <mutex>

namespace dq {

bool netherExactLand(i32 seed, int x, int y, int z, u8& land);   // gen/nether_exact.cpp
double netherExactTemperature(i32 seed, int x, int z);

const char* bnLandName(int l) {
  static const char* N[BN_COUNT] = {"", "Nether Jungle", "Gravel Desert", "Nether Swampland", "Magma Land", "Soul Plain", "Old Warped Woods"};
  return l > 0 && l < BN_COUNT ? N[l] : "";
}
u32 bnFogColour(int l) {   // BetterNether's biome fog colours
  static const u32 F[BN_COUNT] = {0, 0x3EA93D, 0xAA3000, 0x89134E, 0xF89E44, 0xC471EF, 0x1A051A};
  return l > 0 && l < BN_COUNT ? F[l] : 0;
}

namespace {
const SimplexNoise& maskNoise(i32 seed) {
  static std::mutex mu; static std::map<i32, std::unique_ptr<SimplexNoise>> all;
  std::lock_guard<std::mutex> l(mu);
  auto& p = all[seed];
  if (!p) p = std::make_unique<SimplexNoise>((u32)seed ^ 0xB7E7u);
  return *p;
}
}

u8 bnLandFor(i32 seed, int x, int z, u8 host, double temp, bool v19) {
  // the slow mask: BetterNether's lands in patches about 400 blocks across, BTA's between them
  const double m = maskNoise(seed).fbm2(x / 384.0, z / 384.0, 2);
  const double edge = ((hash32(seed, x, z, 0xB7E8) & 255) / 255.0) * 0.06;   // a ragged border, not a line
  if (m < 0.12 + edge) return BN_NONE;
  switch (host) {
    // the crags by their warmth, as BetterNether's climate points: jungle (0.5, 0.8), swamp (0.1, 0.8), warped woods (0.0, 0.55)
    case NB_CRAG:
      // (terrain 19: by the crags' own warmth, which runs 0.76..0.96 — at BetterNether's 0.62 / 0.45 they were all jungle)
      if (v19) return temp > 0.9 ? BN_JUNGLE : temp > 0.84 ? BN_SWAMP : BN_OLD_WARPED;
      return temp > 0.62 ? BN_JUNGLE : temp > 0.45 ? BN_SWAMP : BN_OLD_WARPED;
    case NB_OLDWORLD: return BN_GRAVEL_DESERT;
    case NB_SULFUR: return BN_SWAMP;
    case NB_VOLCANIC: return BN_MAGMA;
    case NB_SHELF: case NB_OLD_DESERT: return BN_SOUL_PLAIN;   // the cold dry shelf: BN's soul plain (0.01, -0.5)
    default: return BN_NONE;   // the crystal lands stay BTA's
  }
}

u8 netherBnLand(i32 seed, int genVersion, int x, int z) {
  if (genVersion < 13) return BN_NONE;
  u8 host = NB_CRAG;
  netherExactLand(seed, x, 128, z, host);
  return bnLandFor(seed, x, z, host, netherExactTemperature(seed, x, z), genVersion >= 19);
}

namespace {
struct Ids {
  BlockId fire = 0, sfire = 0, jgrass, sgrass, mgrass, nmoss, ssand, ssoil, sandstone, gravel, magma, obsidian, wnyl, rack,
          reed, cactus, bbush, abush, lucis, jelly, vines[4], smoker, wlog, wleaves, rlog, rleaves, wroots, wfungus, twisting, sprouts;
  Ids() {
    auto id = [](const char* k) { return reg().blockId(k); };
    fire = id("fire"); sfire = id("sulfuric_fire");
    jgrass = id("nether_jungle_grass"); sgrass = id("swampland_grass"); mgrass = id("mushroom_grass"); nmoss = id("netherrack_moss");
    ssand = id("soul_sand"); ssoil = id("soul_soil"); sandstone = id("soul_sandstone"); gravel = id("gravel"); magma = id("magma_block");
    obsidian = id("obsidian"); wnyl = id("warped_nylium"); rack = id("netherrack");
    reed = id("nether_reed"); cactus = id("nether_cactus"); bbush = id("black_bush"); abush = id("black_apple_bush");
    lucis = id("lucis_mushroom"); jelly = id("jellyfish_mushroom");
    vines[0] = id("black_vine"); vines[1] = id("golden_vine"); vines[2] = id("blooming_vine"); vines[3] = id("eye_vine");
    smoker = id("bn_smoker"); wlog = id("willow_log"); wleaves = id("willow_leaves"); rlog = id("rubeus_log"); rleaves = id("rubeus_leaves");
    wroots = id("warped_roots"); wfungus = id("warped_fungus"); twisting = id("twisting_vines"); sprouts = id("nether_sprouts");
  }
};
const Ids& ids() { static const Ids I; return I; }

struct Painter {
  GenBuf& b; const Ids& I; const Registry& R = reg(); i32 seed; int bx, bz; bool strong = false;
  bool open(BlockId id) const { return id == 0 || id == I.fire || id == I.sfire; }
  bool ground(BlockId id) const {
    if (strong && (R.tLeaves[id] || id == I.rlog || id == I.wlog || id == I.wleaves || id == I.rleaves)) return false;   // (terrain 19: a crown is no floor: no tree on a tree, no grass on leaves)
    return id && R.tSolid[id] && !R.tLiquid[id] && !open(id);
  }
  BlockId& at(int x, int y, int z) { return b.blocks[colIdx(x, y, z)]; }
  void set(int x, int y, int z, BlockId id) { if (id && y > 0 && y < DIM_H) { b.blocks[colIdx(x, y, z)] = id; b.meta[colIdx(x, y, z)] = 0; } }
  float rnd(int x, int z, int salt) const { return strong ? (float)posHashF(seed, bx + x, bz + z, salt) : (hash32(seed, bx + x, bz + z, salt) & 0xFFFF) / 65536.f; }

  // a stack of a plant up from the floor (reeds, cacti: 1..n tall), only into open air
  void stack(int x, int f, int z, BlockId id, int n) { for (int k = 1; k <= n && open(at(x, f + k, z)); k++) set(x, f + k, z, id); }
  // a vine down from the roof's underside
  void hang(int x, int r, int z, BlockId id, int n) { for (int k = 1; k <= n && open(at(x, r - k, z)); k++) set(x, r - k, z, id); }
  bool clearUp(int x, int f, int z, int h) { for (int k = 1; k <= h; k++) if (!open(at(x, f + k, z))) return false; return true; }

  // BetterNether's rubeus: a straight trunk under a round crown
  void rubeus(int x, int f, int z, int h) {
    for (int k = 1; k <= h; k++) set(x, f + k, z, I.rlog);
    const int top = f + h;
    for (int dy = -1; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
      const int r2 = dx * dx + dz * dz + dy * dy * 2;
      if (r2 > 6 || (dx == 0 && dz == 0 && dy <= 0)) continue;
      if (r2 >= 5 && rnd(x + dx * 7, z + dz * 13 + dy, 0xB1) < 0.35f) continue;   // a ragged edge
      if (open(at(x + dx, top + dy, z + dz))) set(x + dx, top + dy, z + dz, I.rleaves);
    }
  }
  // a willow: a short trunk, a flat crown, strands hanging from its rim
  void willow(int x, int f, int z, int h) {
    for (int k = 1; k <= h; k++) set(x, f + k, z, I.wlog);
    const int top = f + h;
    for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
      const int r2 = dx * dx + dz * dz;
      if (r2 > 5) continue;
      if (open(at(x + dx, top + 1, z + dz))) set(x + dx, top + 1, z + dz, I.wleaves);
      if (r2 <= 2 && open(at(x + dx, top + 2, z + dz))) set(x + dx, top + 2, z + dz, I.wleaves);
      if (r2 >= 4) {   // the hanging strands
        const int n = 1 + (int)(rnd(x + dx, z + dz, 0xB2) * 3.5f);
        for (int k = 0; k < n && open(at(x + dx, top - k, z + dz)); k++) set(x + dx, top - k, z + dz, I.wleaves);
      }
    }
  }

  void column(int x, int z, u8 land, bool trees) {
    int y = 248;
    while (y > 66 && !open(at(x, y, z))) y--;   // down through the roof
    const int roofUnder = y + 1;                // the roof's lowest block
    while (y > 66 && open(at(x, y, z))) y--;    // down through the air
    if (y <= 66) return;
    const int f = y;                            // the floor's top block
    const BlockId top = at(x, f, z);
    const bool solidTop = ground(top);
    const bool roof = roofUnder < 250 && ground(at(x, roofUnder, z));
    const float r = rnd(x, z, 0xB0), r2 = rnd(x, z, 0xB3), r3 = rnd(x, z, 0xB4);
    const bool inner = x >= 2 && x <= 13 && z >= 2 && z <= 13;   // a tree stays inside its chunk (no seams)
    switch (land) {
      case BN_JUNGLE:
        if (solidTop) {
          set(x, f, z, I.jgrass);
          if (trees && inner && r < 0.012f && roofUnder - f > 12 && clearUp(x, f, z, 9)) rubeus(x, f, z, 5 + (int)(r2 * 3));
          else if (r < 0.06f) stack(x, f, z, I.reed, 1 + (int)(r2 * 3));
          else if (r < 0.08f) stack(x, f, z, I.abush, 1);
          else if (r < 0.12f) stack(x, f, z, I.bbush, 1);
          else if (r < 0.135f) stack(x, f, z, I.jelly, 1);
          else if (r < 0.145f) stack(x, f, z, I.lucis, 1);
        }
        if (roof && r3 < 0.07f) hang(x, roofUnder, z, I.vines[1 + (int)(r2 * 3) % 3], 1 + (int)(rnd(x, z, 0xB5) * 6));
        break;
      case BN_GRAVEL_DESERT:
        if (solidTop) {
          for (int k = 0; k < 4 && ground(at(x, f - k, z)); k++) set(x, f - k, z, I.gravel);
          if (r < 0.012f) stack(x, f, z, I.cactus, 1 + (int)(r2 * 3));
          else if (r < 0.022f) stack(x, f, z, I.bbush, 1);
        }
        break;
      case BN_SWAMP:
        if (!solidTop && R.tLiquid[top] == 3 && ground(at(x, f - 1, z)) && r3 < 0.7f) set(x, f, z, I.sgrass);   // the acid's shallows: marsh
        if (ground(at(x, f, z))) {
          set(x, f, z, r2 < 0.3f ? I.ssoil : I.sgrass);
          if (trees && inner && r < 0.015f && roofUnder - f > 10 && clearUp(x, f, z, 8)) willow(x, f, z, 4 + (int)(r2 * 2));
          else if (r < 0.08f) stack(x, f, z, I.reed, 1 + (int)(r3 * 3));
          else if (r < 0.11f) stack(x, f, z, I.bbush, 1);
          else if (r < 0.12f) stack(x, f, z, I.smoker, 1);
          else if (r < 0.13f) stack(x, f, z, I.jelly, 1);
        }
        if (roof && r3 < 0.03f) hang(x, roofUnder, z, I.vines[0], 1 + (int)(r2 * 4));
        break;
      case BN_MAGMA:
        if (solidTop) {
          if (r2 < 0.32f) set(x, f, z, I.magma);
          if (r < 0.008f) stack(x, f, z, I.obsidian, 1 + (int)(r3 * 3));   // BetterNether's obsidian crystals, small
          else if (r < 0.025f) stack(x, f, z, I.bbush, 1);
        }
        if (roof && r3 < 0.025f) hang(x, roofUnder, z, I.vines[1], 1 + (int)(r2 * 3));
        break;
      case BN_SOUL_PLAIN:
        if (solidTop) {
          const BlockId s = maskNoise(seed).noise2((bx + x) / 9.0, (bz + z) / 9.0) > 0 ? I.ssand : I.ssoil;
          for (int k = 0; k < 2 && ground(at(x, f - k, z)); k++) set(x, f - k, z, s);
          if (r < 0.02f) stack(x, f, z, I.bbush, 1);
        }
        if (roof) for (int k = 0; k < 2 && ground(at(x, roofUnder + k, z)); k++) set(x, roofUnder + k, z, I.sandstone);
        break;
      case BN_OLD_WARPED:
        if (solidTop) {
          set(x, f, z, I.wnyl);
          if (r < 0.05f) stack(x, f, z, I.wroots, 1);
          else if (r < 0.065f) stack(x, f, z, I.wfungus, 1);
          else if (r < 0.10f) stack(x, f, z, I.sprouts, 1);
          else if (r < 0.12f) stack(x, f, z, I.twisting, 1 + (int)(r2 * 5));
        }
        if (roof && r3 < 0.05f) hang(x, roofUnder, z, I.vines[0], 1 + (int)(r2 * 5));
        break;
      default: break;
    }
  }
};
}

void bnPaint(i32 seed, const nethergen::NetherGen& g, GenBuf& b, int cx, int cz, bool trees, bool strongHash) {
  Painter P{b, ids(), reg(), seed, cx * 16, cz * 16, strongHash};
  if (!P.I.jgrass) return;   // (the data without BetterNether's blocks)
  for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
    const int wx = cx * 16 + x, wz = cz * 16 + z;
    const double m = maskNoise(seed).fbm2(wx / 384.0, wz / 384.0, 2);
    if (m < 0.12) continue;   // (cheap first: most columns are BTA's)
    double temp = 0;
    const u8 host = [&] {
      const int bio = g.biomeAt(wx, 128, wz);
      switch (bio) {   // as gen/nether_exact.cpp ourLand
        case nethergen::NBIO_OLD_WORLD: return (u8)NB_OLDWORLD; case nethergen::NBIO_CRYSTAL_PLAINS: return (u8)NB_CRYSTAL_PLAINS;
        case nethergen::NBIO_CRYSTAL_FOREST: return (u8)NB_CRYSTAL; case nethergen::NBIO_SHELF: return (u8)NB_SHELF; case nethergen::NBIO_CRAG: return (u8)NB_CRAG;
        case nethergen::NBIO_SULFUR_POOLS: return (u8)NB_SULFUR; case nethergen::NBIO_VOLCANIC_ISLANDS: return (u8)NB_VOLCANIC; default: return (u8)NB_OLD_DESERT;
      }
    }();
    if (host == NB_CRAG) temp = g.temperatureAt(wx, wz);
    const u8 land = bnLandFor(seed, wx, wz, host, temp, strongHash);
    if (land) P.column(x, z, land, trees);
  }
}

} // namespace dq
