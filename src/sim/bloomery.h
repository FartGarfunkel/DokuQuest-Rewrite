// sim/bloomery.h — TerraFirmaCraft's bloomery, for iron: the bloomery with a chimney of stone bricks over it (one to three
// high: room for 8, 16 or 24 ore), loaded with iron ore and as much charcoal, lit with a firestarter; five in-game hours
// later (TFC's 14.4, shortened) it gives a raw bloom for every hundred units of iron, to be refined on an anvil into iron.
// Block bloomery, entity {type: "bloomery", ore[] (item ids), units, charcoal, lit, t, out}; ticked from sim/content.cpp.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

constexpr double BLOOMERY_HOURS = 5;
int bloomeryCapacity(World& w, int x, int y, int z);                                 // 8 per chimney block (0..24)
bool bloomeryLoad(World& w, int x, int y, int z, ItemStack& held, std::string* why = nullptr);   // one iron ore or charcoal
bool bloomeryLight(Level& lv, World& w, int x, int y, int z);
ItemStack bloomeryTake(World& w, int x, int y, int z);                              // a finished bloom (or nothing)
void bloomeryTick(Level& lv, double dt);

} // namespace dq
