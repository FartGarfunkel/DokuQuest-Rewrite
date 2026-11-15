// gen/floating_land.h — terrain version 23: the land's little floating pieces taken away, its overhangs kept.
// The land is its density's positive cells (gen/density.h: a lattice every 4 blocks across and 8 up, blended between).
// A positive lattice corner belongs to a piece of land; the piece is grounded if it reaches down to the ground (a corner at
// or under GROUND_Y) or spreads wider than REACH lattice steps (a mountain's body, an arch: too big to be a scrap).
// Anything else is a scrap hanging in the air with nothing under it: its corners are taken out before the blocks are
// filled, so it never appears. The answer belongs to the piece (whichever corner it's asked from), so neighbouring chunks
// agree and nothing is cut at a chunk's edge. Safe from the generators' worker threads.
#pragma once
#include "common/common.h"
#include "world/dims.h"
#include <array>
#include <mutex>
#include <unordered_map>

namespace dq {
class DensityGraph;

class FloatingLand {
public:
  FloatingLand(const DensityGraph& g, int field) : g_(g), field_(field) {}
  // the field's value at lattice corner (lx * 4, j * 8 - 1, lz * 4) (band y), remembered
  double value(int lx, int j, int lz) const;
  // a positive corner of a piece of land with nothing under it (false for a negative corner, or one of grounded land)
  bool floating(int lx, int j, int lz) const;
  static constexpr int GROUND_Y = SEA_LEVEL - 16;   // band y: land reaching this low is the ground's
  static constexpr int REACH = 12;                  // lattice steps (48 blocks): a piece spreading wider is kept
  static constexpr int NY = DIM_H / 8 + 1;
private:
  static u64 key(int lx, int j, int lz) { return ((u64)(u32)lx << 32) | (u64)((((u32)lz & 0x3FFFFFFu) << 6) | (u32)(j & 63)); }
  struct Shard { std::mutex m; std::unordered_map<u64, double> v; std::unordered_map<u64, u8> f; };
  Shard& shard(u64 k) const { return shards_[(k * 0x9E3779B97F4A7C15ull) >> 58]; }
  const DensityGraph& g_;
  int field_;
  mutable std::array<Shard, 64> shards_;
};

} // namespace dq
