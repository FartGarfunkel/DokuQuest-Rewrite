// world/region.h — region files: 32x32 chunks of one dimension band per file
// (<world>/region/d<dim>/r.<rx>.<rz>.dqr). Only modified chunks are stored; untouched ones are
// regenerated from the seed, like the original game. Thread-safe.
#pragma once
#include "world/chunk.h"
#include <mutex>
#include <map>
#include <tuple>

namespace dq {

class RegionStore {
public:
  explicit RegionStore(fs::path dir) : dir(std::move(dir)) {}
  bool load(const ChunkKey& k, std::vector<u8>& out);   // false if not stored
  void store(const ChunkKey& k, std::vector<u8> data);  // in memory; flush() writes
  void flush();
  bool exists(const ChunkKey& k);
  std::vector<ChunkKey> listStored(int dim);            // for LOD of edited areas
private:
  struct Region { std::vector<std::vector<u8>> blobs = std::vector<std::vector<u8>>(1024); bool dirty = false; };
  struct RKey { int dim, rx, rz; bool operator<(const RKey& o) const { return std::tie(dim, rx, rz) < std::tie(o.dim, o.rx, o.rz); } };
  Region& region(const RKey& rk);
  fs::path path(const RKey& rk) const;
  fs::path dir;
  std::mutex mx;
  std::map<RKey, Region> regions;
};

} // namespace dq
