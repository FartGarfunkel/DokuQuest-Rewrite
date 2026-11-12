// sim/straw.cpp — see straw.h
#include "sim/straw.h"
#include "sim/controller.h"
#include "sim/player.h"
#include "sim/gameplay.h"
#include "sim/blockinfo.h"
#include "core/registry.h"
#include <unordered_set>

namespace dq {

bool isGrassPlant(BlockId id) {
  static std::unordered_set<BlockId> set; static size_t n = 0;
  if (n != reg().blocks.size()) {
    n = reg().blocks.size(); set.clear();
    for (const char* k : {"tall_grass", "tall_grass_double", "fern", "large_fern", "dry_grass", "jungle_grass", "bushy_grass", "needlegrass", "marram_grass"})
      if (BlockId b = reg().blockId(k)) set.insert(b);
  }
  return set.count(id) > 0;
}

void strawOnBreak(Level& lv, PlayerController& pc, Player& p, int x, int y, int z, BlockId id) {
  const ItemStack& held = p.held();
  const ItemDef* d = held.empty() ? nullptr : held.def();
  if (!d || !(d->has("knife") || d->has("scythe"))) return;
  if (isGrassPlant(id) && p.mode != "creative") dropItem(lv, p.realm, ItemStack("straw", 1), vec3(x + 0.5f, y + 0.3f, z + 0.5f), true);
  if (!d->has("scythe")) return;
  static thread_local bool sweeping = false;
  if (sweeping || !(isGrassPlant(id) || bhas(id, BF::Crop))) return;
  sweeping = true;
  World& w = lv.world(p.realm);
  for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
    if (!dx && !dz) continue;
    const BlockId b = w.getBlock(x + dx, y, z + dz);
    const BlockDef& bd = reg().block(b);   // grass, and only ripe crops (a scythe doesn't cut the green ones)
    const bool ripeCrop = bhas(b, BF::Crop) && bd.props.contains("crop") && bd.props["crop"].is_object() && (w.getMeta(x + dx, y, z + dz) & 15) >= bd.props["crop"].value("stages", 8) - 1;
    if (isGrassPlant(b) || ripeCrop) pc.breakBlock(x + dx, y, z + dz);
  }
  sweeping = false;
}

} // namespace dq
