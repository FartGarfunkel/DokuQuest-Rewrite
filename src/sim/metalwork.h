// sim/metalwork.h — melting and casting (after TerraFirmaCraft's small vessel and ceramic molds): ore (pieces, samples,
// powder) and metal go into a fired vessel, four stacks; heated on a firepit or in a forge past its melting point
// (core/metals.h: the mix's) it becomes liquid metal of that makeup (tag "metal" {comp, u}). Used with a fired mold in the
// other hand it pours a hundred units into it (tool molds take only the metals that cast: the copper family, gold, silver,
// tin, zinc, bismuth; the ingot mold takes any). A filled mold cooled below half its metal's melting point breaks open
// for the piece (a head of that makeup, or an ingot); now and then the mold breaks with it.
#pragma once
#include "core/metals.h"

namespace dq {

constexpr int VESSEL_SLOTS = 4, VESSEL_STACK = 16, MOLD_UNITS = 100;
bool vesselAdd(ItemStack& vessel, ItemStack& from);           // ore or metal into it (a stack, as much as fits)
std::vector<ItemStack> vesselEmpty(ItemStack& vessel);         // what's in it, back out (not when it's liquid)
int vesselUnits(const ItemStack& vessel);
Comp vesselComp(const ItemStack& vessel);
bool vesselMolten(const ItemStack& vessel);                   // liquid metal, hot enough to pour
void vesselMeltStep(ItemStack& vessel);                       // after heating: melts once hot enough
bool pourInto(ItemStack& vessel, ItemStack& mold, std::string* why = nullptr);   // mold: one fired mold (n = 1)
std::pair<ItemStack, bool> breakMold(ItemStack& mold, ItemStack* opened = nullptr);   // a stack: one opened, the rest stay filled; the opened mold (if it held) in *opened        // the piece, and whether the mold survived (mold emptied, or cleared)
bool moldFilled(const ItemStack& mold);
class Level; class Player; class World;
// use with a vessel or a filled mold in hand (sim/controller.cpp): fill from the other hand, empty (sneak), pour into a mold
// in the other hand, break a cooled mold open. true if it took the click.
bool metalworkUse(Level& lv, Player& p);
bool metalworkPourOnGround(Level& lv, Player& p, World& w, int x, int y, int z);   // a molten vessel used on molds set down on the ground (an item pile)

} // namespace dq
