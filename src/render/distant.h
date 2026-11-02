// render/distant.h — distant terrain (game/distant.js), built like Cubyz's far land (see distant_build.cpp):
// past the loaded chunks the land goes on as textured voxels, 2 blocks across just outside them, then
// 4, 8, 16 ... 512 blocks, out to settings.farTerrain (up to 16384 blocks). Tiles of 32x32 columns
// in a quadtree around the camera, coarse ones first so there is never a hole, finer ones as they're
// made; each built on a worker thread from DimGenerator::sample() and from summaries of real chunks
// (loaded this session, or stored in the save) so builds show from afar. One draw per tile, cut away
// chunk by chunk where the real chunks are drawn. One quadtree per band of the stacked world: from the
// Skylands the Overworld shows far below, from the Overworld the Skylands' islands overhead.
#pragma once
#include "render/frame.h"
#include "world/world.h"
#include <unordered_set>
#include <mutex>

namespace dq {

class Level;
struct SkyEnv;
struct TileSet;

namespace lod {
constexpr int CELLS = 32;                 // columns across a tile
constexpr int MAX_LEVEL = 8;              // levels 0..8: columns of 2 .. 512 blocks, tiles of 64 .. 16384
inline int cellSize(int L) { return 2 << L; }
inline int tileSize(int L) { return CELLS * cellSize(L); }

// A real chunk brought down to voxels 2x2 blocks across and one block high (Cubyz: each LOD level made from the one
// under it; render/distant_build.cpp summarizeChunk): per cell, its solid runs bottom up, each one block type.
struct ChunkSummary {
  struct Run { u8 y0, y1; u16 id; };   // band-local, inclusive
  u16 start[65] = {};                  // cell q's runs: runs[start[q] .. start[q + 1])
  std::vector<Run> runs;
  u8 temp[64] = {}, humid[64] = {};    // climate (grass and foliage tints)
  bool generated = false;              // made by the generator for the far land alone (kept in the world's lod_far.bin)
};

// Average colours of every block from the current tiles (resource pack aware), shared with workers.
struct ColorTable {
  std::vector<u32> top, side;   // 0xBBGGRR
  std::vector<u8> tint;         // 0 none, 1 grass, 2 foliage, 3 water
  std::vector<u8> isLog;
  BlockId water = 0, lava = 0, dirt = 0, ice = 0;
};

// 12 bytes (it was 16: the far land is most of a long view's vertices). x,z: tile/32768 units, y: 1/8 block; tile: the
// block face's texture layer; rgb: its tint as 5-6-5; pk: bits 0..3 flags (1 water, 2 glows, 4 tint every pixel,
// 8 average colour only), 4..6 the face (+x -x +y -y +z -z), 7..14 the shade
struct LodVertex { u16 x, z; i16 y; u16 tile; u16 rgb; u16 pk; };
static_assert(sizeof(LodVertex) == 12, "LOD vertex must be 12 bytes");

struct TileInput {
  int dim = 2, L = 0, tx = 0, tz = 0;
  std::shared_ptr<WorldGen> gen;
  std::shared_ptr<const ColorTable> colors;
  std::vector<std::shared_ptr<const ChunkSummary>> sums;   // per cell (W*W, or 4 a cell past level 0: its quarters) or empty
};
struct TileOutput { std::vector<LodVertex> verts; float minY = 0, maxY = 0; double ms = 0; };

void buildTile(const TileInput& in, TileOutput& out);                                       // worker thread
std::shared_ptr<ChunkSummary> summarizeChunk(const Chunk& c, const ColorTable& ct);        // any thread (reads only c)
std::shared_ptr<ColorTable> makeColorTable(const TileSet& tiles);
} // namespace lod

class DistantTerrain {
public:
  DistantTerrain();
  ~DistantTerrain();
  // per frame: what to draw from here, what to make next, results to upload
  void update(Level& lv, World& w, const FrameCtx& f, ThreadPool& pool, double dt, const TileSet& tiles);
  void draw(const FrameCtx& f, const SkyEnv& env, const TileTextures& tex);
  void reset();                 // forget everything (world change, pack change, option off)
  float range = 0;              // how far it reaches this frame (blocks)
  bool adaptive = true;         // draw closer by itself when the frame rate drops (distantFor)
  float realR = 1e9f;           // the chunks are only drawn this far (the terrain memory budget): past it, the far land fills in
  // stats (debug overlay, docs/port/lod.md)
  int drawn = 0, tilesHeld = 0, building = 0, built = 0;
  size_t gpuBytes = 0, summaries = 0, generated = 0;
  double buildMsTotal = 0, buildMsMax = 0;
private:
  struct Tile;
  struct Band;
  struct Done { int dim, L, tx, tz; lod::TileOutput out; };
  struct Shared { std::mutex mx; std::vector<Done> done; std::vector<std::pair<ChunkKey, std::shared_ptr<lod::ChunkSummary>>> sums, gen; int genBatches = 0; bool cacheDone = false; std::vector<ChunkKey> stored; bool storedListed = false; };
  void select(Band& b, double cx, double cz);
  void submit(Band& b, int L, int tx, int tz, ThreadPool& pool, float pri);
  void scanChunks(World& w, double now);
  void addSummary(const ChunkKey& k, std::shared_ptr<lod::ChunkSummary> s, bool quiet);
  void markDirty(int dim, int cx, int cz);
  // Cubyz makes its far land with the world generator itself: the chunks nobody has been to are generated on a worker
  // (caves, ravines, villages, lakes, trees), brought down to voxels and kept, nearest first
  void genFar(World& w, int dim, glm::dvec3 cam, ThreadPool& pool);
  void trimSummaries(glm::dvec3 cam);
  // the far land generated so far is kept in the world's folder (lod_far.bin), so it's there at once next time
  void saveFarCache();
  fs::path cacheFile; std::string cacheTag; bool cacheLoaded = true;
  std::unordered_set<ChunkKey, ChunkKeyHash> genTried;
  int genInFlight = 0, genCx = 1 << 30, genCz = 0, genDim = -1, genRing = 0, genIdx = 0;
  void updateMask(Band& b, const FrameCtx& f, int maskR);
  bool chunkReal(int dim, int cx, int cz) const;
  std::unique_ptr<Band> bands[DIM_COUNT];
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  std::shared_ptr<const lod::ColorTable> colors;
  std::string colorsFor;
  std::unordered_map<ChunkKey, std::shared_ptr<const lod::ChunkSummary>, ChunkKeyHash> sums;
  std::unordered_map<u64, int> sumRegions;   // (dim, 16x16-chunk region) -> summaries in it
  struct Seen { const Chunk* ptr; u32 hash; double first; u64 stamp; };
  std::unordered_map<ChunkKey, Seen, ChunkKeyHash> seen;
  std::vector<ChunkKey> scanOrder; size_t scanPos = 0;
  std::shared_ptr<WorldGen> gen;
  const World* worldFor = nullptr;
  std::shared_ptr<RegionStore> store;
  bool storedRequested = false; size_t storedPos = 0; int storedInFlight = 0;
  std::vector<ChunkKey> storedKeys;
  Level* lvl = nullptr;
  u64 frame = 0;
  int inFlight = 0;
  float adaptScale = 1, adaptLow = 0, adaptHigh = 0;
  Shader shader;
  bool glReady = false;
  size_t maxBytes = 64u << 20;
  double clock = 0;
  glm::dvec3 camAt{0};
};

} // namespace dq
