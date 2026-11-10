// Internal: the composite "World" the BTA decorator runs on (World/Chunk set/notify/schedule semantics)
// and the block logic (BlockLogic* onPlacedByWorld / onNeighborChanged / updateTick) that decoration triggers.
#pragma once
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

#include "bta_nether_internal.h"

namespace bta {

#ifdef BTA_DEBUG_TRACE
bool traceEnabled();
#endif
struct DecoWorld {
  // composite = nx * nz chunks starting at chunk (ox, oz); chunk storage is materialised from the terrain
  // cache on first access (same contents as an eagerly loaded composite, far less memory/terrain work)
  struct Slot {
    uint16_t ids[65536];
    std::unique_ptr<uint8_t[]> meta;  // block data, allocated on the first non-zero write
    uint8_t alloc[16];  // section allocated (ChunkSection.blocks != null)
    uint8_t climate[256];
    void setMeta(int i, int d) {
      if (!meta) {
        if (d == 0) return;
        meta.reset(new uint8_t[65536]());
      }
      meta[i] = (uint8_t)d;
    }
    int getMeta(int i) const { return meta ? meta[i] : 0; }
  };
  int nx = 0, nz = 0;
  int ox = 0, oz = 0;
  mutable std::vector<std::unique_ptr<Slot>> slots;
  bool immediate = false;                 // World.scheduledUpdatesAreImmediate
  std::unordered_set<uint64_t> immSet;    // World.immediatelyUpdatedPositions (pos, block)
  JavaRandom wrand;                       // stand-in for World.rand (BTA: global, unpredictable)
  NetherGen::Impl* g = nullptr;
  int tickDepth = 0;

  void resize(int cx0, int cz0, int w, int h) {
    ox = cx0; oz = cz0; nx = w; nz = h;
    slots.clear();
    slots.resize((size_t)w * h);
  }
  Slot& slotData(int s) const {
    if (!slots[s]) load(s);
    return *slots[s];
  }
  void load(int s) const;
  size_t loadedSlots() const { size_t n = 0; for (auto& p : slots) n += p != nullptr; return n; }

  static bool inBounds(int x, int y, int z) {
    return x >= -32000000 && y >= 0 && z >= -32000000 && x <= 32000000 && y < 256 && z <= 32000000;
  }
  int slot(int x, int z) const {
    int cx = (x >> 4) - ox, cz = (z >> 4) - oz;
    if (cx < 0 || cx >= nx || cz < 0 || cz >= nz) return -1;
    return cz * nx + cx;
  }
  bool chunkLoaded(int cx, int cz) const {
    cx -= ox; cz -= oz;
    return cx >= 0 && cx < nx && cz >= 0 && cz < nz;
  }
  static int lidx(int x, int y, int z) { return (y << 8) | ((z & 15) << 4) | (x & 15); }
  uint16_t get(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return 0;
    int s = slot(x, z);
    if (s < 0) return 0;
    return slotData(s).ids[lidx(x, y, z)];
  }
  int data(int x, int y, int z) const {
    if (!inBounds(x, y, z)) return 0;
    int s = slot(x, z);
    if (s < 0) return 0;
    const Slot& d = slotData(s);
    if (!d.alloc[y >> 4]) return 0;
    return d.getMeta(lidx(x, y, z));
  }
  bool isAir(int x, int y, int z) const { return get(x, y, z) == 0; }
  Mat mat(int x, int y, int z) const { return matOf(get(x, y, z)); }
  bool normalCube(int x, int y, int z) const;
  bool areBlocksLoaded(int x, int z, int r) const {  // World.areBlocksLoaded(pos, r) (x/z only)
    for (int cx = (x - r) >> 4; cx <= (x + r) >> 4; ++cx)
      for (int cz = (z - r) >> 4; cz <= (z + r) >> 4; ++cz)
        if (!chunkLoaded(cx, cz)) return false;
    return true;
  }
  int biome(int x, int y, int z) const;  // World.getBlockBiome

  // Chunk.setBlockIdData(pos, id, data, raw)
  bool setIdData(int x, int y, int z, uint16_t id, int data, bool raw);
  // World.* setters
  bool setBlockType(int x, int y, int z, uint16_t id) { trace('S', x, y, z, id, 0); return setIdData(x, y, z, id, 0, false); }
#ifdef BTA_DEBUG_TRACE
  void trace(char c, int x, int y, int z, int id, int d) const;  // debug log of world writes (matches the Java harness)
#else
  void trace(char, int, int, int, int, int) const {}
#endif
  bool setBlockTypeData(int x, int y, int z, uint16_t id, int d) { trace('T', x, y, z, id, d); return setIdData(x, y, z, id, d, false); }
  bool setBlockTypeNotify(int x, int y, int z, uint16_t id) {
    trace('N', x, y, z, id, 0);
    if (!setIdData(x, y, z, id, 0, false)) return false;
    notifyNeighbors(x, y, z, id);
    return true;
  }
  bool setBlockTypeDataNotify(int x, int y, int z, uint16_t id, int d) {
    trace('U', x, y, z, id, d);
    if (!setIdData(x, y, z, id, d, false)) return false;
    notifyNeighbors(x, y, z, id);
    return true;
  }
  bool setBlockData(int x, int y, int z, int d) { trace('D', x, y, z, -1, d); return setBlockDataRaw(x, y, z, d); }
  bool setBlockDataRaw(int x, int y, int z, int d);
  bool setBlockDataNotify(int x, int y, int z, int d) {
    trace('M', x, y, z, -1, d);
    if (!setBlockDataRaw(x, y, z, d)) return false;
    notifyNeighbors(x, y, z, get(x, y, z));
    return true;
  }
  void notifyNeighbors(int x, int y, int z, uint16_t src);  // NeighborUpdates.diamondD1 (W,E,D,U,N,S)
  void scheduleUpdate(int x, int y, int z, uint16_t block);  // World.scheduleBlockUpdate
};

// block logic entry points
void blockOnPlaced(DecoWorld& w, int x, int y, int z, uint16_t id);
void blockOnNeighborChanged(DecoWorld& w, int x, int y, int z, uint16_t id, uint16_t src);
void blockUpdateTick(DecoWorld& w, int x, int y, int z, uint16_t id, JavaRandom& rand);
bool layerAshCanPlaceAt(const DecoWorld& w, int x, int y, int z);

}  // namespace bta
