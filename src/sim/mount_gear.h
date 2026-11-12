// sim/mount_gear.h — what a tamed mount wears and carries, and its gear screen:
//   the saddle (Creature::saddled), horse armour (a horse only: data "horseArmor" + "horseArmorD", its wear; the item's
//   "horseArmor" {points, durability, mat}, tools/port/horse_gear.py), and what it carries in data "bags" with data "pack":
//   "bags" (saddle bags, 18 slots, any saddled mount) or "chest" (a chest, 27 slots: a donkey, mule, llama or camel).
// Sneak and use your mount with an empty hand: the gear screen (the inventory's container mode with InvOpen gear slots,
// ui/inventory.cpp): the saddle and armour slots over what it carries. render/creature_layer.cpp draws the barding, the
// chests on its flanks and the bags.
#pragma once
#include "core/itemstack.h"
#include <string>
#include <vector>

namespace dq {

class Level; class Player; class Creature;

bool mountTakesArmour(const Creature& c);   // a grown horse
bool mountTakesChest(const Creature& c);    // a grown donkey, mule, llama or camel
std::string mountPack(const Creature& c);   // what it carries: "chest", "bags" or ""
int mountPackSlots(const std::string& pack);
// the gear slots: [the saddle, the horse armour (a horse only)]; what may go in one; putting them on (the screen's changes)
std::vector<ItemStack> mountGear(const Creature& c);
bool mountGearFits(const Creature& c, int slot, const ItemStack& s);
void setMountGear(Level& lv, Creature& c, const std::vector<ItemStack>& gear);
void openMountScreen(Level& lv, Player& p, Creature& c, const std::string& title);
// horse armour's protection points (Creature::armourPoints adds them) and its wear (Creature::hurt)
float horseArmourPoints(const Creature& c);
void horseArmourWear(Creature& c, Level& lv);
// on death: the saddle, the armour, the chest or bags and everything in them
void mountGearDrops(Level& lv, Creature& c);
// the stable's book keeps these with a companion called away and back (sim/stable.cpp)
extern const char* const MOUNT_GEAR_KEYS[6];

} // namespace dq
