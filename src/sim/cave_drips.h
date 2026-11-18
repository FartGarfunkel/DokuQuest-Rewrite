// sim/cave_drips.h — what drips off a stalactite: a hanging pointed dripstone's column drips what lies over the stone it
// hangs from (water, or lava), slowly, and quicker the longer the column; the drops fall to whatever is under the tip,
// and a cauldron there fills with it (water a level at a time, lava a little slower: a cauldron of lava is meta & 4).
#pragma once
#include "world/world.h"

namespace dq {

struct CaveDrip {
  int liquid = 0;   // 0: nothing over it (no drips), 1: water, 2: lava
  int length = 0;   // the column's length, in blocks (1 for a lone tip)
};
// (x, y, z) a hanging pointed dripstone: if it's a column's tip (air or anything not dripstone under it), what drips off it
CaveDrip caveDripAt(const World& w, int x, int y, int z);
// a random tick of a hanging pointed dripstone (sim/worldsys.cpp): the cauldron under its tip, within 11 blocks, may fill
void caveDripTick(World& w, int x, int y, int z, double rnd01);
// a cauldron's contents (meta): its level 0..3 and whether it's lava
inline int cauldronLevel(u8 meta) { return meta & 3; }
inline bool cauldronLava(u8 meta) { return (meta & 4) != 0; }
inline int cauldronWater(u8 meta) { return cauldronLava(meta) ? 0 : (meta & 3); }

} // namespace dq
