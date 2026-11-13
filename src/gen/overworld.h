// gen/overworld.h — the Overworld band: a port of world/worldgen.js (generateOverworld and everything it
// calls) with game/worldupdate.js (rivers, the Ember Crust), game/metals.js (ores) and game/deepdark.js
// (the Deep Dark's halls), scaled into the 256-tall band (gen/gendefs.h vmap).
#pragma once
#include <memory>
#include "gen/generator.h"
#include "gen/structures.h"
#include "gen/trees.h"
#include "gen/carvers.h"

namespace dq {

struct OwColumn {
  int h = 0;            // the original's height (JS column().h with rivers and levelling), 128-world units
  int H = 0;            // band-local height of the top block
  u8 biome = 0;
  double temp = 0, humid = 0;
  float river = 0;      // 1 mid-stream, 0.5 on the banks
  u8 special = 0;       // terrain version 14: 1 a mushroom island (gen/overworld.cpp)
  u16 mc = 0xFFFF;      // terrain version 15: Soulrend's biome here (gen/mc_biomes.h index; 0xFFFF older terrain)
};
// Biome variants inspired by Better than Adventure (our own designs), carved out of the climate: the hottest, driest
// savanna turns to Redlands (red sand, spinifex, pale gum trees), the coldest taiga and peaks to Glacier (packed and
// blue ice, ice spires), mild damp plains to Meadow (flowers everywhere), wet lowland to Mossmire (moss, mud, dark trees).
enum BtaVariant : u8 { BV_NONE = 0, BV_REDLANDS, BV_GLACIER, BV_MEADOW, BV_MOSSMIRE, BV_THORNSCRUB, BV_HIGHLANDS, BV_TUNDRA, BV_SEASONAL, BV_MUSHROOM };
// terrain version 15: each of Soulrend's biomes as one of our land kinds, by its name (OverworldGen fills it once; the same
// data gives every world the same list)
inline std::vector<u8>& mcLandKinds() { static std::vector<u8> v; return v; }
inline BtaVariant btaVariant(const OwColumn& c) {
  if (c.special == 1) return BV_MUSHROOM;   // (terrain version 14: Minecraft's mushroom fields, out in the deep sea)
  if (c.mc != 0xFFFF) return c.mc < mcLandKinds().size() ? (BtaVariant)mcLandKinds()[c.mc] : BV_NONE;   // (terrain version 15: by Soulrend's biome)
  if (c.biome == BIOME_PLAINS && c.temp > 0.5 && c.humid < -0.18) return BV_REDLANDS;
  if (c.biome == BIOME_PLAINS && c.temp > 0.32 && c.humid >= -0.16 && c.humid < -0.04) return BV_THORNSCRUB;   // BTA's Caatinga
  if ((c.biome == BIOME_TAIGA || c.biome == BIOME_MOUNTAINS) && c.temp < -0.58) return BV_GLACIER;
  if ((c.biome == BIOME_PLAINS || c.biome == BIOME_FOREST) && c.humid > 0.46 && c.temp > -0.15 && c.temp < 0.24 && c.h <= vmap::JS_SEA + 5 && c.h > vmap::JS_SEA) return BV_MOSSMIRE;
  if (c.biome == BIOME_PLAINS && c.temp > 0.02 && c.temp < 0.26 && c.humid > 0.05 && c.humid < 0.28) return BV_MEADOW;
  // BTA's Highland Spruce (Boreal): tall spruce woods on podzol up in the cool hills
  if ((c.biome == BIOME_TAIGA || c.biome == BIOME_MOUNTAINS) && c.temp > -0.45 && c.temp < -0.08 && c.h > vmap::JS_SEA + 16 && c.h < vmap::JS_SEA + 52) return BV_HIGHLANDS;
  // BTA's Seasonal Forest: mild, damp lowland groves of cherry whose leaves turn with the year (sim/worldsys.cpp)
  if ((c.biome == BIOME_FOREST || c.biome == BIOME_PLAINS) && c.temp > 0.04 && c.temp < 0.3 && c.humid > 0.28 && c.humid < 0.46 && c.h > vmap::JS_SEA + 1 && c.h < vmap::JS_SEA + 22) return BV_SEASONAL;
  // the tundra step between the snowy woods and the plains: open snowy grass over permafrost, hardly a tree
  if (c.biome == BIOME_PLAINS && c.temp < -0.32) return BV_TUNDRA;
  return BV_NONE;
}
struct SurfaceMat { BlockId top, filler, under; int fillDepth; };

std::unique_ptr<DimGenerator> makeUnderworldGen(const class OverworldGen* ow, int depth);   // gen/underworld.cpp
class OverworldGen : public DimGenerator, public TerrainQuery {
public:
  explicit OverworldGen(const WorldGenOptions& o);
  void generate(Chunk& c) override;
  void sample(int wx, int wz, ColumnSample& out) const override;
  void trees(int cx, int cz, std::vector<TreeSample>& out) const override;
  const TerrainQuery* terrain() const override { return this; }
  // TerrainQuery
  int dim() const override { return DIM_OVERWORLD; }
  i32 seed() const override { return seed_; }
  TerrainColumn terrainColumn(int x, int z) const override;
  int genVersion() const override { return genVer_; }
  int finalSurfaceY(int x, int z) const override { return column(x, z).H; }

  OwColumn column(int x, int z) const;          // the column as generated (structures' levelling applied)
  std::string mcBiomeName(int x, int z) const;
  std::string mcBiomeNameAt(int x, int y, int z) const;   // (at a depth: Minecraft's y)  // terrain version 15: Soulrend's biome at the ground here ("" before)
  double climateScale = 1;                      // how wide the climate (and so the biomes) spreads: 2 from terrain version 4
  // BTA's world types on this land (the create-world screen): amplified, islands, winter (Endless Winter), hell, floating
  std::string wtype;
  void worldTypePass(GenBuf& b, const OwColumn* cols, int cx, int cz) const;   // gen/world_types.cpp
  int floatingBottom(int wx, int wz, int H) const;   // floating islands: the underside here (-1: no island)
  bool floes = false;
  bool jigsawVillages = false;   // terrain version 9: villages of our jigsaw pieces (gen/structures_jigsaw_village.cpp)
  bool villageStyles = false;    // terrain version 10: villages in every style (beach, jungle, swamp, mesa, igloo, festive...) by the land
  bool looseGround = false;      // immersive worlds only: loose rocks, flint and twigs (gen/decorate.cpp)
  bool dynTrees = false;         // terrain version 13: grown trees (gen/dyntree_gen.cpp, sim/dyntree.h)
  bool rockLayers = false;       // terrain version 12: rock layers and ores in their rocks (gen/rock_layers.cpp)
  std::vector<u8> zincRep_, bismuthRep_, tinRep_, silverRep_, goldRep_;
  BlockId layerRock(int wx, int y, int wz, int H) const;
  void initRockLayers();
  const u8* oreHost(BlockId ore) const;   // where this ore may lie (nullptr: wherever ores do)
  bool villagePlan11 = false;    // terrain version 11: the village plan's fixes (gen/generator.h); older worlds keep their plans
  OwColumn rawColumn(int x, int z) const;       // terrain and rivers only
  int biomeAt(int x, int z) const { return column(x, z).biome; }
  bool goodSpawn(int x, int z) const;           // a dry, gentle spot (WorldGen.goodSpawn)
  bool riverFlow(int x, int z, double& fx, double& fz) const;   // which way a river runs here (false: no river)
  double deepZone(int x, int z) const;          // 0..1, how far into the Deep Dark (game/deepdark.js)
  SurfaceMat surface(const OwColumn& c, int wx, int wz) const;   // the column's top/filler/under blocks
  void treesForChunk(int cx, int cz, std::vector<TreeSpec>& out) const;
  bool treeGroundOk(const TreeSpec& t) const;   // would the generator plant it (soil under it)
  const GenBlocks& blocks() const { return B; }

  StructureIndex structures;
  // the deep world (terrain version 19, layout 2: docs/WORLD_DEPTH.md): no floor under the land; the Underworld's
  // bands below are generated here (gen/underworld.cpp), depth 1 just under it
  bool deep_ = false;
  void generateDeep(Chunk& ch, int depth) const;

  // terrain version 14 (docs/WORLDGEN_MERGE.md): the land shaped by Minecraft 1.20's noise router (gen/density.h)
  bool v14 = false;
  int v14Surface(int x, int z) const;   // band y of the land's top here (its terrain without the caves)

private:
  std::shared_ptr<class DensityGraph> vg_;
  int vSloped = -1, vFinal = -1, vCont = -1, vEro = -1, vOff = -1, vJag = -1;
  void fillV14(GenBuf& b, OwColumn* cols, int cx, int cz) const;
  bool v14Whole(int x, int z, int H) const;
  int v14Spike(int x, int z, int H) const;   // (the planned ground height is solid land here)
  int v14Corner(int x, int z) const;   // (the exact surface at a cell corner)
  struct BaseCol { int h; double hc; u8 biome; double temp, humid; int H = -1; u8 special = 0; u16 mc = 0xFFFF; };
  // terrain version 15: Soulrend's world (gen/mc_biomes.h, gen/surface_rules.h; data/soulrend_worldgen)
  bool v15 = false, v16 = false, v17 = false, v18 = false;
  double hf(i32 a, i32 b, i32 c, i32 d) const { return v18 ? posHashF(a, b, c, d) : hashFloat(a, b, c, d); }   // (18: no stripes)
  int genVer_ = 0;
  std::shared_ptr<class McBiomes> mcb_;
  std::shared_ptr<class McSurface> mcs_;
  std::shared_ptr<class McFeatures> mcf_;   // (Soulrend's biome features: its plants, disks, rocks, icebergs, snow)
  int vTemp = -1, vVeg = -1, vDepth = -1, vRidges = -1, vInit = -1;
  // the land's extras before its surface: Minecraft's eroded badlands spires and frozen-ocean icebergs (SurfaceSystem's
  // extensions), Nature's Spirit's sugi and stratified-desert pillars (its SurfaceBuilderMixin): their noises and biomes
  int nX_[12] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
  int bEroded_ = -1, bFrozen_ = -1, bDeepFrozen_ = -1, bSugi_ = -1, bSugi2_ = -1, bStrat_ = -1, bLively_ = -1, bBlooming_ = -1;
  void surfaceExtras(GenBuf& b, int x, int z, int wx, int wz, int top, int prelim) const;
  std::vector<u8> mcCat_;                 // per biome: ours (BIOME_*)
  std::vector<float> mcTemp_, mcHumid_;   // per biome: our climate numbers for it (tints, snow, our systems)
  int mcNoiseBiome(int qx, int qy, int qz) const;   // the biome at a quart (y Minecraft's)
  int mcBiomeAt(int x, int y, int z) const;         // at a block (BiomeManager's zoom; y Minecraft's)
  int prelimSurface(int x, int z) const;            // Minecraft's preliminary surface level (y Minecraft's)
  void surfaceV15(GenBuf& b, OwColumn* cols, int cx, int cz) const;
  BaseCol baseColumn(int x, int z) const;
  double riverNoise(int x, int z) const;
  // gen/underground.cpp: Minecraft Beta style underground (carvers, ore veins, lava lakes, springs)
  void initUnderground();
  void carve(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  void placeOres(GenBuf& b, int cx, int cz, Mulberry32& rnd) const;
  void placeLakes(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  void placeSprings(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  // gen/dungeons_classic.cpp: Minecraft Alpha/Beta monster rooms (cobblestone, a cage, a chest or two)
  void placeDungeons(GenBuf& b, Chunk& ch, const OwColumn* cols) const;
  void placeTrees(GenBuf& b, int cx, int cz) const;
  void writeTree(GenBuf& b, int cx, int cz, const TreeSpec& t, std::vector<TreePart>& parts) const;
  void decorate(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  void decorate2(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  void decorateCaves(GenBuf& b, const OwColumn* cols, int cx, int cz) const;
  void placeOddities(GenBuf& b, int cx, int cz) const;
  void carveDeep(GenBuf& b, int cx, int cz) const;

  i32 seed_;
  GenBlocks B;
  CarveRules carve_;
  std::vector<u8> rockRep_, oreRep_;   // what rock blobs / ores may replace
  SimplexNoise nCont, nHill, nDetail, nMount, nRidge, nTemp, nHumid, nCave1, nCave2, nCave3, nFloor, nRiver, nDeep, nDeepC;
};

} // namespace dq
