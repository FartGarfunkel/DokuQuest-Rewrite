// gen/dimgens.h — the other bands and the side realms:
//   DepthsGen   the Ember Depths / Nether (worldgen.js generateDepths + theend.js void crust), 2.5x taller
//   SkylandsGen the Skylands / Aether (worldgen.js generateSkylands), islands lifted to band 140..230
//   EndGen      The End (theend.js: islands, pillars, exit portal bowl, platform, chorus), lifted 64
//   PocketGen   the pocket dimension's rooms (worldgen.js generatePocket), RealmGen his realm (riftarc.js)
#pragma once
#include "gen/nether_bta.h"
#include <deque>
#include <mutex>
#include <unordered_map>
namespace dq { namespace be { class BETerrain; } }
namespace bta { class NetherGen; }
#include <memory>
#include "gen/generator.h"
#include "gen/structures.h"
#include "gen/trees.h"
#include "gen/carvers.h"

namespace dq {

class DepthsGen : public DimGenerator, public TerrainQuery {
public:
  explicit DepthsGen(const WorldGenOptions& o);
  static constexpr int LAYERS = 104;                 // original rows 0..103 sampled (the band reaches 102.0)
  static constexpr int LAVA_SEA = 28;                // the original's lava sea row (structures' random picks)
  // Minecraft-like layout in band rows: a low lava sea, open caverns over it up to the roof, bedrock at both ends
  static constexpr int LAVA_Y = 50, FLOOR_Y = 44, ROOF_Y = 206;
  static double bias(int y) { return (y < FLOOR_Y ? (FLOOR_Y - y) / 22.0 : 0) + (y > ROOF_Y ? (y - ROOF_Y) / 20.0 : 0); }
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
  // terrain version 5: the five-land heightfield. Version 6: the cave Nether (NetherClimate).
  std::unique_ptr<NetherShape> bta_;
  std::unique_ptr<NetherClimate> cave_;
  CarveRules btaCarve_;
  void generateBTA(Chunk& c);
  void generateCave(Chunk& c);
  // terrain version 7: BTA 8.0.1's own Nether generator, exact (gen/bta via gen/nether_exact.cpp)
  std::shared_ptr<bta::NetherGen> exact_;
  bool exactOpen_ = false;   // terrain 8: the open land (the ground is found from the roof down)
  void initExact(bool open);   // open: terrain 8, BTA's Nether in the open land
public:
  // where to put someone down (terrain 7 and 8: the real generated ground, from the roof down; never in the lava)
  int spawnHeight(int wx, int wz) const override;
private:
  void generateExact(Chunk& c);
  bool solidExact(int x, int y, int z) const;
  TerrainColumn terrainExact(int x, int z) const;
  bool openNether() const override { return bta_ != nullptr || cave_ != nullptr; }
  bool solidBTA(int x, int y, int z) const;
  bool solidCave(int x, int y, int z) const;
  TerrainColumn terrainBTA(int x, int z) const;
  TerrainColumn terrainCave(int x, int z) const;
  void sampleBTA(int wx, int wz, ColumnSample& s) const;
  void sampleCave(int wx, int wz, ColumnSample& s) const;
  void glowstone(GenBuf& b, int cx, int cz) const;     // clusters hanging from the roofs (WorldGenGlowStone)
  void btaFeatures7(GenBuf& b, int cx, int cz, const u8* atSea) const;   // terrain 7: BTA 8.0's floes, terraces, crystals, pillars, ash (gen/nether_bta7.cpp)
  void ores(GenBuf& b, int cx, int cz) const;          // quartz, gold, cinderite, debris veins
  i32 seed_;
  GenBlocks B;
  CarveRules hell_;
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
  double islandMask(int x, int z) const;             // the islands' mask (nCont) — places.js skyTop / dragon lairs test it
  double underside(double mask) const;               // how deep an island hangs under a column
  bool sunwoodAt(int x, int z) const;                // a sunwood tree (gold leaves) rather than a cloud pine
  int skyBiome(int x, int z) const;                  // 0 meadow, 1 forest, 2 grove, 3 woodland
  int hillTop(int x, int z) const;                   // band y an island's top has here wherever the mask allows one
  StructureIndex structures;
  // terrain version 7: The Aether's islands (gen/skylands_aether.cpp): its 3D density, Aether grass over Aether dirt
  // over holystone, its trees, aerclouds, quicksoil shelves, ores and ground cover
  bool aether() const { return aether_ != nullptr; }
  bool aetherColumn(int wx, int wz, int& top, int& bottom) const;   // band y of the highest and lowest land here
  static constexpr int AETHER_DY = 100;              // band y = Aether y + 100: the islands at 118-205, clear of the cloud bank
private:
  i32 seed_;
  GenBlocks B;
  SimplexNoise nCont, nHill, nCave1;
  std::shared_ptr<const class AetherNoise> aether_;
  void generateAether(Chunk& c);
  void sampleAether(int wx, int wz, ColumnSample& out) const;
  void treesAether(int cx, int cz, std::vector<TreeSample>& out) const;
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
  bool island(int x, int z, Isle& out) const;        // WorldGen.endColumn (+ the outer End's craters)
  int endBiome(int x, int z) const;                  // outer End lands, Better End's biomes: chorus forest, blossoming spires, foggy mushroomland, glowing grasslands, crystal mountains, sulphur springs, umbrella jungle, shadow forest, ice starfield, amber land, megalake, lantern woods, painted mountains, neon oasis, dragon graveyards, dry shrubland
  double craterDepth(int x, int z) const;            // how deep a crater bowl dips here (0: none; < 0: its raised rim)
  const std::vector<Pillar>& pillars() const { return pillars_; }
  int fountainY() const;                             // the exit portal's bowl floor + 1 (band-local)
  void outerFeatures(int bx, int bz, class ChunkWriter& P) const;   // spires, arches and floating shards
  void landDress(int bx, int bz, class ChunkWriter& P) const;       // the outer lands' trees, springs, stars and ribs
  // Better End's lands (gen/theend_be.cpp): each land's ground, its trees and growths, its plants
  void beSurface(int land, int x, int z, const Isle& c, class ChunkWriter& P) const;
  void beDress(int bx, int bz, class ChunkWriter& P) const;
  void beStructures(int bx, int bz, class ChunkWriter& P) const;   // Better End's buildings (gen/end_be_structures.cpp)
  // Better End's rebuild of a pillar (its SpikeFeatureMixin): false when it doesn't apply (terrain before 7, no data).
  // Builds it into P when given; topY gets the highest block.
  bool bePillar(const Pillar& pl, class ChunkWriter* P, int* topY) const;
  void bePlants(int land, int x, int z, const Isle& c, class ChunkWriter& P) const;
  bool beSplit(int x, int z) const;
  vec3 spawnPoint() const { return vec3(PLAT_X + 0.5f, PLAT_Y + 1, PLAT_Z + 0.5f); }
  StructureIndex structures;
  // terrain version 7: Better End's own islands (gen/be: its three island layers' SDF density, the domain warp, the
  // hex biome map), outside the dragon's island and its gulf. Each chunk's columns (the top and the lowest bottom of
  // the land standing in them) are kept for island() / sample() / the features.
  bool betterEnd() const { return be_ != nullptr; }
  const class be::BETerrain* beTerrain() const { return be_.get(); }
private:
  bool chorusAt(int wx, int wz, std::vector<std::array<int, 4>>& out) const;
  struct BECols { i16 top[256], bot[256]; i8 land[256]; };
  std::shared_ptr<const BECols> beCols(int cx, int cz, const float* density = nullptr) const;
  i32 seed_;
  GenBlocks B;
  SimplexNoise nA, nB, nC;
  std::vector<Pillar> pillars_;
  std::shared_ptr<const class be::BETerrain> be_;
  mutable std::mutex beMx_;
  mutable std::unordered_map<u64, std::shared_ptr<const BECols>> beCache_;
  mutable std::deque<u64> beOrder_;
};
// the Better End land at a block for a world (gen/be for terrain version 7, else the older noise lands)
int endLandAt(i32 seed, int genVersion, int x, int z);

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

// The land under a column, for sky haze and motes. Same thresholds as EndGen::endBiome and SkylandsGen::skyBiome.
int endLandAt(i32 seed, int x, int z);
int skyLandAt(i32 seed, int x, int z);

} // namespace dq
