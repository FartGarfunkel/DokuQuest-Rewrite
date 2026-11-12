// sim/heat.h — how hot a thing is (after TerraFirmaCraft's ItemHeat): metal, ore and pottery carry a temperature in their
// stack's tag ("heat", °C), heated on a firepit, a forge or in a kiln and cooling in the air. Smithing needs working heat,
// pouring needs molten metal; hot things glow and the tooltip names the heat as TFC does.
#pragma once
#include "core/itemstack.h"

namespace dq {

constexpr float AIR_TEMP = 20;
float itemHeat(const ItemStack& s);                 // °C (AIR_TEMP when it carries none)
void setItemHeat(ItemStack& s, float c);            // at or below 25 °C the tag goes (so it stacks with cold ones again)
bool heatable(const std::string& itemId);           // ingots, ores, heads, sheets, blooms, pottery, the crucible...
// one step toward `target`: warming quickly toward a hotter fire, cooling slowly (2% of the gap a second, at least 1 °C)
float heatTowards(float h, float target, float dt);
std::string heatName(float c);                      // "" when cold; Warming, Hot, Very Hot, Faint Red ... Brilliant White
u32 heatGlow(float c);                              // 0xRRGGBB of the glow, 0 below red heat
class Level;
void heatTick(Level& lv, double dt);                // what players carry cools (once a second); hot metal in a bare hand singes
// dipped in water: cold at once (a hiss and steam); a cast or forged head quenched from 300 °C or more is a little tougher
// (tag "q": +5% durability when it becomes a tool). false if it wasn't hot.
bool quench(ItemStack& s);

} // namespace dq
