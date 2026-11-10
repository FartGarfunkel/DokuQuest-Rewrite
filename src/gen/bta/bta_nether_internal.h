// Internal: BTA 8.0.1 block ids / properties and shared state of the Nether generator port.
#pragma once
#include <cstdint>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "bta_nether.h"
#include "bta_noise.h"

namespace bta {
namespace bid {  // BTA numeric block ids (Blocks.java)
constexpr uint16_t AIR = 0, BASALT = 2, SLATE = 6, COBBLE_BASALT = 12, OBSIDIAN = 180, PUMICE_DRY = 232,
                   PUMICE_WET = 233, BEDROCK = 260, WATER_FLOWING = 270, WATER_STILL = 271, LAVA_FLOWING = 272,
                   LAVA_STILL = 273, ORE_NETHERCOAL_NETHERRACK = 420, ORE_NETHERCOAL_BASALT = 421,
                   ORE_NETHERCOAL_GLOOMSTONE = 422, BLOCK_NETHER_COAL = 436, FIRE = 630, FIRE_SULFURIC = 634,
                   LAYER_ASH = 723, ICE = 730, BLOCK_ASH = 741, COBBLE_NETHERRACK = 800, MAGMA = 801,
                   COBBLE_NETHERRACK_CRYSTALLINE = 802, NETHERRACK = 803, SOULSAND = 810, GLOWSTONE = 820,
                   RUBYGLASS_COLUMN = 821, RUBYGLASS_CRYSTAL = 823, RUBYGLASS_GROWTH = 825, RUBYGLASS_NODE = 826,
                   OVERLAY_PEBBLES = 880, THERMAL_VENT = 1025, BONE_PILE = 1030, SOUL_CATCHER = 1031,
                   GLOOMSTONE = 1032, COBBLE_GLOOMSTONE = 1033, BOULDER_MAGMATIC = 1060, BOULDER_SULFURIC = 1061,
                   ACID_FLOWING = 1062, ACID_STILL = 1063, EMBER = 1065, BRIMSAND = 1085, BRIMSTONE = 1086,
                   BRIMTHAW = 1092, SULFUR = 1098, DIRT = 220, GRASS = 200;
constexpr int MAX_ID = 1100;
}  // namespace bid

// Material identity (only what the generator compares)
enum class Mat : uint8_t { AIR, STONE, BASALT, SLATE, NETHERRACK, GLOOMSTONE, SOUL_SAND, SAND, ASH, TOP_ASH, ICE,
                           PLANT, DECORATION, FIRE, WATER, LAVA, ACID, OTHER };

enum : uint32_t {
  T_CAVES_CUT = 1u << 0, T_CAVE_REPLACES_SURFACE = 1u << 1, T_NETHER_SURFACE = 1u << 2, T_INF_BURN = 1u << 3,
  T_INF_BURN_SULF = 1u << 4, T_IS_LAVA = 1u << 5, T_IS_WATER = 1u << 6, T_BROKEN_BY_FLUIDS = 1u << 7,
  T_PLACE_OVERWRITES = 1u << 8,
  P_MAT_SOLID = 1u << 12, P_BLOCKS_MOTION = 1u << 13, P_LIQUID = 1u << 14, P_SOLID = 1u << 15,  // Block.solid()
  P_CUBE = 1u << 16, P_SOLID_RENDER = 1u << 17, P_FLUID_LOGIC = 1u << 18, P_KNOWN = 1u << 19,
};

struct BlockProps {
  uint32_t f = 0;
  Mat mat = Mat::OTHER;
  uint8_t nb = 0;  // NB kind
};
const BlockProps& props(uint16_t id);
inline bool hasF(uint16_t id, uint32_t fl) { return (props(id).f & fl) != 0; }
inline Mat matOf(uint16_t id) { return props(id).mat; }

// Nether biome ids (internal == public NetherBiome order)
enum : uint8_t { BI_OLD_WORLD = 0, BI_CRYSTAL_PLAINS, BI_CRYSTAL_FOREST, BI_SHELF, BI_CRAG, BI_SULFUR_POOLS,
                 BI_VOLCANIC_ISLANDS, BI_OLD_WORLD_DESERT, BI_COUNT };

// Undecorated chunk: blocks after ChunkGeneratorPerlin.doBlockGeneration (terrain+surface+large features)
struct TerrainChunk {
  uint16_t id[65536];    // (y<<8)|(z<<4)|x
  uint8_t climate[256];  // biome above y=64 at (z<<4)|x  (y < 64 is always OLD_WORLD_DESERT)
};
// compact cached form (NB kinds; terrain only ever contains known blocks)
struct TerrainCached {
  uint8_t kind[65536];
  uint8_t climate[256];
  uint16_t sections;  // bit s: section s has a non-air block
};
struct DecoRegion {
  int size = 0;
  std::vector<uint8_t> kind, meta;  // size*size chunks, chunk (i,j) at ((j*size)+i)*65536
};

struct NetherGen::Impl {
  Impl(int64_t seed, const NetherGenOptions& opt);
  NetherGenOptions opt;
  int64_t seed;
  // biome provider
  Fractal<Simplex> tempNoise, downNoise, fuzzNoise;
  uint8_t biomeLut[50 * 50 * 2];  // [alt>=0.5][iH][iT]
  // terrain
  Fractal<ImprovedPerlin> minLimit, maxLimit, mainNoise;
  Fractal<ImprovedPerlin> beachNoise, soilNoise;
  double densityOffset[25];
  // large features
  int64_t lfRandX, lfRandZ;
  Worley floeWorley;
  Fractal<ImprovedPerlin> floeGap;  // == heightNoise (same seed/octaves)
  Normalize2D floeScale;
  // decoration
  Worley pillarWorley;
  Fractal<ImprovedPerlin> crystalDensity;
  ImprovedPerlin veinNoise;  // WorldFeaturePerlinVein(COBBLE_NETHERRACK_CRYSTALLINE) noiseGen
  int64_t decoA, decoB;
  std::vector<int32_t> decoSeeds;
  uint64_t decoMask = ~0ull;  // NamespaceID.toString().hashCode() per registered decoration

  // biomes
  void climateRegion(int x0, int z0, uint8_t* out /*256, (z<<4)|x*/, double* temps /*nullable*/) const;
  int climateAt(int x, int z, double* temp) const;
  // terrain
  void buildTerrain(int cx, int cz, TerrainChunk& tc, int stages) const;
  void density(int cx, int cz, double* d /*5*33*5*/) const;
  void surface(int cx, int cz, TerrainChunk& tc) const;
  void caves(int cx, int cz, TerrainChunk& tc) const;
  void floes(int cx, int cz, TerrainChunk& tc, int stages) const;
  // cache of undecorated chunks
  std::shared_ptr<const TerrainCached> terrain(int cx, int cz);
  std::mutex mu;
  std::list<std::pair<int64_t, std::shared_ptr<const TerrainCached>>> lru;
  std::unordered_map<int64_t, decltype(lru)::iterator> index;
  // decoration (bta_nether_deco.cpp)
  void decorateFour(int cx, int cz, uint16_t* ids, uint8_t* meta, DecoStats* stats);
  std::shared_ptr<const DecoRegion> region(int rx, int rz, DecoStats* stats);
  std::mutex rmu;
  std::condition_variable rcv;
  std::list<std::pair<int64_t, std::shared_ptr<const DecoRegion>>> rlru;
  std::unordered_map<int64_t, decltype(rlru)::iterator> rindex;
  std::unordered_map<int64_t, int> rinflight;
  void finalChunk(int cx, int cz, uint8_t* kind, uint8_t* meta, uint16_t* ids, DecoStats* stats);
};

int32_t javaStringHash(const char* s);

}  // namespace bta
