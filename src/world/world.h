// world/world.h — a realm's blocks: chunk storage, streaming (background generation/loading),
// block access in world coordinates (y 0..1023 across the stacked dimensions), block entities,
// change notification for meshing/lighting/mods, and saving.
#pragma once
#include "world/chunk.h"
#include "gen/generator.h"
#include "common/threadpool.h"
#include <unordered_set>
#include <mutex>

namespace dq {

class RegionStore;

struct StreamAnchor {
  vec3 pos;           // world position (y global)
  int radius;         // chunks
  bool nearDims = true;   // also prepare a neighbouring band when close to its edge (seamless crossing)
};

struct SectionKey {
  int dim, cx, sy, cz;
  bool operator==(const SectionKey& o) const { return dim == o.dim && cx == o.cx && sy == o.sy && cz == o.cz; }
};
struct SectionKeyHash { size_t operator()(const SectionKey& k) const { return ChunkKeyHash()({k.dim * 16 + k.sy, k.cx, k.cz}); } };

// Systems that tick with the world (fluids, random ticks, redstone...). Mods can add their own.
class WorldSystem {
public:
  virtual ~WorldSystem() = default;
  virtual const char* name() const = 0;
  virtual void tick(class World& w, double dt) {}
  virtual void onBlockChanged(class World& w, int x, int y, int z, BlockId oldId, BlockId newId) {}
  virtual void onChunkLoaded(class World& w, Chunk& c) {}
  virtual void onChunkUnloading(class World& w, Chunk& c) {}
  virtual void onBlockEntityChanged(class World& w, int x, int y, int z) {}   // setBlockEntity / blockEntityChanged
};

namespace SetFlags { enum : u32 { None = 0, NoNotify = 1, NoRemesh = 2, NoModify = 4, Urgent = 8, NoLight = 16, FromNet = 32 }; }   // FromNet: applied from the server/a client (net/)

class World {
public:
  World(int realm, std::shared_ptr<WorldGen> gen, const fs::path& saveDir, ThreadPool* pool);
  ~World();
  const int realm;
  std::shared_ptr<WorldGen> gen;
  fs::path saveDir;

  // ---- chunk access ----
  Chunk* chunk(int dim, int cx, int cz) const;
  Chunk* chunkAtBlock(int x, int y, int z) const { return inWorldY(y) ? chunk(dimOfY(y), x >> 4, z >> 4) : nullptr; }
  bool isLoaded(int x, int y, int z) const { return chunkAtBlock(x, y, z) != nullptr; }
  const std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash>& chunks() const { return map; }

  // ---- blocks in world coordinates (y global) ----
  BlockId getBlock(int x, int y, int z) const;
  u8 getMeta(int x, int y, int z) const;
  u8 getLight(int x, int y, int z) const;        // sky<<4 | block; unloaded -> full sky
  bool setBlock(int x, int y, int z, BlockId id, u8 meta = 0, u32 flags = 0);
  bool setMeta(int x, int y, int z, u8 meta, u32 flags = 0);
  BlockId getBlockPhys(int x, int y, int z) const;   // unloaded chunks read as stone (no falling out of the world)
  json* blockEntity(int x, int y, int z, bool create = false);
  void setBlockEntity(int x, int y, int z, json e);   // null erases
  void blockEntityChanged(int x, int y, int z);       // call after editing a blockEntity() in place (chest contents...): saves + syncs it
  void markModified(int x, int y, int z);
  int surfaceY(int x, int z, int dim) const;          // global y of the highest non-air block in that band (-1 none)

  // ---- streaming ----
  void stream(const std::vector<StreamAnchor>& anchors, double budgetMs);
  void requestChunk(int dim, int cx, int cz, float priority);
  void unloadAll(bool save = true);
  int pendingGeneration() const { return (int)inFlight.size(); }
  bool hasNeighbours(const Chunk& c) const;

  // ---- change tracking (renderer / light engine drain these) ----
  std::vector<SectionKey> drainDirtySections();
  void markSectionDirty(int dim, int cx, int sy, int cz, bool urgent = false);
  void markBlockDirty(int x, int y, int z, bool urgent = false);   // + neighbours on section edges
  std::vector<ChunkKey> drainLoaded();     // chunks that arrived since the last call (for light/mesh/LOD)
  std::vector<ChunkKey> drainUnloaded();

  // ---- systems & listeners ----
  void addSystem(std::shared_ptr<WorldSystem> s) { systems.push_back(std::move(s)); }
  template <class T> T* system() { for (auto& s : systems) if (auto p = dynamic_cast<T*>(s.get())) return p; return nullptr; }
  std::vector<std::shared_ptr<WorldSystem>> systems;
  void tickSystems(double dt) { for (auto& s : systems) s->tick(*this, dt); }

  // ---- saving ----
  void saveDirty(bool all = false);        // writes modified chunks (background unless all)
  void flush();                            // wait for pending writes
  std::shared_ptr<RegionStore> regionStoreShared() const { return store; }   // saved chunks (distant terrain reads them on workers)

  u32 changeFlags = 0;     // SetFlags of the setBlock being reported to systems (net/ skips NoModify/FromNet changes)
  RegionStore* regionStore() const { return store.get(); }   // net/: a client keeps the server's chunk records here

  MainQueue mainQueue;   // results from workers (call pumpMain each frame)
  void pumpMain(double budgetMs);

private:
  std::unordered_map<ChunkKey, std::unique_ptr<Chunk>, ChunkKeyHash> map;
  std::unordered_set<ChunkKey, ChunkKeyHash> inFlight;
  std::unordered_set<ChunkKey, ChunkKeyHash> needPopulate;
  std::unordered_set<SectionKey, SectionKeyHash> dirtySecs;
  std::vector<SectionKey> urgentSecs;
  std::vector<ChunkKey> loadedQ, unloadedQ;
  ThreadPool* pool;
  struct Shared { std::shared_ptr<RegionStore> store; std::shared_ptr<WorldGen> gen; std::mutex mx; std::vector<std::unique_ptr<Chunk>> done; };
  std::shared_ptr<Shared> shared;   // background jobs hold this, never the World itself
  std::shared_ptr<RegionStore> store;
  mutable Chunk* lastChunk = nullptr;
  void insertChunk(std::unique_ptr<Chunk> c);
  void unloadChunk(const ChunkKey& k, bool save);
  void tryPopulate(Chunk& c);
};

} // namespace dq
