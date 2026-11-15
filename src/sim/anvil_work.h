// sim/anvil_work.h — an anvil worked in the world (the anvil scene, ui/cinematic_table.cpp; after Cinematic Interactions'
// AnvilStation): the work lies on the anvil's face and what goes into it beside it (block entity {type:"anvilwork",
// items:[work, addition]}, so it syncs and spills when the anvil breaks), and the forging is the anvil screen's own:
// stationResult("anvil", ...) (repairs, two of a kind, an enchanted book), its level cost, the anvil's wear.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"
#include "sim/enchanting.h"

namespace dq {
class Player;

std::vector<ItemStack> anvilWorkItems(World& w, ivec3 at);                                  // [work, addition]
bool anvilWorkPut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held);    // all of what's held (the same tops it up)
bool anvilWorkTake(Level& lv, Player& p, World& w, ivec3 at, int slot);                    // back into the bag
StationResult anvilWorkResult(World& w, ivec3 at);                                          // what forging would make
// forges it: the result into the bag, the pieces used, the levels spent, the anvil worn (it may chip, or break: then
// what's left on it spills). false (and why, toasted) if it can't
bool anvilWorkForge(Level& lv, Player& p, World& w, ivec3 at, bool leaveOnAnvil = false);   // (Tactile: the result stays where the work was)

} // namespace dq
