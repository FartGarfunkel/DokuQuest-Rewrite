// ChunkDecoratorNether (+ ChunkFeatureDecorator / ChunkDecorationBuilder / PositionSelectors / PlacementMethod)
// run on a composite of undecorated chunks. See README.md for the neighbour model.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

#include "bta_nether_features.h"
#include "bta_nether_stats.h"

namespace bta {

namespace {
using namespace feat;

enum Sel : uint8_t { S_MINY, S_HRU, S_HRUFO, S_UPPER, S_BIASED, S_PILLAR };
enum Kind : uint8_t {
  K_CRYSTAL_CEIL, K_CRYSTAL_FLOOR, K_CRYSTAL_FLOOR2, K_NODE, K_BUD, K_GROWTH, K_VEIN, K_CLUMP_CRYST, K_CLUMP_BRIMTHAW,
  K_SULFUR_POCKET, K_SOULSAND_POCKET, K_WATER_SPRING, K_LAVA_SPRING, K_LAVA_SPRING2, K_LAVA_LAKE, K_LAVA_TERRACE,
  K_LAVA_TERRACE2, K_ACID_SPRING, K_PILLAR_RUBY, K_PILLAR_COBBLE, K_PILLAR_BRIMSTONE, K_PILLAR_SLATE, K_BOULDER_MAGMA,
  K_SULFUR_TERRACE, K_FLOOR_VENT, K_SPIRE, K_SULFUR_BOULDER, K_ROOF_NETHER, K_ROOF_VOLCANO, K_ROOF_SULFUR,
  K_ROOF_OLDWORLD, K_COAL1, K_COAL2, K_COAL3, K_GLOW1, K_GLOW2, K_GLOW_SULFUR, K_FIRE, K_FIRE_VOLCANIC,
  K_FIRE_SULFURIC, K_EMBER_ASH, K_BONE_PILE, K_BOULDER_MAG, K_BOULDER_MAG_SPARSE, K_BOULDER_SULF, K_SOUL_CATCHER,
  K_PEBBLES, K_COUNT
};
enum Place : uint8_t { P_ONCE, P_TRIES, P_CHANCE, P_DENSITY, P_POCKET40, P_GLOW1, P_NONE };
struct Deco { Kind k; uint16_t mask; Sel sel; Place place; int n; };
#define B(x) (uint16_t)(1u << (x))
const uint16_t CF = B(BI_CRYSTAL_FOREST), CP = B(BI_CRYSTAL_PLAINS), VO = B(BI_VOLCANIC_ISLANDS), CR = B(BI_CRAG),
               SH = B(BI_SHELF), SU = B(BI_SULFUR_POOLS), OW = B(BI_OLD_WORLD), OD = B(BI_OLD_WORLD_DESERT);
// registerDecorations() order (LinkedHashMap keyed by decoration object: duplicates keep both)
const Deco kDecos[K_COUNT] = {
    {K_CRYSTAL_CEIL, CF, S_UPPER, P_DENSITY, 0},
    {K_CRYSTAL_FLOOR, CF, S_HRUFO, P_DENSITY, 0},
    {K_CRYSTAL_FLOOR2, (uint16_t)(CF | CP), S_HRUFO, P_TRIES, 4},
    {K_NODE, (uint16_t)(CF | CP), S_HRU, P_TRIES, 10},
    {K_BUD, (uint16_t)(CF | CP), S_HRUFO, P_TRIES, 100},
    {K_GROWTH, (uint16_t)(CF | CP), S_HRUFO, P_TRIES, 100},
    {K_VEIN, (uint16_t)(CP | CF), S_MINY, P_ONCE, 1},
    {K_CLUMP_CRYST, (uint16_t)(CP | CF), S_HRU, P_TRIES, 500},
    {K_CLUMP_BRIMTHAW, SU, S_HRU, P_TRIES, 1000},
    {K_SULFUR_POCKET, SU, S_HRU, P_POCKET40, 0},
    {K_SOULSAND_POCKET, (uint16_t)(OW | OD), S_HRU, P_POCKET40, 0},
    {K_WATER_SPRING, (uint16_t)(CF | CP), S_BIASED, P_TRIES, 16},
    {K_LAVA_SPRING, (uint16_t)(VO | CR | SH), S_HRU, P_TRIES, 16},
    {K_LAVA_SPRING2, VO, S_HRU, P_TRIES, 16},
    {K_LAVA_LAKE, (uint16_t)(VO | CR | CP | CF | SH | SU), S_UPPER, P_CHANCE, 4},
    {K_LAVA_TERRACE, VO, S_MINY, P_CHANCE, 3},
    {K_LAVA_TERRACE2, VO, S_MINY, P_CHANCE, 3},
    {K_ACID_SPRING, SU, S_HRU, P_TRIES, 16},
    {K_PILLAR_RUBY, (uint16_t)(CP | CF), S_PILLAR, P_CHANCE, 8},
    {K_PILLAR_COBBLE, (uint16_t)(CP | CF | CR | SH), S_PILLAR, P_CHANCE, 4},
    {K_PILLAR_BRIMSTONE, (uint16_t)(VO | SU), S_PILLAR, P_TRIES, 4},
    {K_PILLAR_SLATE, OW, S_PILLAR, P_TRIES, 4},
    {K_BOULDER_MAGMA, VO, S_HRUFO, P_TRIES, 2},
    {K_SULFUR_TERRACE, SU, S_MINY, P_TRIES, 3},
    {K_FLOOR_VENT, (uint16_t)(VO | SU), S_HRUFO, P_TRIES, 12},
    {K_SPIRE, VO, S_HRUFO, P_TRIES, 8},
    {K_SULFUR_BOULDER, SU, S_HRUFO, P_TRIES, 12},
    {K_ROOF_NETHER, (uint16_t)(CR | SH), S_HRUFO, P_TRIES, 2},
    {K_ROOF_VOLCANO, VO, S_HRUFO, P_TRIES, 2},
    {K_ROOF_SULFUR, SU, S_HRUFO, P_TRIES, 2},
    {K_ROOF_OLDWORLD, OW, S_HRUFO, P_TRIES, 2},
    {K_COAL1, 0, S_HRU, P_TRIES, 10},
    {K_COAL2, VO, S_HRU, P_TRIES, 20},
    {K_COAL3, (uint16_t)(CR | SH), S_HRU, P_TRIES, 10},
    {K_GLOW1, (uint16_t)(VO | CR | CP | CF | SH | SU), S_HRUFO, P_GLOW1, 0},
    {K_GLOW2, (uint16_t)(VO | CR | CP | CF | SH | SU), S_HRUFO, P_TRIES, 10},
    {K_GLOW_SULFUR, SU, S_HRUFO, P_TRIES, 20},
    {K_FIRE, (uint16_t)(CR | SH), S_HRUFO, P_TRIES, 16},
    {K_FIRE_VOLCANIC, VO, S_HRUFO, P_TRIES, 24},
    {K_FIRE_SULFURIC, SU, S_HRUFO, P_TRIES, 32},
    {K_EMBER_ASH, 0, S_HRUFO, P_TRIES, 20},
    {K_BONE_PILE, 0, S_HRUFO, P_TRIES, 8},
    {K_BOULDER_MAG, (uint16_t)(CR | SH | VO), S_HRUFO, P_TRIES, 8},
    {K_BOULDER_MAG_SPARSE, (uint16_t)(CF | CP), S_HRUFO, P_TRIES, 4},
    {K_BOULDER_SULF, SU, S_HRUFO, P_TRIES, 24},
    {K_SOUL_CATCHER, 0, S_HRUFO, P_TRIES, 8},
    {K_PEBBLES, 0, S_MINY, P_NONE, 0},  // WorldFeaturePebbles: can never spawn on Nether blocks -> no effect
};
#undef B

// rubyglass crystals (0-2) and nether pillars (18-21): the only decorations reaching > 1 chunk out
const uint64_t kLongReachMask = (1ull << K_CRYSTAL_CEIL) | (1ull << K_CRYSTAL_FLOOR) | (1ull << K_CRYSTAL_FLOOR2) |
                                (1ull << K_PILLAR_RUBY) | (1ull << K_PILLAR_COBBLE) | (1ull << K_PILLAR_BRIMSTONE) |
                                (1ull << K_PILLAR_SLATE);

const OreMap kNodeMap{1, {bid::RUBYGLASS_COLUMN}, {bid::RUBYGLASS_NODE}};
const OreMap kCoalMap{3, {bid::COBBLE_NETHERRACK, bid::COBBLE_BASALT, bid::COBBLE_GLOOMSTONE},
                      {bid::ORE_NETHERCOAL_NETHERRACK, bid::ORE_NETHERCOAL_BASALT, bid::ORE_NETHERCOAL_GLOOMSTONE}};
const OreMap kSulfurMap{2, {bid::BRIMSTONE, bid::BRIMSAND}, {bid::SULFUR, bid::SULFUR}};
const OreMap kSoulMap{4, {bid::GLOOMSTONE, bid::COBBLE_GLOOMSTONE, bid::BLOCK_ASH, bid::SLATE},
                      {bid::SOULSAND, bid::SOULSAND, bid::SOULSAND, bid::SOULSAND}};

struct Runner {
  DecoWorld& w;
  const NetherGen::Impl& g;
  int cx, cz;
  DecoStats* stats;
  uint64_t onlyMask;  // decorations to run (far-margin chunks run only the long-reach ones)

  void select(Sel s, JavaRandom& r, int& x, int& y, int& z) {
    if (s == S_PILLAR) {  // ChunkDecoratorNether.selectPillarPosition
      int px = cx * 16 + r.nextInt(16);
      int pz = cz * 16 + r.nextInt(16);
      double ccx, ccy;
      g.pillarWorley.getCenter((double)(px >> 5), (double)(pz >> 5), ccx, ccy);
      x = jd2i(ccx * 32.0); y = 200; z = jd2i(ccy * 32.0);
      return;
    }
    x = cx * 16 + r.nextInt(16) + 8;
    z = cz * 16 + r.nextInt(16) + 8;
    switch (s) {
      case S_MINY: y = 0; break;
      case S_HRU: y = r.nextInt(256); break;
      case S_HRUFO: y = 96 + r.nextInt(256 - 8) + 4; break;
      case S_UPPER: y = 256 / 2 + r.nextInt(256 - 8) + 4; break;
      case S_BIASED: { int inner = r.nextInt(256 - 256 / 16) + 256 / 16; y = r.nextInt(inner); break; }
      default: y = 0;
    }
  }

  void feature(Kind k, JavaRandom& r, int x, int y, int z) {
    switch (k) {
      case K_CRYSTAL_CEIL: rubyglassCrystalline(w, r, x, y, z, true, 20, 15); break;
      case K_CRYSTAL_FLOOR: rubyglassCrystalline(w, r, x, y, z, false, 20, 30); break;
      case K_CRYSTAL_FLOOR2: rubyglassCrystalline(w, r, x, y, z, false, 15, 25); break;
      case K_NODE: ore(w, r, x, y, z, kNodeMap, 5); break;
      case K_BUD: growthPatch(w, r, x, y, z, 0.0f); break;
      case K_GROWTH: growthPatch(w, r, x, y, z, 0.6f); break;
      case K_VEIN: perlinVein(w, g.veinNoise, x, z, bid::COBBLE_NETHERRACK_CRYSTALLINE, bid::COBBLE_NETHERRACK, 0.06, 0.045); break;
      case K_CLUMP_CRYST: wallClump(w, x, y, z, bid::COBBLE_NETHERRACK_CRYSTALLINE, bid::COBBLE_NETHERRACK); break;
      case K_CLUMP_BRIMTHAW: wallClump(w, x, y, z, bid::BRIMTHAW, bid::BRIMSTONE); break;
      case K_SULFUR_POCKET: ore(w, r, x, y, z, kSulfurMap, 24); break;
      case K_SOULSAND_POCKET: ore(w, r, x, y, z, kSoulMap, 32); break;
      case K_WATER_SPRING: netherSpring(w, r, x, y, z, bid::WATER_FLOWING); break;
      case K_LAVA_SPRING: case K_LAVA_SPRING2: netherSpring(w, r, x, y, z, bid::LAVA_FLOWING); break;
      case K_LAVA_LAKE: lake(w, r, x, y, z, bid::LAVA_STILL); break;
      case K_LAVA_TERRACE: case K_LAVA_TERRACE2: terrace(w, r, x, y, z, bid::LAVA_STILL, bid::BRIMSTONE, 4, 4); break;
      case K_ACID_SPRING: netherSpring(w, r, x, y, z, bid::ACID_FLOWING); break;
      case K_PILLAR_RUBY: pillar(w, r, x, y, z, bid::RUBYGLASS_COLUMN, false); break;
      case K_PILLAR_COBBLE: pillar(w, r, x, y, z, bid::COBBLE_NETHERRACK, false); break;
      case K_PILLAR_BRIMSTONE: pillar(w, r, x, y, z, bid::BRIMSTONE, true); break;
      case K_PILLAR_SLATE: pillar(w, r, x, y, z, bid::SLATE, true); break;
      case K_BOULDER_MAGMA: boulderMagma(w, r, x, y, z); break;
      case K_SULFUR_TERRACE: terrace(w, r, x, y, z, bid::ACID_FLOWING, bid::BRIMSAND, 6, 6); break;
      case K_FLOOR_VENT: floorVent(w, r, x, y, z); break;
      case K_SPIRE: spireVent(w, r, x, y, z); break;
      case K_SULFUR_BOULDER: boulderGlowstone(w, r, x, y, z); break;
      case K_ROOF_NETHER: roofSpire(w, r, x, y, z, bid::COBBLE_NETHERRACK); break;
      case K_ROOF_VOLCANO: roofSpire(w, r, x, y, z, bid::BRIMSTONE); break;
      case K_ROOF_SULFUR: roofSpire(w, r, x, y, z, bid::SULFUR); break;
      case K_ROOF_OLDWORLD: roofSpire(w, r, x, y, z, bid::SLATE); break;
      case K_COAL1: case K_COAL2: ore(w, r, x, y, z, kCoalMap, 12); break;
      case K_COAL3: ore(w, r, x, y, z, kCoalMap, 16); break;
      case K_GLOW1: case K_GLOW2: case K_GLOW_SULFUR: glowstone(w, r, x, y, z); break;
      case K_FIRE: case K_FIRE_SULFURIC: fire(w, r, x, y, z, false); break;
      case K_FIRE_VOLCANIC: fire(w, r, x, y, z, true); break;
      case K_EMBER_ASH: emberAsh(w, r, x, y, z); break;
      case K_BONE_PILE: scatterBonePile(w, r, x, y, z); break;
      case K_BOULDER_MAG: case K_BOULDER_MAG_SPARSE: scatterBoulder(w, x, y, z, false); break;
      case K_BOULDER_SULF: scatterBoulder(w, x, y, z, true); break;
      case K_SOUL_CATCHER: soulCatcherPatch(w, r, x, y, z); break;
      default: break;
    }
  }

  void placeOne(const Deco& d, JavaRandom& r) {
    int x, y, z;
    select(d.sel, r, x, y, z);
    feature(d.k, r, x, y, z);
    if (stats) stats->attempts[d.k]++;
  }

  void decoration(int i, JavaRandom& r) {
    const Deco& d = kDecos[i];
    if (d.place == P_NONE) return;
    if (d.mask) {  // ChunkDecorationBuilder.placeDecoration biome mask (chunk corner, y = 127)
      int b = w.biome(cx * 16, 127, cz * 16);
      if (b >= BI_COUNT || !(d.mask & (1u << b))) return;
    }
    switch (d.place) {
      case P_ONCE: placeOne(d, r); break;
      case P_TRIES: for (int t = 0; t < d.n; ++t) placeOne(d, r); break;
      case P_CHANCE: if (r.nextInt(d.n) == 0) placeOne(d, r); break;
      case P_DENSITY: {
        int x = cx * 16, z = cz * 16;
        int density = jd2i((g.crystalDensity.getValue2((double)x * 0.5, (double)z * 0.5) - 6.0 + 30.0) / 6.0);
        for (int t = 0; t < density + 4; ++t) placeOne(d, r);
        break;
      }
      case P_POCKET40: {
        int tries = jf2i(20.0f * ((float)256 / 128.0f));
        for (int t = 0; t < tries; ++t) placeOne(d, r);
        break;
      }
      case P_GLOW1: {
        int inner = r.nextInt(10) + 1;
        int mx = r.nextInt(inner);
        for (int t = 0; t < mx; ++t) placeOne(d, r);
        break;
      }
      default: break;
    }
  }

  void postDecorate() {
    int wx = cx * 16, wz = cz * 16;
    for (int x = 0; x < 16; ++x)
      for (int z = 0; z < 16; ++z) {
        uint16_t id = w.get(wx + x, 255, wz + z);
        int cell = INT32_MIN;
        bool ash = false;
        for (int y = 255; y > 96; --y) {
          uint16_t below = w.get(wx + x, y - 1, wz + z);
          if (id == 0 && below != 0 && below != bid::OBSIDIAN && below != bid::SOULSAND && hasF(below, P_BLOCKS_MOTION) &&
              below != bid::RUBYGLASS_NODE && below != bid::RUBYGLASS_COLUMN && below != bid::RUBYGLASS_GROWTH) {
            if ((y >> 3) != cell) {
              cell = y >> 3;
              int b = w.biome(wx + x, y, wz + z);
              ash = b == BI_OLD_WORLD || b == BI_OLD_WORLD_DESERT;
            }
            if (ash) w.setBlockType(wx + x, y, wz + z, bid::LAYER_ASH);
          }
          id = below;
        }
      }
  }

  void run() {
    w.immediate = true;  // ChunkFeatureDecorator.decorate
    w.immSet.clear();
    w.wrand.setSeed(ladd(lmul(cx, 341873128712LL), lmul(cz, 132897987541LL)) ^ 0x5DEECE66DLL);
    JavaRandom rand;
    int64_t base = ladd(lmul(cx, g.decoA), lmul(cz, g.decoB)) ^ g.seed;
    for (int i = 0; i < K_COUNT; ++i) {
      if (!((g.decoMask >> i) & 1) || !((onlyMask >> i) & 1)) continue;
      rand.setSeed(base ^ (int64_t)g.decoSeeds[i]);
#ifdef BTA_RANDOM_DEBUG
      rand.debugLog = getenv("LOGRAND") != nullptr;
      if (rand.debugLog) printf("DECO %d chunk %d %d\n", i, cx, cz);
#endif
      decoration(i, rand);
    }
    postDecorate();
    w.immediate = false;
  }
};
}  // namespace

namespace {
// The composite covers these chunks; their terrain is materialised on first access.
void loadComposite(NetherGen::Impl&, DecoWorld& w, int cx0, int cz0, int nx, int nz) { w.resize(cx0, cz0, nx, nz); }
void decorateChunk(DecoWorld& w, const NetherGen::Impl& g, int cx, int cz, DecoStats* stats, uint64_t onlyMask = ~0ull) {
  Runner run{w, g, cx, cz, stats, onlyMask};
  run.run();
}
void extractChunk(const DecoWorld& w, int cx, int cz, uint8_t* kind, uint8_t* meta, uint16_t* ids) {
  int s = (cz - w.oz) * w.nx + (cx - w.ox);
  const DecoWorld::Slot& sd = w.slotData(s);
  const uint16_t* src = sd.ids;
  for (int i = 0; i < 65536; ++i) {
    bool al = sd.alloc[i >> 12] != 0;
    if (kind) kind[i] = (uint8_t)nbFromBtaId(src[i]);
    if (ids) ids[i] = src[i];
    if (meta) meta[i] = al ? (uint8_t)sd.getMeta(i) : 0;
  }
}
inline int floorDiv(int a, int b) { return (a >= 0 ? a : a - b + 1) / b; }
}  // namespace

// DecoMode::FourNeighbours
void NetherGen::Impl::decorateFour(int cx, int cz, uint16_t* outIds, uint8_t* outMeta, DecoStats* stats) {
  DecoWorld w;
  w.g = this;
  loadComposite(*this, w, cx - 2, cz - 2, 4, 4);
  static const int order[4][2] = {{-1, -1}, {0, -1}, {-1, 0}, {0, 0}};
  for (auto& o : order) decorateChunk(w, *this, cx + o[0], cz + o[1], stats);
  extractChunk(w, cx, cz, nullptr, outMeta, outIds);
}

// DecoMode::Region: decorate region +- margin in row-major order on a composite with one more ring of terrain.
std::shared_ptr<const DecoRegion> NetherGen::Impl::region(int rx, int rz, DecoStats* stats) {
  const int64_t key = ((int64_t)rx << 32) ^ (int64_t)(uint32_t)rz;
  {
    std::unique_lock<std::mutex> lk(rmu);
    for (;;) {
      auto it = rindex.find(key);
      if (it != rindex.end()) {
        rlru.splice(rlru.begin(), rlru, it->second);
        return it->second->second;
      }
      if (!rinflight.count(key)) break;
      rcv.wait(lk);
    }
    rinflight[key] = 1;
  }
  const int R = opt.regionSize, m = opt.margin, fm = std::max(opt.margin, opt.farMargin);
  const int c0x = rx * R, c0z = rz * R;
  auto reg = std::make_shared<DecoRegion>();
  try {
    DecoWorld w;
    w.g = this;
    loadComposite(*this, w, c0x - fm - 1, c0z - fm - 1, R + 2 * fm + 2, R + 2 * fm + 2);
    for (int cz = c0z - fm; cz < c0z + R + fm; ++cz)
      for (int cx = c0x - fm; cx < c0x + R + fm; ++cx) {
        bool full = cx >= c0x - m && cx < c0x + R + m && cz >= c0z - m && cz < c0z + R + m;
        decorateChunk(w, *this, cx, cz, stats, full ? ~0ull : kLongReachMask);
      }
    reg->size = R;
    reg->kind.resize((size_t)R * R * 65536);
    reg->meta.resize((size_t)R * R * 65536);
    for (int j = 0; j < R; ++j)
      for (int i = 0; i < R; ++i) {
        size_t o = ((size_t)j * R + i) * 65536;
        extractChunk(w, c0x + i, c0z + j, &reg->kind[o], &reg->meta[o], nullptr);
      }
  } catch (...) {
    std::lock_guard<std::mutex> lk(rmu);
    rinflight.erase(key);
    rcv.notify_all();
    throw;
  }
  std::lock_guard<std::mutex> lk(rmu);
  rinflight.erase(key);
  rlru.emplace_front(key, reg);
  rindex[key] = rlru.begin();
  while (rlru.size() > opt.regionCache) {
    rindex.erase(rlru.back().first);
    rlru.pop_back();
  }
  rcv.notify_all();
  return reg;
}

namespace {
// Out.meta carries BTA data only for blocks where it means something (see README); 0 elsewhere.
void normalizeMeta(const uint8_t* kind, uint8_t* meta) {
  for (int i = 0; i < 65536; ++i) {
    switch (kind[i]) {
      case NB_LAVA_STILL: case NB_LAVA_FLOWING: case NB_WATER_STILL: case NB_WATER_FLOWING: case NB_ACID_STILL:
      case NB_ACID_FLOWING: case NB_RUBYGLASS_GROWTH: case NB_LAYER_ASH: case NB_FIRE: case NB_FIRE_SULFURIC:
      case NB_THERMAL_VENT: case NB_OVERLAY_PEBBLES:
        break;
      default: meta[i] = 0;
    }
  }
}
}  // namespace

void NetherGen::Impl::finalChunk(int cx, int cz, uint8_t* kind, uint8_t* meta, uint16_t* ids, DecoStats* stats) {
  if (opt.mode == DecoMode::FourNeighbours) {
    std::vector<uint16_t> tmp;
    uint16_t* dst = ids;
    if (!dst) { tmp.resize(65536); dst = tmp.data(); }
    std::vector<uint8_t> mtmp;
    uint8_t* mdst = meta;
    if (!mdst) { mtmp.resize(65536); mdst = mtmp.data(); }
    decorateFour(cx, cz, dst, mdst, stats);
    if (kind) {
      for (int i = 0; i < 65536; ++i) kind[i] = (uint8_t)nbFromBtaId(dst[i]);
      if (meta) normalizeMeta(kind, meta);
    }
    return;
  }
  const int R = opt.regionSize;
  int rx = floorDiv(cx, R), rz = floorDiv(cz, R);
  auto reg = region(rx, rz, stats);
  size_t o = ((size_t)(cz - rz * R) * R + (cx - rx * R)) * 65536;
  if (kind) std::memcpy(kind, &reg->kind[o], 65536);
  if (meta) std::memcpy(meta, &reg->meta[o], 65536);
  if (kind && meta) normalizeMeta(kind, meta);
  if (ids)
    for (int i = 0; i < 65536; ++i) ids[i] = nbToBtaId((NB)reg->kind[o + i]);
}

}  // namespace bta
