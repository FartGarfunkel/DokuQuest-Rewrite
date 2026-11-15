// gen/land_biomes.h — Soulrend's biomes (terrain version 15): which biome is where, as its mods place them
// (tools/port/worldgen/import.py -> data/worldgen/biome_regions.json, biome/<ns>/<name>.json).
// TerraBlender's region layer picks, per area of the world, one of six lists of climate boxes (region 0: Expanded
// Ecosphere's; 1-5: Nature's Spirit's regions); the biome is the box nearest the climate there (temperature, humidity,
// continentalness, erosion, depth, weirdness: Minecraft's noise router), as Minecraft's Climate.RTree finds it.
// Positions: quarts (block >> 2) for the climate, as Minecraft samples it; fuzzyQuart() is Minecraft's BiomeManager zoom
// (which quart a block reads its biome from: the jittered edges between biomes).
#pragma once
#include "common/common.h"
#include "common/fs.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace dq {

class LandBiomes {
public:
  struct Info {
    std::string name;                       // "natures_spirit:aspen_forest"
    float temperature = 0.8f, downfall = 0.4f;
    bool precipitation = true, frozen = false;   // (frozen: Minecraft's "frozen" temperature modifier)
    int grass = -1, foliage = -1, water = 0x3F76E4, sky = 0x78A7FF;   // -1: from the colour maps
    std::string grassModifier;              // "", "dark_forest", "swamp"
  };
  // dir: data/worldgen. False when the data isn't there.
  // withQuark (terrain version 17): Quark's TerraBlender region too (its Glimmering Weald; and the regions' weights with it)
  bool load(const fs::path& dir, i64 worldSeed, bool withQuark = true, bool strongHash = true);   // strongHash (18): unstriped snow lines
  bool ok() const { return !regions_.empty(); }
  int count() const { return (int)info_.size(); }
  const Info& info(int b) const { return info_[b]; }
  int find(const std::string& name) const { auto it = byName_.find(name); return it == byName_.end() ? -1 : it->second; }

  // TerraBlender's region at a quart (x, z)
  int region(int qx, int qz) const;
  // the biome at a climate point (Minecraft's quantized values: value * 10000) in a region
  int lookup(int region, const i64 p[6]) const;
  // Minecraft's BiomeManager.getBiome: the quart a block's biome is read from
  void fuzzyQuart(int x, int y, int z, int& qx, int& qy, int& qz) const;

  // (the snow line: Minecraft's Biome.coldEnoughToSnow at a block, y in Minecraft's coordinates)
  bool coldEnoughToSnow(int b, int x, int y, int z) const;

private:
  struct Box { i64 lo[7], hi[7]; int biome; };   // (index 6: the offset, a point)
  struct Node { i64 lo[7], hi[7]; int first = 0, count = 0, child = -1, nchild = 0; };   // leaves: boxes [first, first+count)
  struct Region { std::string name; int weight = 0; std::vector<Box> boxes; std::vector<Node> nodes; };
  void build(Region& r);
  int layer(int L, int x, int z) const;

  std::vector<Region> regions_;
  std::vector<Info> info_;
  std::unordered_map<std::string, int> byName_;
  int regionSize_ = 3;
  i64 worldSeed_ = 0;
  u64 zoomSeed_ = 0;
  bool strong_ = true;
  std::vector<i64> layerSeed_;   // (each layer's AreaContext seed)
};

} // namespace dq
