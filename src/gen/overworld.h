// gen/overworld.h — the Overworld band: a port of world/worldgen.js (generateOverworld and everything it
// calls) with game/worldupdate.js (rivers, the Ember Crust), game/metals.js (ores) and game/deepdark.js
// (the Deep Dark's halls), scaled into the 256-tall band (gen/gendefs.h vmap).
#pragma once
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
};
struct SurfaceMat { BlockId top, filler, under; int fillDepth; };

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
  int finalSurfaceY(int x, int z) const override { return column(x, z).H; }

  OwColumn column(int x, int z) const;          // the column as generated (structures' levelling applied)
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

private:
  struct BaseCol { int h; double hc; u8 biome; double temp, humid; };
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
