// world/light.h — Cubyz-style light, stored per section in Chunk.
//
// Sun and block light are each RGB bytes (0..255). Every step subtracts 8, so a full light reaches
// about 31 blocks. Full white sun going straight down does not decay (a sun column). Crossing a
// transparent block also subtracts that block's absorption (water takes more red than blue, stained
// glass takes the dye's absorbedLight so the light beyond it is that color). Opaque
// blocks stop both. The gameplay nibble (sky << 4 | block, 0..15) is derived from the brightest
// channel so mobs, spawn and shader-pack eye brightness keep a 0..15 reading.
//
// Light is persistent and incremental:
//   * initialChunkLight() lights a fresh chunk on the worker that generated/loaded it (chunk-local
//     flood fill, like the original's first pass),
//   * LightEngine (a WorldSystem, added to every World) merges light across chunk and band borders
//     when a chunk arrives, and re-floods incrementally (removal + re-propagation BFS) when blocks
//     change. Changes are queued and applied in flush(): every tick, and by the renderer just before
//     it gathers sections to mesh, so a placed torch lights up in the same frame.
#pragma once
#include "world/world.h"
#include <unordered_set>

namespace dq {

u8 skyTopLevel(int dim);            // sky light entering a band from above (0 in the Ember Depths)
void initialChunkLight(Chunk& c);   // worker thread: column sky + chunk-local flood fill + emitters

class LightEngine : public WorldSystem {
public:
  const char* name() const override { return "light"; }
  void tick(World& w, double dt) override { flush(w); }
  void onBlockChanged(World& w, int x, int y, int z, BlockId oldId, BlockId newId) override;
  void onChunkLoaded(World& w, Chunk& c) override;
  void flush(World& w);                       // apply queued changes now
  bool busy() const { return !changes.empty() || !arrivals.empty(); }
  // stats (debug overlay / benchmarks)
  int lastCells = 0; double lastMs = 0;
private:
  struct Node { int x, y, z; u8 c[3]; };
  std::vector<ivec3> changes;
  std::vector<ChunkKey> arrivals;
  std::vector<Node> inc[2], rem[2];
  struct CacheEnt { int dim = -1, cx = 0, cz = 0; Chunk* c = nullptr; };
  CacheEnt cache[64];
  std::unordered_set<SectionKey, SectionKeyHash> touched;
  SectionKey lastTouched{-1, 0, 0, 0};
  World* w = nullptr;
  int cells = 0;
  struct R3 { u8 c[3]{}; };
  static Node nodeAt(int x, int y, int z, const u8* c);
  Chunk* chunkAt(int x, int y, int z);
  R3 getC(int x, int y, int z, int ch);
  void setC(int x, int y, int z, int ch, const R3& v);
  R3 emission(int x, int y, int z, int ch, BlockId id, u8 meta);
  void touch(int x, int y, int z);
  void propagate(int ch);
  void unpropagate(int ch);
  void mergeBorders(Chunk& c);
  void applyChange(const ivec3& p);
};

} // namespace dq
