// render/rod_tip.h — where each player's fishing rod tip was last drawn (world position), so the line to the float
// leaves the real tip: set by the first-person hand (render/item_layers.cpp) and the third-person hand
// (render/char_layer.cpp), read by the line in render/content_layers.cpp.
#pragma once
#include "common/common.h"
#include <unordered_map>

namespace dq {

struct RodTipSlot { glm::dvec3 pos{0}; double t = -1e9; };
inline std::unordered_map<u32, RodTipSlot>& rodTips() { static std::unordered_map<u32, RodTipSlot> m; return m; }
inline void setRodTip(u32 player, glm::dvec3 world, double now) { rodTips()[player] = {world, now}; }
// a tip drawn within the last half second
inline bool getRodTip(u32 player, double now, vec3& out) {
  auto it = rodTips().find(player);
  if (it == rodTips().end() || now - it->second.t > 0.5) return false;
  out = vec3(it->second.pos);
  return true;
}

} // namespace dq
