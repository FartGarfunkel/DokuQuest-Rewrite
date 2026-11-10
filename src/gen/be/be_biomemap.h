// gen/be/be_biomemap.h — Better End 3.2.5's End biomes and BCLib 2.3's hex biome map (BCLibEndBiomeSource with
// EndBiomeMapType.HEX). The biome table (genChance, terrainHeight, parent) is Better End's own data
// (data/betterend/worldgen/betterx/biome/*.json); the map itself lives in BCLib, which is not in the BE jar, so
// HexBiomeMap / HexBiomeChunk / BiomePicker here are RECONSTRUCTED from BCLib 2.x as remembered (see README.md):
// 32 x 32 hex cells per map chunk, 16 jittered seeds per chunk each painting a 7-cell hexagon, random-neighbour growth
// until full, then a 1-in-4 per cell chance to paint a 7-cell hexagon of a sub-biome (weighted by genChance); cells are
// size/8 blocks, the lookup point is rotated 0.4 rad and warped by fractal OpenSimplex noise.
#pragma once
#include "gen/be/be_noise.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace dq::be {

// 0..18 are DokuQuest's End land indices (gen/theend_be.cpp); 19..23 are the vanilla biomes BCLib keeps in its pools.
enum BiomeId : int {
  B_CHORUS_FOREST = 0, B_BLOSSOMING_SPIRES, B_FOGGY_MUSHROOMLAND, B_GLOWING_GRASSLANDS, B_CRYSTAL_MOUNTAINS,
  B_SULPHUR_SPRINGS, B_UMBRELLA_JUNGLE, B_SHADOW_FOREST, B_ICE_STARFIELD, B_AMBER_LAND,
  B_MEGALAKE, B_LANTERN_WOODS, B_PAINTED_MOUNTAINS, B_NEON_OASIS, B_DRAGON_GRAVEYARDS,
  B_DRY_SHRUBLAND, B_DUST_WASTELANDS, B_UMBRA_VALLEY, B_MEGALAKE_GROVE,
  B_THE_END, B_END_HIGHLANDS, B_END_MIDLANDS, B_SMALL_END_ISLANDS, B_END_BARRENS,
  B_COUNT,
  B_LAND_COUNT = B_THE_END,
};

enum BiomeKind : uint8_t { K_LAND, K_VOID, K_CENTER, K_BARRENS };

struct BiomeInfo {
  const char* id;       // resource id
  float genChance;      // pick weight in its pool / sub-biome weight in its parent
  float terrainHeight;  // BCLBiomeSettings.terrainHeight (feeds the island top noise, 3.5 in the spec)
  int8_t parent;        // parent biome for sub-biomes, else -1
  int8_t edge;          // edge biome, else -1
  uint8_t edgeSize;     // edge width in blocks
  BiomeKind kind;
};
extern const BiomeInfo kBiomes[B_COUNT];

// BCLib BiomePicker + BCLBiome sub-biome lists: weighted picks with one nextFloat() each.
class BiomePicker {
public:
  void add(int id);
  void finish();                                        // builds the cumulative weights (call once)
  int pick(JavaRandom& r) const;                        // WeighTree.get: v = nextFloat*total, first v < cum
  int subBiome(int id, JavaRandom& r) const;            // WeightedList.get over [self 1.0, subs by genChance]
  bool empty() const { return ids_.empty(); }

private:
  std::vector<int> ids_;
  std::vector<float> cum_;
  struct Sub { std::vector<int> ids; std::vector<float> cum; };
  std::vector<Sub> subs_ = std::vector<Sub>(B_COUNT);
};

struct HexMapOptions {
  float warp = 0.5f;      // fractal warp amplitude (cell units) of the octave at 1 cycle per cell [reconstructed]
  float seamWarp = 1.5f;  // amplitude (cells) of an extra octave at 1/4 cycle per cell that bends the straight
                          // map-chunk seams; 0 = only the BCLib-like octaves [DokuQuest addition]
  int subOneIn = 4;       // per cell: 1 in N paints a sub-biome hexagon [reconstructed]
};

class HexBiomeMap {
public:
  HexBiomeMap(int64_t seed, int size, const BiomePicker* picker, const HexMapOptions& o = {});
  int biome(double x, double z) const;  // BiomeId at block (x, z), with edge biomes
  int rawBiome(double x, double z) const;
  float cellSize() const { return scale_; }

private:
  struct Chunk { uint8_t b[1024]; };
  const Chunk& chunk(int cx, int cz) const;
  void buildChunk(int cx, int cz, Chunk& out) const;

  const BiomePicker* picker_;
  HexMapOptions opt_;
  float scale_;
  OpenSimplexNoise noise0_, noise1_;
  int iterations_;
  int32_t seed_;
  mutable std::shared_mutex mx_;
  mutable std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace dq::be
