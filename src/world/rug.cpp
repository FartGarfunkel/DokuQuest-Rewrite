// world/rug.cpp — see world/rug.h
#include "world/rug.h"
#include "world/shapes.h"
#include <array>
#include <mutex>

namespace dq {

static const std::array<const char*, 17> RUGS = {"white_carpet", "orange_carpet", "magenta_carpet", "light_blue_carpet", "yellow_carpet", "lime_carpet",
  "pink_carpet", "gray_carpet", "light_gray_carpet", "cyan_carpet", "purple_carpet", "blue_carpet", "brown_carpet", "green_carpet", "red_carpet",
  "black_carpet", "moss_carpet"};

static const std::array<BlockId, 18>& rugIds() {
  static std::array<BlockId, 18> ids{};
  static std::once_flag once;
  std::call_once(once, [] { for (size_t i = 0; i < RUGS.size(); i++) ids[i + 1] = reg().blockId(RUGS[i]); });
  return ids;
}
u8 rugCodeOf(BlockId carpet) {
  if (!carpet) return 0;
  const auto& ids = rugIds();
  for (int i = 1; i < (int)ids.size(); i++) if (ids[i] == carpet) return (u8)(RUG_BIT | i);
  return 0;
}
BlockId rugBlockOf(u8 coat) {
  if (!isRugCoat(coat)) return 0;
  const auto& ids = rugIds(); const int i = coat & 0x7F;
  return i > 0 && i < (int)ids.size() ? ids[i] : 0;
}
bool rugFloor(BlockId id, u8 carve) { return id && !carve && fullSolid(id); }

} // namespace dq
