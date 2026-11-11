// sim/forge.h — TerraFirmaCraft's forge: charcoal in a pit walled in by stone (four sides and below), lit with a
// firestarter. Charcoal burns it to about 1350 °C; each bellows beside it adds 350 °C (two at most: the fantasy metals
// need them). Up to 8 charcoal; five places to heat things (ingots to working heat, a vessel or a crucible to melt what's
// in it, sim/metalwork.h). Rain on an open forge puts it out. Block forge, entity {type: "forge", fuel, burn, temp, lit,
// items[5]}; ticked from sim/content.cpp.
#pragma once
#include "sim/level.h"
#include "core/itemstack.h"

namespace dq {

constexpr int FORGE_FUEL_MAX = 8, FORGE_SLOTS = 5;
constexpr float FORGE_PEAK = 1350, BELLOWS_HEAT = 350;
bool forgeBuild(World& w, int x, int y, int z, ItemStack& charcoal);   // charcoal into a stone-ringed pit: a forge
bool forgeAddFuel(World& w, int x, int y, int z, ItemStack& charcoal);
bool forgeLight(Level& lv, World& w, int x, int y, int z);
bool forgePut(World& w, int x, int y, int z, ItemStack& held);          // a heatable thing into a free place
ItemStack forgeTake(World& w, int x, int y, int z);                     // the last thing put in
void forgeTick(Level& lv, double dt);
bool bellowsPump(World& w, int x, int y, int z);   // use on bellows: ten seconds of blowing

} // namespace dq
