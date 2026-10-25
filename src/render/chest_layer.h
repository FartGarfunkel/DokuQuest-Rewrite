// render/chest_layer.h — chest lids that open while the chest's screen is open (0.9.0.65/.83 behaviour).
//   chestScreenOpened(pos, true/false)  called by the container screen (ui/inventory.cpp) when it opens / closes
//   chestHiddenAt(x, y, z)              the mesher leaves that chest out while the layer draws it (any thread)
#pragma once
#include "common/common.h"
#include <atomic>

namespace dq {

extern std::atomic<bool> g_chestHide;
extern std::atomic<int> g_chestHideX, g_chestHideY, g_chestHideZ;
inline bool chestHiddenAt(int x, int y, int z) {
  return g_chestHide.load(std::memory_order_relaxed) && g_chestHideX.load(std::memory_order_relaxed) == x &&
         g_chestHideY.load(std::memory_order_relaxed) == y && g_chestHideZ.load(std::memory_order_relaxed) == z;
}
void chestScreenOpened(ivec3 pos, bool open);
void registerChestLayer();   // called from registerItemLayers()

} // namespace dq
