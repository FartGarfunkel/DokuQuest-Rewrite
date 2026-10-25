// sim/explosion.h — blowing a round hole (game/blockfx.js explode). Written by the creatures workstream for
// fuselings because the gameplay workstream's TNT wasn't in this tree; gameplay's TNT should call this (or
// replace it at merge). Hurts and knocks back players and creatures, breaks blocks unless noBlocks
// (30% of them drop), primes nothing (TNT chains belong to the TNT code: onBlock hook below).
#pragma once
#include "sim/level.h"

namespace dq {

struct ExplodeOpts {
  bool noBlocks = false;
  u32 source = 0;   // entity that went off (not hurt by its own blast)
  // called for each block the blast takes before it is removed; return false to keep it (TNT priming etc.)
  std::function<bool(int x, int y, int z, BlockId id)> onBlock;
};
void explode(Level& lv, int realm, vec3 p, float power, const ExplodeOpts& o = {});

} // namespace dq
