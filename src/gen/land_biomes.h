// gen/land_biomes.h — the land biomes (terrain version 15): which biome is where, as the data places them
// (tools/port/worldgen/import.py -> data/worldgen/biome_regions.json, biome/<ns>/<name>.json).
// The region layer picks, per area of the world, one of six lists of climate boxes (region 0: the base set;
// 1-5: the other regions); the biome is the box nearest the climate there (temperature, humidity,
// continentalness, erosion, depth, weirdness: the noise router), found with an R-tree search.
// Positions: quarts (block >> 2) for the climate; fuzzyQuart() is the biome zoom
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
    std::string name;                       // "nature:aspen_forest"
    float temperature = 0.8f, downfall = 0.4f;
    bool precipitation = true, frozen = false;   // (frozen: the "frozen" temperature modifier)
    int grass = -1, foliage = -1, water = 0x3F76E4, sky = 0x78A7FF;   // -1: from the colour maps
    std::string grassModifier;              // "", "dark_forest", "swamp"
  };
  // dir: data/worldgen. False when the data isn't there.
  // withDappled (terrain version 26): the humid part of each region's forests is the Dappled Forest instead (tall poplars, red leaf litter)
  // withExtras (terrain version 17): the extra region too (its Glimmering Weald; and the regions' weights with it)
  bool load(const fs::path& dir, i64 worldSeed, bool withExtras = true, bool strongHash = true, bool withDappled = false);   // strongHash (18): unstriped snow lines, biome zoom, region mix
  bool ok() const { return !regions_.empty(); }
  int count() const { return (int)info_.size(); }
  const Info& info(int b) const { return info_[b]; }
  int find(const std::string& name) const { auto it = byName_.find(name); return it == byName_.end() ? -1 : it->second; }

  // the region at a quart (x, z)
  int region(int qx, int qz) const;
  // the biome at a climate point (quantized values: value * 10000) in a region
  int lookup(int region, const i64 p[6]) const;
  // the biome zoom: the quart a block's biome is read from
  void fuzzyQuart(int x, int y, int z, int& qx, int& qy, int& qz) const;
  // one quart corner's jitter (the 8-corner mix). strong_: posHashF, no third-axis stripes.
  void quartFiddle(int cx, int cy, int cz, double& a, double& b, double& c) const;

  // (the snow line: cold enough to snow at a block, y in the imported data's coordinates)
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
