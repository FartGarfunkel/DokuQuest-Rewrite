// sim/dyntree.h — trees that grow (after Dynamic Trees, github.com/DynamicTreesTeam/DynamicTrees, MIT; docs/CREDITS.md).
// In a terrain-13 world (rule dynamicTrees) a sapling takes root: rooty soil under a twig of its wood's branch block. The
// soil's random tick sends the tree one growth signal (GrowSignal): energy 16, up from the trunk's foot, turning at each
// branch by Dynamic Trees' weights (up 2, the way it was going +1, air 1, leaves 2, a branch of the tree its radius + 2;
// only up below the third block). Where it ends the tree grows: a twig leafs out, a thicker branch (or a leaf it reaches)
// branches out into a new twig with leaves round it. On the way back every branch it passed takes the pipe model's
// radius: sqrt(the areas of the branches joining it) + 0.3, never thinner than it was, at most 8 (a whole block).
// Each growth costs the soil its life now and then (1 in 8); dead soil grows no more; soil with no tree is dirt again.
//
// A branch block's meta: bit 4 set = a growing tree's branch, bits 1..3 its radius - 1 (1..8 of 8ths of a block's half).
// Branch blocks without bit 4 are the generated branches of before (bit 0 thin), as they were.
#pragma once
#include "sim/level.h"

namespace dq {

constexpr u8 DYN_BRANCH = 0x10;
inline u8 branchMeta(int r) { r = r < 1 ? 1 : r > 8 ? 8 : r; return (u8)(DYN_BRANCH | ((r - 1) << 1)); }
inline int dynRadius(u8 meta) { return ((meta >> 1) & 7) + 1; }
// a tree part's radius (1..8): a growing tree's branch its own, a whole log 8, an old branch 3 or 4, else 0
int branchRadius(BlockId id, u8 meta);

bool dynTreesOn(Level& lv);
// the sapling at (x, y, z) takes root (from growTree, sim/worldsys.cpp): false if it can't (its wood has no branch block)
bool plantDynTree(Level& lv, World& w, int x, int y, int z);
// one growth signal from the rooty soil at (x, y, z) (its random tick; bone meal): true if the tree grew
bool growDynTree(Level& lv, World& w, int x, int y, int z, u32 seed);
// what a felled growing tree's wood comes to: Dynamic Trees' volume (a branch r^2 * 64 of a log's 4096), logs and sticks
void dynTreeYield(const std::vector<std::pair<BlockId, u8>>& parts, std::map<std::string, int>& logs, int& sticks);

} // namespace dq
