// render/chest_layer.h — chest lids that open in the world: while a chest's screen or the chest scene is open
// (ui/inventory.cpp, ui/cinematic_table.cpp), a large chest's two halves together, and the chests the others are working
// (Player::stationKind 5); what's in an open chest lies inside it, in its slots' rows (sim/chest_work.h).
//   chestScreenOpened(pos, true/false)  the screen / scene opens or shuts that chest (its partner goes with it)
//   the mesher leaves the open ones out while this layer draws them (g_chestHidden*)
#pragma once
#include "common/common.h"
#include <algorithm>
#include <atomic>

namespace dq {

constexpr int CHEST_HIDE_MAX = 32;   // (yours first, then the lids left open about you)
extern std::atomic<int> g_chestHiddenN;
extern std::atomic<int> g_chestHidden[CHEST_HIDE_MAX][3];
inline bool chestHiddenAt(int x, int y, int z) {   // (any thread: the mesher)
  for (int i = 0, n = std::min(CHEST_HIDE_MAX, g_chestHiddenN.load(std::memory_order_relaxed)); i < n; i++)
    if (g_chestHidden[i][0].load(std::memory_order_relaxed) == x && g_chestHidden[i][1].load(std::memory_order_relaxed) == y && g_chestHidden[i][2].load(std::memory_order_relaxed) == z) return true;
  return false;
}
void chestScreenOpened(ivec3 pos, bool open);
void registerChestLayer();   // called from registerItemLayers()

} // namespace dq
