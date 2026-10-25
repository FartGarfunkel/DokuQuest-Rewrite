// gen/dimgens.h — the other bands and the side realms:
//   DepthsGen   the Ember Depths / Nether (worldgen.js generateDepths + theend.js void crust), 2.5x taller
//   SkylandsGen the Skylands / Aether (worldgen.js generateSkylands), islands lifted to band 140..230
//   EndGen      The End (theend.js: islands, pillars, exit portal bowl, platform, chorus), lifted 64
//   PocketGen   the pocket dimension's rooms (worldgen.js generatePocket), RealmGen his realm (riftarc.js)
#pragma once
#include "gen/generator.h"
#include "gen/structures.h"
#include "gen/trees.h"

namespace dq {

class DepthsGen : public DimGenerator, public TerrainQuery {
public:
  explicit DepthsGen(const WorldGenOptions& o);
  static constexpr int LAYERS = 104;                 // original rows 0..103 sampled (the band reaches 102.0)
  static constexpr int LAVA_SEA = 28;                // original units (band 70)
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
  const TerrainQuery* terrain() const override { return this; }
  int dim() const override { return DIM_NETHER; }
  i32 seed() const override { return seed_; }
  TerrainColumn terrainColumn(int x, int z) const override;
  bool solidAt(int x, int y, int z) const override;  // the cavern test (WorldGen.depthsSolid), band-local y
  // density of one column at the original rows (exactly what generate() interpolates)
  void densityColumn(int wx, int wz, float* D) const;
  static bool solidFrom(const float* D, int y);      // band y from a density column
  StructureIndex structures;
private:
  void fungusTrees(int cx, int cz, std::vector<TreeSpec>& out) const;
  i32 seed_;
  GenBlocks B;
  SimplexNoise nCave1, nCave2, nFloor, nHill, nMount, nDetail;
};

class SkylandsGen : public DimGenerator, public TerrainQuery {
public:
  explicit SkylandsGen(const WorldGenOptions& o);
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
  void trees(int cx, int cz, std::vector<TreeSample>& out) const override;
  const TerrainQuery* terrain() const override { return this; }
  int dim() const override { return DIM_AETHER; }
  i32 seed() const override { return seed_; }
  TerrainColumn terrainColumn(int x, int z) const override;
  int islandTop(int x, int z) const;                 // band y of the island's grass here, -1 = open sky
  StructureIndex structures;
private:
  i32 seed_;
  GenBlocks B;
  SimplexNoise nCont, nHill, nCave1;
};

class EndGen : public DimGenerator, public TerrainQuery {
public:
  explicit EndGen(const WorldGenOptions& o);
  struct Isle { int top, bot; bool main; };          // band-local
  struct Pillar { int x, z, h, rad; bool cage; };     // h band-local
  static constexpr int PLAT_X = 100, PLAT_Y = 48 + vmap::END_DY, PLAT_Z = 0;   // the stronghold portal's platform
  static constexpr int PILLAR_R = 43, OUTER = 330, GATE_R = 96;
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
  const TerrainQuery* terrain() const override { return this; }
  int dim() const override { return DIM_END; }
  i32 seed() const override { return seed_; }
  TerrainColumn terrainColumn(int x, int z) const override;
  bool island(int x, int z, Isle& out) const;        // WorldGen.endColumn
  const std::vector<Pillar>& pillars() const { return pillars_; }
  int fountainY() const;                             // the exit portal's bowl floor + 1 (band-local)
  vec3 spawnPoint() const { return vec3(PLAT_X + 0.5f, PLAT_Y + 1, PLAT_Z + 0.5f); }
  StructureIndex structures;
private:
  bool chorusAt(int wx, int wz, std::vector<std::array<int, 4>>& out) const;
  i32 seed_;
  GenBlocks B;
  SimplexNoise nA, nB, nC;
  std::vector<Pillar> pillars_;
};

class PocketGen : public DimGenerator {
public:
  explicit PocketGen(const WorldGenOptions& o) { B.resolve(); }
  static constexpr int HALF = 24, FLOOR = 63, CEIL = 100;
  static vec3 center(int slot) { return vec3(slot * 256 + 0.5f, 64, 0.5f); }
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
private:
  GenBlocks B;
};

class RealmGen : public DimGenerator {
public:
  explicit RealmGen(const WorldGenOptions& o) : seed_(o.seed) { B.resolve(); }
  static constexpr int HALL = 112, HALLS = 5, FLOOR = 40, CEIL = 82;
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
private:
  struct Isle { double x, y, z, r; bool tree; };
  void islands(int hi, std::vector<Isle>& out) const;
  i32 seed_;
  GenBlocks B;
};

// a band with nothing in it (side realms' other bands)
class VoidGen : public DimGenerator {
public:
  void generate(Chunk& c) override {}
  void sample(int wx, int wz, ColumnSample& s) const override { s = ColumnSample(); s.height = -1; }
};

void registerDefaultWorldGen();

} // namespace dq
