// Exact port of the classic Nether generator ("nether.default").
// Same seed -> same blocks as the reference chunk generator (terrain, surface, large features) and
// decorator (decoration, in a fixed neighbour order, see README.md).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace nethergen {

// Every block the classic Nether generation can place. Names in nbName() are the generator's namespace ids.
enum NB : uint8_t {
  NB_AIR = 0,
  NB_BEDROCK,
  NB_NETHERRACK,
  NB_COBBLE_NETHERRACK,
  NB_COBBLE_NETHERRACK_CRYSTALLINE,
  NB_LAVA_STILL,
  NB_LAVA_FLOWING,
  NB_WATER_STILL,
  NB_WATER_FLOWING,
  NB_ACID_STILL,
  NB_ACID_FLOWING,
  NB_OBSIDIAN,
  NB_SOULSAND,
  NB_SULFUR,
  NB_BRIMSAND,
  NB_BRIMSTONE,
  NB_BRIMTHAW,
  NB_GLOOMSTONE,
  NB_COBBLE_GLOOMSTONE,
  NB_GLOWSTONE,
  NB_BLOCK_ASH,
  NB_LAYER_ASH,
  NB_SLATE,
  NB_BASALT,
  NB_COBBLE_BASALT,
  NB_PUMICE_WET,
  NB_PUMICE_DRY,
  NB_MAGMA,
  NB_RUBYGLASS_CRYSTAL,
  NB_RUBYGLASS_COLUMN,
  NB_RUBYGLASS_NODE,
  NB_RUBYGLASS_GROWTH,
  NB_ORE_NETHERCOAL_NETHERRACK,
  NB_ORE_NETHERCOAL_BASALT,
  NB_ORE_NETHERCOAL_GLOOMSTONE,
  NB_BLOCK_NETHER_COAL,
  NB_THERMAL_VENT,
  NB_FIRE,
  NB_FIRE_SULFURIC,
  NB_EMBER,
  NB_BONE_PILE,
  NB_BOULDER_MAGMATIC,
  NB_BOULDER_SULFURIC,
  NB_SOUL_CATCHER,
  NB_OVERLAY_PEBBLES,
  NB_OTHER,  // any other block (should never appear)
  NB_COUNT
};
const char* nbName(NB k);         // e.g. "block/cobble_netherrack"
uint16_t nbToLegacyId(NB k);         // the generator's numeric block id (0 for NB_OTHER)
NB nbFromLegacyId(uint16_t legacyId);

// the Nether biome provider's biomes
enum NetherBiome : uint8_t {
  NBIO_OLD_WORLD = 0,        // nether.old_world
  NBIO_CRYSTAL_PLAINS,       // nether.crystal_plains
  NBIO_CRYSTAL_FOREST,       // nether.crystal_forest
  NBIO_SHELF,                // nether.shelf
  NBIO_CRAG,                 // nether.crag
  NBIO_SULFUR_POOLS,         // nether.sulfur_pools
  NBIO_VOLCANIC_ISLANDS,     // nether.volcanic_islands
  NBIO_OLD_WORLD_DESERT,     // nether.old_world.desert (every column below y = 64)
  NBIO_COUNT
};
const char* netherBiomeName(int biome);

struct DecoStats;  // optional per-decoration placement counters (validation)

// How decorations of neighbouring chunks are combined (the generator decorates a chunk once its 8 neighbours are
// loaded, writing into the +8 offset area and further: rubyglass crystals and nether pillars reach 2-4
// chunks away, so the final blocks of a chunk depend on the decoration of many chunks and on their order).
enum class DecoMode : uint8_t {
  // Default. The world is decorated in the reference streaming order (row-major: z outer, x inner, every chunk after
  // its terrain neighbours exist). A region of regionSize^2 chunks is produced by decorating the region plus
  // `margin` chunks around it on a composite of their terrain, then cached. Matches a reference world generated in
  // row-major order; region borders are seamless except for second-order effects (see README).
  Region,
  // The simple model: terrain of chunks cx-2..cx+1 x cz-2..cz+1, decorations of (cx-1,cz-1), (cx,cz-1),
  // (cx-1,cz), (cx,cz) in that order. Cheapest, but features reaching > 8 blocks out of their chunk are cut.
  FourNeighbours,
};

struct NetherGenOptions {
  DecoMode mode = DecoMode::Region;
  int regionSize = 4;          // Region mode: chunks per region side
  int margin = 4;              // Region mode: chunks around a region that are decorated too
  int farMargin = 0;           // Region mode: further chunks of which only the long-reach decorations run
                               //   (rubyglass crystals, nether pillars); <= margin disables. 6 = exact, ~2x cost
  size_t terrainCache = 512;   // LRU capacity, undecorated chunks (64 KB each)
  size_t regionCache = 16;     // Region mode: LRU capacity, decorated regions (regionSize^2 * 128 KB each)
  // Optional (DokuQuest's open Nether; not the reference): the land's shape in place of the classic density. Given a chunk, fill its 256
  // columns (z * 16 + x): rock up to `floor` and from `roof` up, between them air (the sea below y 96 as ever), a
  // pool to `poolTop` of lava (pool 1) or acid (pool 2). The classic bedrock, surface, caves, floes, shelves and every
  // decoration then run as they would. Unset: the reference exactly.
  struct ShapeColumn { int16_t floor = 96, roof = 230, poolTop = -1; uint8_t pool = 0; };
  std::function<void(int cx, int cz, ShapeColumn* cols)> shape;
};

class NetherGen {
 public:
  // indexed (y<<8)|(z<<4)|x, y = generator y 0..255
  struct Out {
    uint8_t kind[16 * 256 * 16];
    uint8_t meta[16 * 256 * 16];
  };
  explicit NetherGen(int64_t seed, const NetherGenOptions& opt = NetherGenOptions());
  ~NetherGen();
  NetherGen(const NetherGen&) = delete;
  NetherGen& operator=(const NetherGen&) = delete;

  int64_t seed() const;
  // the biome at block coordinates (the world's block biome / the biome provider's)
  int biomeAt(int x, int y, int z) const;
  // the biome provider's temperature at (x, z): continuous temperature in [0,1]
  double temperatureAt(int x, int z) const;

  // FINAL (decorated) blocks of chunk (cx,cz), see DecoMode. Thread-safe.
  void generateChunk(int cx, int cz, Out& out);
  // Undecorated blocks (the block generation pass's output). Thread-safe.
  void generateTerrain(int cx, int cz, Out& out);

  // ---- validation helpers (raw generator ids) ----
  // stages bit mask: 1 terrain(density+lerp), 2 surface, 4 caves, 8 lava floe, 16 rubyglass floe, 32 shelf
  void terrainIds(int cx, int cz, uint16_t* ids /*65536*/, int stages = 63) const;
  void chunkIds(int cx, int cz, uint16_t* ids /*65536*/, uint8_t* meta /*65536*/, DecoStats* stats = nullptr);
  void densityMap(int cx, int cz, double* out /*5*5*33*/) const;
  // run only the decorations whose bit is set (index = the decorator's registration order 0..46)
  void setDecorationMaskForTesting(uint64_t mask);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace nethergen
