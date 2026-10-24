// gen/generator.h — world generation interface. One DimGenerator per stacked dimension band.
// Generators must be thread-safe for concurrent generate()/sample() calls on different chunks
// (they run on worker threads): keep per-call state on the stack.
#pragma once
#include "world/chunk.h"
#include "common/noise.h"

namespace dq {

// What distant terrain (LOD) needs to know about one column without generating the chunk.
struct ColumnSample {
  i16 height = 0;          // band-local y of the top solid block (-1 = nothing, e.g. void)
  i16 water = -1;          // band-local y of the water/lava surface above it (-1 = none)
  BlockId top = AIR;       // top block (grass, sand, snow...)
  BlockId under = AIR;     // block under the top (dirt, stone...) for cliff sides
  BlockId fluid = AIR;     // water or lava block id when water >= 0
  u8 tree = 0;             // 0 none, else tree kind index (generator-defined) with canopy at treeTop
  i16 treeTop = 0;         // band-local y of canopy top
  BlockId leaves = AIR;    // canopy block
  u32 tint = 0xFFFFFFFF;   // grass/foliage tint (0xAABBGGRR) from climate, 0xFFFFFFFF = none
  // floating layers (Skylands islands, End islands): bottom of the upper mass (or -1)
  i16 floatBottom = -1;
  u8 temp = 128, humid = 128;  // the column's climate bytes (Chunk::climate): tint = climateTint(kind, temp, humid)
  u8 biome = 255;              // Biome (gen/gendefs.h) for the Overworld, 255 elsewhere
};

// A generated tree, column by column (for distant terrain near the player: game/distant.js distTreeCols).
struct TreeColumn { i8 dx, dz; i16 top, leafBottom; BlockId leaves, log; };   // top = highest part + 1; leafBottom 999 = no leaves
struct TreeSample { int x, y, z; u8 kind; bool bare; std::vector<TreeColumn> cols; };

class DimGenerator {
public:
  virtual ~DimGenerator() = default;
  virtual void generate(Chunk& c) = 0;                               // fill blocks (band-local), climate, block entities
  virtual void sample(int wx, int wz, ColumnSample& out) const = 0;  // cheap column estimate for LOD
  virtual int spawnHeight(int wx, int wz) const { ColumnSample s; sample(wx, wz, s); return s.height + 1; }
  // decorations that need neighbours (trees crossing chunk edges, structures): called once all
  // 8 neighbours of a chunk exist; default none.
  virtual void populate(class World& w, Chunk& c) {}
  // the trees the generator plants in chunk (cx, cz) (whole trees, also parts reaching into neighbours)
  virtual void trees(int cx, int cz, std::vector<TreeSample>& out) const {}
  // raw terrain queries for structures and gameplay (gen/structures.h); null for void generators
  virtual const class TerrainQuery* terrain() const { return nullptr; }
};

struct WorldGenOptions {
  i32 seed = 0;
  int genVersion = 3;                  // C++ terrain generation version
  std::string worldType = "default";   // default | flat | amplified | ... (from the create-world screen)
  json extra;                          // structures on/off, bonus chest, etc.
};

class WorldGen {
public:
  explicit WorldGen(const WorldGenOptions& o);
  WorldGenOptions opts;
  std::unique_ptr<DimGenerator> dims[DIM_COUNT];
  DimGenerator* forDim(int d) const { return d >= 0 && d < DIM_COUNT ? dims[d].get() : nullptr; }
  // set by the worldgen implementation (gen/worldgen.cpp); overridable by mods ("world types")
  using Factory = std::function<std::unique_ptr<DimGenerator>(int dim, const WorldGenOptions&)>;
  static void registerType(const std::string& worldType, Factory f);
  static std::vector<std::string> types();
};

} // namespace dq
