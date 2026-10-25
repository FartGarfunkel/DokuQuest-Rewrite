// gen/structures.h — the hook for structures (villages, towns, dungeons, strongholds, ruins, fortresses,
// End cities, ancient cities...). The terrain generators call it; the structures themselves are ported
// in their own workstream and registered with registerStructure() from an explicit register function
// (never a static constructor).
//
// Placement follows the original game/structures.js: the world is cut into regions of `region` x `region`
// blocks per type; each region gets at most one candidate, decided by a mulberry32 seeded with
// lairSeed(seed, rx, rz, 9100 + typeIndex*37 + jsDim), placed `margin` in from the region's edges; plan()
// decides if and how it goes there; a plan that comes within its clearance of a structure of an earlier
// registered type is dropped. Every generator (one per band and world) owns a StructureIndex that caches
// plans; build() writes the parts of every plan that overlap a chunk while that chunk is generated (on a
// worker thread: plan()/build()/flatten() must be thread-safe and deterministic).
#pragma once
#include "gen/gendefs.h"
#include <mutex>
#include <unordered_map>

namespace dq {

// What a placer may ask about the raw terrain of one band (no structures applied: no flattening).
struct TerrainColumn {
  int y = -1;           // band-local y of the top solid block (-1 = none: void, open sky)
  int water = -1;       // band-local y of the water/lava surface above it (-1 = none)
  int biome = -1;       // Biome (Overworld only)
  double temp = 0, humid = 0;
  int jsY = -1;         // the original game's height for this column (its own 128-tall coordinates)
  bool river = false;
};
class TerrainQuery {
public:
  virtual ~TerrainQuery() = default;
  virtual int dim() const = 0;                                   // DIM_*
  virtual i32 seed() const = 0;
  virtual TerrainColumn terrainColumn(int x, int z) const = 0;
  int surfaceY(int x, int z) const { return terrainColumn(x, z).y; }
  // raw solidity of the terrain at a band-local point (the Depths' caverns, islands, Overworld columns)
  virtual bool solidAt(int x, int y, int z) const { TerrainColumn c = terrainColumn(x, z); return c.y >= 0 && y <= c.y; }
  // the ground as finally generated (with structures' levelling): for build(), never from plan()
  virtual int finalSurfaceY(int x, int z) const { return surfaceY(x, z); }
};

struct StructurePlan {
  int type = -1;            // index in structureTypes()
  std::string id;           // "s<type>_<dim>_<rx>_<rz>" (stable: save books, maps, compasses)
  std::string name;         // "a Nether Fortress"
  int x0 = 0, z0 = 0, x1 = 0, z1 = 0;   // footprint, world x/z inclusive
  int y = 0;                // band-local base height (placer-defined)
  int cx = 0, cz = 0;       // centre
  int rot = 0;
  u32 seed = 0;             // lairSeed(seed, rx, rz, 9200 + type)
  json data;                // anything else the placer keeps (rooms, spawners...)
};

// Writes one structure into the chunk being generated (the original sPut). y is band-local.
class ChunkWriter {
public:
  ChunkWriter(struct GenBuf& buf, Chunk& ch, const StructurePlan& S, const TerrainQuery& t)
    : buf(buf), ch(ch), S(S), terrain(t), bx(ch.cx * 16), bz(ch.cz * 16) {}
  GenBuf& buf; Chunk& ch; const StructurePlan& S; const TerrainQuery& terrain;
  const int bx, bz;
  bool in(int x, int z) const { return x >= bx && x < bx + 16 && z >= bz && z < bz + 16; }
  int put(int x, int y, int z, BlockId id, u8 meta = 0);              // returns colIdx or -1; clears a block entity there
  int put(int x, int y, int z, const char* key, u8 meta = 0) { return put(x, y, z, reg().blockId(key), meta); }
  BlockId get(int x, int y, int z) const;                             // 0xFFFF outside this chunk
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, BlockId id, u8 meta = 0);
  void fill(int x0, int y0, int z0, int x1, int y1, int z1, const std::function<BlockId(int, int, int)>& f, u8 meta = 0);
  double h(int x, int y, int z, int s = 0) const { return hashFloat(x * 31 + s * 7919, y + s * 131, z, (i32)S.seed); }
  template <class V> const auto& pick(const V& list, int x, int y, int z, int s = 0) const { return list[(size_t)std::floor(h(x, y, z, s) * list.size())]; }
  void blockEntity(int x, int y, int z, json be);                     // chests, spawners, signs... (if in this chunk)
  int ground(int x, int z) const { return terrain.finalSurfaceY(x, z); }
};

constexpr u32 SF_SURFACE = 1;    // stands on the surface: trees are kept 3 blocks off its footprint
constexpr u32 SF_NO_CARVE = 2;   // the Deep Dark's halls are not carved inside its footprint (ancient cities)

struct StructureType {
  std::string key, name;
  int region = 512, margin = 40;   // one candidate per region, kept `margin` from the region's edges
  double chance = 0.5;             // chance a region has one
  int clearance = 6;               // later types keep this far (plus their own radius) from this one
  u32 dims = 1u << DIM_OVERWORLD;  // bit per stacked band (DIM_*)
  u32 realms = 1u << REALM_MAIN;   // bit per realm (side worlds have their own generators)
  u32 flags = 0;                   // SF_*
  // where/how: (x, z) is the region's candidate point; fill `out` (footprint, y, ...) and return true, or false = none
  std::function<bool(const TerrainQuery& t, int x, int z, Mulberry32& rnd, StructurePlan& out)> plan;
  // write the part of the plan that falls in w's chunk
  std::function<void(ChunkWriter& w, const StructurePlan& s)> build;
  // optional ground levelling (villages on level ground): weight 0..1 toward band-local targetY at (x, z);
  // only asked for points within flattenPad of the footprint
  std::function<bool(const StructurePlan& s, int x, int z, double& weight, int& targetY)> flatten;
  int flattenPad = 0;
};

int registerStructure(StructureType t);              // returns the type index (registration order = priority)
const std::vector<StructureType>& structureTypes();

class StructureIndex {
public:
  StructureIndex(const TerrainQuery& t, int realm = REALM_MAIN);
  bool active() const { return !types.empty(); }      // any structure types for this band at all
  const StructurePlan* at(int type, int rx, int rz) const;                    // the plan for a region (cached) or null
  void near(int x, int z, int pad, std::vector<const StructurePlan*>& out, int type = -1) const;
  void build(GenBuf& buf, Chunk& c) const;                                    // every plan overlapping the chunk
  bool flatten(int x, int z, double& weight, int& targetY) const;            // the strongest levelling here
  bool flagged(int x, int z, int pad, u32 flag) const;                        // inside (pad) a structure with flag
private:
  const TerrainQuery& t;
  int realm;
  std::vector<int> types;                  // type indices that apply to this band/realm
  bool anyFlatten = false;
  mutable std::mutex mx;
  mutable std::unordered_map<u64, std::unique_ptr<StructurePlan>> cache;   // null = no structure there
};

} // namespace dq
