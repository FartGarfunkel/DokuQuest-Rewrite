// sim/furnace_work.h — a furnace (smoker, blast furnace) loaded by hand in the world (the furnace scene,
// ui/cinematic_table.cpp; after Cinematic Interactions' FurnaceStation): what smelts there into its mouth, fuel under it,
// the smelted out of it — the furnace screen's own rules (smeltResult for the furnace's kind, fuelValue), on its block
// entity {type:"furnace", items:[input, fuel, output], ...} so it burns on as ever.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {
class Player;

std::vector<ItemStack> furnaceItems(World& w, ivec3 at);                                     // [input, fuel, output]
bool furnacePut(Level& lv, Player& p, World& w, ivec3 at, int slot, ItemStack& held);       // 0 the input, 1 the fuel
bool furnaceTake(Level& lv, Player& p, World& w, ivec3 at, int slot);                       // any of the three
float furnaceCook(World& w, ivec3 at);                                                        // 0..1 of the item cooking
float furnaceBurn(World& w, ivec3 at);                                                        // 0..1 of the fuel left burning

} // namespace dq
