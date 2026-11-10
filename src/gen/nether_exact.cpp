// gen/nether_exact.cpp — terrain version 7's Nether: Better than Adventure 8.0.1's own generator, ported exactly
// (gen/bta, checked block for block against BTA's classes: terrain, surface, caves, floes, shelves, its biomes and all
// 47 decorations), its blocks mapped onto ours. BTA's y is our band y (both 256 tall, its sea at 96, bedrock below 64).
#include "gen/dimgens.h"
#include "gen/bta/bta_nether.h"
#include "gen/nether_bta.h"
#include <cmath>
#include "core/registry.h"
#include <map>
#include <mutex>

namespace dq {

namespace {
// BTA's blocks -> ours (the nearest where this game has no exact one)
struct NBMap { BlockId id[bta::NB_OTHER + 1]; };
const NBMap& nbMap() {
  static const NBMap m = [] {
    NBMap r{};
    auto k = [&](bta::NB n, std::initializer_list<const char*> keys) {
      for (const char* s : keys) if (BlockId b = reg().blockId(s)) { r.id[n] = b; return; }
    };
    k(bta::NB_BEDROCK, {"bedrock"}); k(bta::NB_NETHERRACK, {"netherrack"}); k(bta::NB_COBBLE_NETHERRACK, {"cobbled_netherrack", "netherrack"});
    k(bta::NB_COBBLE_NETHERRACK_CRYSTALLINE, {"crystalline_netherrack", "cobbled_netherrack", "netherrack"});
    k(bta::NB_LAVA_STILL, {"lava"}); k(bta::NB_LAVA_FLOWING, {"lava"}); k(bta::NB_WATER_STILL, {"water"}); k(bta::NB_WATER_FLOWING, {"water"});
    k(bta::NB_ACID_STILL, {"acid", "water"}); k(bta::NB_ACID_FLOWING, {"acid", "water"}); k(bta::NB_OBSIDIAN, {"obsidian"});
    k(bta::NB_SOULSAND, {"soul_sand"}); k(bta::NB_SULFUR, {"sulphur_block", "nether_brimstone"}); k(bta::NB_BRIMSAND, {"brimsand", "soul_sand"});
    k(bta::NB_BRIMSTONE, {"nether_brimstone", "basalt"}); k(bta::NB_BRIMTHAW, {"brimthaw", "magma_block"});
    k(bta::NB_GLOOMSTONE, {"gloomstone", "blackstone"}); k(bta::NB_COBBLE_GLOOMSTONE, {"cobbled_gloomstone", "gloomstone"});
    k(bta::NB_GLOWSTONE, {"glowstone"}); k(bta::NB_BLOCK_ASH, {"ash"}); k(bta::NB_LAYER_ASH, {"ash_layer", "ash"}); k(bta::NB_SLATE, {"slate"});
    k(bta::NB_BASALT, {"basalt"}); k(bta::NB_COBBLE_BASALT, {"cobbled_basalt", "basalt"}); k(bta::NB_PUMICE_WET, {"molten_pumice", "pumice"});
    k(bta::NB_PUMICE_DRY, {"pumice", "basalt"}); k(bta::NB_MAGMA, {"magma_block"});
    k(bta::NB_RUBYGLASS_CRYSTAL, {"rubyglass_crystal", "red_crystal"}); k(bta::NB_RUBYGLASS_COLUMN, {"rubyglass_column", "red_crystal"});
    k(bta::NB_RUBYGLASS_NODE, {"rubyglass_node", "red_crystal"}); k(bta::NB_RUBYGLASS_GROWTH, {"rubyglass_growth", "red_crystal_cluster"});
    k(bta::NB_ORE_NETHERCOAL_NETHERRACK, {"nether_coal_ore", "coal_ore"}); k(bta::NB_ORE_NETHERCOAL_BASALT, {"basalt_coal_ore", "nether_coal_ore"});
    k(bta::NB_ORE_NETHERCOAL_GLOOMSTONE, {"gloom_coal_ore", "nether_coal_ore"}); k(bta::NB_BLOCK_NETHER_COAL, {"nether_coal_block", "coal_block"});
    k(bta::NB_THERMAL_VENT, {"thermal_vent", "steam_vent", "magma_block"}); k(bta::NB_FIRE, {"fire"}); k(bta::NB_FIRE_SULFURIC, {"sulfuric_fire", "fire"});
    k(bta::NB_EMBER, {"ember", "magma_block"}); k(bta::NB_BONE_PILE, {"bone_pile", "bone_block"}); k(bta::NB_BOULDER_MAGMATIC, {"magmatic_boulder", "magma_block"});
    k(bta::NB_BOULDER_SULFURIC, {"sulfuric_boulder", "sulphur_block"}); k(bta::NB_SOUL_CATCHER, {"soul_catcher", "soul_sand"});
    k(bta::NB_OTHER, {"netherrack"});
    return r;
  }();
  return m;
}
// the meta that carries over: fluid levels (BTA's 0 source, 1..7 flowing, 8 falling: ours exactly) and ash layer heights
inline u8 metaOf(u8 kind, u8 meta) {
  switch (kind) {
    case bta::NB_LAVA_STILL: case bta::NB_LAVA_FLOWING: case bta::NB_WATER_STILL: case bta::NB_WATER_FLOWING: case bta::NB_ACID_STILL: case bta::NB_ACID_FLOWING: return meta & 15;
    case bta::NB_LAYER_ASH: return meta & 7;
    default: return 0;
  }
}
inline u8 ourLand(int nbio) {
  switch (nbio) {
    case bta::NBIO_OLD_WORLD: return NB_OLDWORLD; case bta::NBIO_CRYSTAL_PLAINS: return NB_CRYSTAL_PLAINS;
    case bta::NBIO_CRYSTAL_FOREST: return NB_CRYSTAL; case bta::NBIO_SHELF: return NB_SHELF; case bta::NBIO_CRAG: return NB_CRAG;
    case bta::NBIO_SULFUR_POOLS: return NB_SULFUR; case bta::NBIO_VOLCANIC_ISLANDS: return NB_VOLCANIC; default: return NB_OLD_DESERT;
  }
}
// Terrain 8: BTA's Nether in the open land of terrain 5. BTA's lands (its biome map, every 4 blocks round the chunk)
// pick the shapes of version 5's five lands (crags, volcanic islands in the lava sea, acid pools, the crystal plains,
// the Old World's dunes), blended across their borders; the ground and roof are lifted 46 blocks so version 5's lava
// sea (y 50) is BTA's (y 96). BTA's generator does the rest on that shape: its surfaces, caves, floes, shelves and all
// its decorations. A walk across it, not a climb through caves.
struct OpenShape {
  NetherShape shape; std::shared_ptr<bta::NetherGen> biomes;
  OpenShape(i32 seed, std::shared_ptr<bta::NetherGen> b) : shape(seed, true), biomes(std::move(b)) {}
  static int kindOf(int nbio) {   // each of BTA's lands its own shape (NetherShape open8)
    switch (nbio) {
      case bta::NBIO_CRAG: return NB_CRAG; case bta::NBIO_VOLCANIC_ISLANDS: return NB_VOLCANIC; case bta::NBIO_SULFUR_POOLS: return NB_SULFUR;
      case bta::NBIO_CRYSTAL_FOREST: return NB_CRYSTAL; case bta::NBIO_CRYSTAL_PLAINS: return NB_CRYSTAL_PLAINS; case bta::NBIO_SHELF: return NB_SHELF;
      default: return NB_OLDWORLD;
    }
  }
  void fill(int cx, int cz, bta::NetherGenOptions::ShapeColumn* cols) const {
    constexpr int N = 13, STEP = 4, R = 14, LIFT = 96 - NetherShape::LAVA;
    const int x0 = cx * 16 - 16, z0 = cz * 16 - 16;
    u8 kind[N][N];
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) kind[j][i] = (u8)kindOf(biomes->biomeAt(x0 + i * STEP, 100, z0 + j * STEP));
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
      float w[NB_COUNT] = {};
      const int wx = cx * 16 + x, wz = cz * 16 + z;
      for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        const float d = std::hypot((float)(x0 + i * STEP - wx), (float)(z0 + j * STEP - wz));
        if (d >= R) continue;
        const float k = 1 - d / R; w[kind[j][i]] += k * k;
      }
      const NetherCol c = shape.columnWeighted(wx, wz, w);
      auto& o = cols[(z << 4) | x];
      o.floor = (int16_t)std::min(240, c.floor + LIFT); o.roof = (int16_t)std::min(250, c.roof + LIFT);
      o.pool = c.pool; o.poolTop = (int16_t)(c.pool ? c.poolTop + LIFT : -1);
    }
  }
};
// one exact Nether per seed (and one in the open shape), shared by the generator, the sky and the spawns
std::shared_ptr<bta::NetherGen> exactFor(i32 seed, bool open = false) {
  static std::mutex mx; static std::map<std::pair<i32, bool>, std::weak_ptr<bta::NetherGen>> all;
  std::shared_ptr<bta::NetherGen> exact;
  if (open) exact = exactFor(seed, false);   // (its biome map: the open one's lands are the same)
  std::lock_guard<std::mutex> lk(mx);
  auto& w = all[{seed, open}];
  if (auto p = w.lock()) return p;
  bta::NetherGenOptions o;
  if (open) { auto os = std::make_shared<OpenShape>(seed, exact); o.shape = [os](int cx, int cz, bta::NetherGenOptions::ShapeColumn* c) { os->fill(cx, cz, c); }; }
  auto p = std::make_shared<bta::NetherGen>((int64_t)seed, o);
  w = p;
  return p;
}
// undecorated terrain for the queries structures make (where the ground is), a few chunks kept per thread
struct TerrainLRU { const bta::NetherGen* g = nullptr; int cx = INT32_MIN, cz = INT32_MIN; std::unique_ptr<bta::NetherGen::Out> out; };
const bta::NetherGen::Out& terrainOf(bta::NetherGen& g, int cx, int cz) {
  thread_local TerrainLRU slot[4]; thread_local int next = 0;
  for (auto& s : slot) if (s.g == &g && s.cx == cx && s.cz == cz) return *s.out;
  TerrainLRU& s = slot[next++ & 3];
  if (!s.out) s.out = std::make_unique<bta::NetherGen::Out>();
  g.generateTerrain(cx, cz, *s.out); s.g = &g; s.cx = cx; s.cz = cz;
  return *s.out;
}
}

void DepthsGen::initExact(bool open) { exact_ = exactFor(seed_, open); exactOpen_ = open; }

void DepthsGen::generateExact(Chunk& ch) {
  GenBuf& b = GenBuf::local();
  std::fill(ch.climate.begin(), ch.climate.end(), (u8)128);
  thread_local std::unique_ptr<bta::NetherGen::Out> out;
  if (!out) out = std::make_unique<bta::NetherGen::Out>();
  exact_->generateChunk(ch.cx, ch.cz, *out);
  const NBMap& M = nbMap();
  for (int i = 0; i < DIM_H * 256; i++) {
    const u8 k = out->kind[i];
    if (!k) continue;
    b.blocks[i] = M.id[k < bta::NB_OTHER ? k : bta::NB_OTHER];
    b.meta[i] = metaOf(k, out->meta[i]);
  }
  structures.build(b, ch);
  b.store(ch);
}

bool DepthsGen::solidExact(int x, int y, int z) const {
  if (y <= 0) return true;
  if (y >= DIM_H) return false;
  const auto& T = terrainOf(*exact_, floordiv(x, 16), floordiv(z, 16));
  const u8 k = T.kind[(y << 8) | ((z & 15) << 4) | (x & 15)];
  return k && k != bta::NB_LAVA_STILL && k != bta::NB_LAVA_FLOWING && k != bta::NB_WATER_STILL && k != bta::NB_WATER_FLOWING &&
         k != bta::NB_ACID_STILL && k != bta::NB_ACID_FLOWING && k != bta::NB_FIRE && k != bta::NB_FIRE_SULFURIC;
}

// the floor a structure stands on: the highest open floor between BTA's sea (96) and its roof, else the sea floor
namespace {
// the finished (decorated) chunk, one kept per thread: a landing is checked against what is really there (BTA's
// decorations melt, burn and flow after the terrain is made)
const bta::NetherGen::Out& decoratedOf(bta::NetherGen& g, int cx, int cz) {
  thread_local TerrainLRU slot; 
  if (slot.g == &g && slot.cx == cx && slot.cz == cz) return *slot.out;
  if (!slot.out) slot.out = std::make_unique<bta::NetherGen::Out>();
  g.generateChunk(cx, cz, *slot.out); slot.g = &g; slot.cx = cx; slot.cz = cz;
  return *slot.out;
}
// the ground under the open air from the roof down: the y to stand at, or -1 (no ground, or lava, acid, a crystal's tip)
int landingIn(const bta::NetherGen::Out& T, int i) {
  auto k = [&](int y) { return T.kind[(y << 8) | i]; };
  auto open = [&](u8 c) { return c == bta::NB_AIR || c == bta::NB_FIRE || c == bta::NB_FIRE_SULFURIC; };
  int y = 248;
  while (y > 66 && !open(k(y))) y--;          // down through the roof
  while (y > 66 && open(k(y))) y--;           // down through the air to the ground
  if (y <= 66 || !open(k(y + 1)) || !open(k(y + 2)) || open(k(y - 1))) return -1;   // headroom, and ground two thick (not a shelf's lip)
  const u8 g = k(y);
  if (g == bta::NB_LAVA_STILL || g == bta::NB_LAVA_FLOWING || g == bta::NB_WATER_STILL || g == bta::NB_WATER_FLOWING || g == bta::NB_ACID_STILL || g == bta::NB_ACID_FLOWING ||
      g == bta::NB_RUBYGLASS_CRYSTAL || g == bta::NB_RUBYGLASS_GROWTH || g == bta::NB_LAYER_ASH) return -1;
  return y + 1;
}
}

int DepthsGen::spawnHeight(int wx, int wz) const {
  if (!exact_) { ColumnSample s; sample(wx, wz, s); return s.height + 1; }
  const int cx = floordiv(wx, 16), cz = floordiv(wz, 16), i = ((wz & 15) << 4) | (wx & 15);
  if (landingIn(terrainOf(*exact_, cx, cz), i) < 0) return -1;   // (cheap first: most of a ring search ends here)
  // not in or by a structure: they're built after all this, and a bone pit or a forge's cellar goes down a long way
  std::vector<const StructurePlan*> near; structures.near(wx, wz, 8, near);
  if (!near.empty()) return -1;
  return landingIn(decoratedOf(*exact_, cx, cz), i);
}

TerrainColumn DepthsGen::terrainExact(int x, int z) const {
  TerrainColumn t;
  const auto& T = terrainOf(*exact_, floordiv(x, 16), floordiv(z, 16));
  const int i = ((z & 15) << 4) | (x & 15);
  auto kind = [&](int y) { return T.kind[(y << 8) | i]; };
  auto fluid = [&](u8 k) { return k == bta::NB_LAVA_STILL || k == bta::NB_LAVA_FLOWING || k == bta::NB_WATER_STILL || k == bta::NB_WATER_FLOWING; };
  auto solid = [&](u8 k) { return k != bta::NB_AIR && !fluid(k) && k != bta::NB_FIRE && k != bta::NB_FIRE_SULFURIC; };
  int y = 64;
  if (exactOpen_) {
    // the open land: the ground under the open air, found from the roof down (from the bedrock up, BTA's caves under
    // the land came first, and structures were built in them, out of sight)
    y = 248;
    while (y > 66 && solid(kind(y))) y--;
    while (y > 66 && !solid(kind(y))) y--;
  } else while (y < 250 && solid(kind(y + 1))) y++;   // the floor: the top of the rock rising from the bedrock
  t.y = y; t.jsY = y;
  if (fluid(kind(y + 1))) { int w = y + 1; while (w < 255 && fluid(kind(w + 1))) w++; t.water = w + 1; }
  return t;
}

// the BTA land and temperature where you stand (the fog, the grade and the spawns read these)
// BTA's BlockTags.NETHER_MOBS_SPAWN: the ground zombie pigmen stand on and ghasts float over, as the blocks they are
// here (netherrack and its cobble, soul sand and schist, the nether coal ores, gloomstone, ember, rubyglass, brimsand,
// brimstone and its bricks, sulfur, boneshale, carved slate)
bool netherMobsSpawnOn(BlockId id) {
  static const std::vector<u8> tag = [] {
    std::vector<u8> t(65536, 0);
    const NBMap& m = nbMap();
    for (bta::NB n : {bta::NB_NETHERRACK, bta::NB_COBBLE_NETHERRACK, bta::NB_SOULSAND, bta::NB_ORE_NETHERCOAL_NETHERRACK, bta::NB_ORE_NETHERCOAL_BASALT,
                      bta::NB_ORE_NETHERCOAL_GLOOMSTONE, bta::NB_RUBYGLASS_CRYSTAL, bta::NB_GLOOMSTONE, bta::NB_EMBER, bta::NB_BRIMSAND, bta::NB_BRIMSTONE, bta::NB_SULFUR})
      if (m.id[n]) t[m.id[n]] = 1;
    for (const char* k : {"bone_shale", "soul_schist", "carved_slate", "brimstone_bricks", "cobbled_netherrack", "netherrack"}) if (BlockId b = reg().blockId(k)) t[b] = 1;
    return t;
  }();
  return tag[id] != 0;
}

bool netherExactLand(i32 seed, int x, int y, int z, u8& land) {
  auto g = exactFor(seed);
  land = ourLand(g->biomeAt(x, y, z));
  return true;
}
double netherExactTemperature(i32 seed, int x, int z) { return exactFor(seed)->temperatureAt(x, z); }

} // namespace dq
