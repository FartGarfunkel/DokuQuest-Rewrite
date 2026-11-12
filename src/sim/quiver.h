// sim/quiver.h — the quiver: worn in its own slot beside the backpack's (Player::quiver), four slots of arrows of its own
// (its stack's tag "c", like a backpack's contents). A bow draws from it after the off hand; arrows you pick up go into
// it first. See sim/quiver.cpp.
#pragma once
#include "sim/player.h"

namespace dq {

constexpr int QUIVER_SLOTS = 4;
bool isArrowItem(const ItemStack& s);            // an arrow of any kind (plain, golden, flaming, purple...)
bool isQuiverItem(const ItemStack& s);           // a quiver or a golden quiver
int quiverSlotsOf(const ItemStack& q);           // its arrow slots: 4 for a quiver, 0 for the golden one (it never runs dry)
std::vector<ItemStack> quiverLoad(const ItemStack& q);          // its slots (an old quiver's count of arrows comes out as stacks)
void quiverSave(ItemStack& q, const std::vector<ItemStack>& c);
int quiverArrows(Player& p);                     // arrows in the worn quiver and any carried in the bag
std::string quiverTake(Player& p);               // one arrow out (the worn quiver first): its kind, "" = none
bool goldenQuiver(Player& p);                    // a golden quiver worn or carried
int quiverStow(Player& p, ItemStack& s);         // arrows into the worn quiver; returns how many are left over
int quiverFill(const ItemStack& q);              // how full it looks: 0 empty, 1..4 by the stack (the golden one is always 4)

} // namespace dq
