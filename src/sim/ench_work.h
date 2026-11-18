// sim/ench_work.h — the enchanting table worked in the world (the enchanting scene, ui/cinematic_table.cpp; after
// the enchant scene's): the item hovers over the table and the lapis lies on it (block entity
// {type:"enchwork", items:[item, lapis]}, synced, spilt when it breaks); the offers and the enchanting are the enchanting
// screen's own (sim/enchanting.h enchOffers / enchApplyOffer: the shelves round it, your levels, the lapis).
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include "sim/enchanting.h"
#include <array>

namespace dq {
class Player;

std::vector<ItemStack> enchWorkItems(World& w, ivec3 at);                                  // [item, lapis]
bool enchWorkPut(Level& lv, Player& p, World& w, ivec3 at, ItemStack& held);               // lapis goes by the table, anything else over it
bool enchWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot);                     // 0 the item, 1 the lapis
bool enchWorkOffers(World& w, ivec3 at, Player& p, std::array<EnchOffer, 3>& out);         // false: nothing to offer
std::string enchWorkApply(Level& lv, Player& p, World& w, ivec3 at, int offer);            // "" = enchanted, else why not

} // namespace dq
