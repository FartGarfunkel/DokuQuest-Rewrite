// gen/surface_rules.h — Minecraft's surface rules, run as Soulrend runs them (terrain version 15): what the stone of the
// land becomes near its surface (grass, dirt, sand, snow, terracotta bands, calcite, gravel...), by biome, height, depth,
// slope, water and noise. The rules are data (tools/port/worldgen/import.py -> data/worldgen/
// surface_rules.json): TerraBlender's by the biome's namespace (natures_spirit, minecraft, climaterivers), then the noise
// settings' own (Expanded Ecosphere's: its wythers biomes). The column walk is SurfaceSystem.buildSurface's.
// Coordinates are Minecraft's (y with its sea at 63).
#pragma once
#include "common/common.h"
#include "common/fs.h"
#include "core/registry.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dq {
class DensityGraph;
class LandBiomes;

class LandSurface {
public:
  // dir: data/worldgen. g: the world's density graph (its noises; call on the main thread). False without the data.
  bool load(const fs::path& dir, DensityGraph& g, const LandBiomes& biomes, u64 seed);
  bool ok() const { return base_ >= 0; }

  // one column, as Minecraft's SurfaceRules.Context
  struct Column {
    int x = 0, z = 0;
    int surfaceDepth = 0, minSurface = 0;   // (minSurface: the preliminary surface - 8 + surfaceDepth; begin() sets it)
    double secondary = 0;
    bool steep = false;
    // the block being worked: set per block by the walk
    int y = 0, waterHeight = INT32_MIN, stoneAbove = 0, stoneBelow = 0;
    std::function<int(int y)> biomeAt;      // the biome (LandBiomes index) at (x, y, z)
    int biomeY = INT32_MIN, biome = -1;     // (the last asked)
  };
  // a column's start: its surface depth and secondary noise; prelim is Minecraft's preliminary surface level here
  // (NoiseChunk.preliminarySurfaceLevel, blended from its 16-block cell corners by the caller)
  void begin(Column& c, int x, int z, int prelim) const;
  // the block the rules put at the column's block (c.y and the rest set): 0xFFFF for none (it stays stone)
  BlockId apply(Column& c) const;

private:
  struct Cond {
    u8 t = 0; bool add = false, ceiling = false;
    int child = -1, offset = 0, mul = 0, range = 0, noise = -1, a = 0, b = 0;
    double lo = 0, hi = 0; u64 salt = 0;
    std::vector<u8> biomes;   // (per LandBiomes index)
  };
  struct Rule { u8 t = 0; BlockId block = 0; int cond = -1, then = -1; std::vector<int> seq; };
  int parseRule(const json& j);
  int parseCond(const json& j);
  int anchor(const json& j) const;
  bool test(int ci, Column& c) const;
  BlockId run(int ri, Column& c) const;
  int biomeOf(Column& c) const;

  std::vector<Cond> conds_;
  std::vector<Rule> rules_;
  int base_ = -1;
  std::vector<int> byNs_;   // per LandBiomes index: its namespace's rule (-1 none)
  const DensityGraph* g_ = nullptr;
  const LandBiomes* bio_ = nullptr;
  u64 seed_ = 0;
  int nSurface = -1, nSecondary = -1, nClayOffset = -1;
  std::vector<BlockId> bands_;   // the badlands' clay bands
  std::unordered_map<std::string, std::string> subs_;   // a mod's ground block -> ours (import.py matched them by look)
};

} // namespace dq
