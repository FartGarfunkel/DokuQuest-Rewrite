// sim/brewing.h — brewing, potions and what they do to you (game/brewing.js + arcana.js's four + nether2.js's
// ghast tear). Bottles of water + nether wart → awkward potion → (sugar, rabbit's foot, ...) → potions;
// redstone longer, glowstone stronger, fermented spider eye turns it, gunpowder makes it splash.
// The brewing stand is a block entity in the original's JSON form:
//   { type: "brew", items: [bottle, bottle, bottle, ingredient, fuel], fuel: n, t: secondsLeft, ing: "key" }
#pragma once
#include "sim/magic_combat.h"
#include "sim/magic_data.h"

namespace dq {

class World;

std::string potKey(const std::string& P, const std::string& v = "", bool splash = false);
const json* potionInfo(const std::string& itemId);           // the item's `potion` def ({P, v, fx, col, splash}) or null
std::string brewResult(const std::string& from, const std::string& ing);   // "" = nothing
bool isBrewIngredient(const std::string& id);
bool brewAccepts(int slot, const std::string& id);            // 0-2 potions, 3 ingredient, 4 cinder powder

json& brewEntity(World& w, int x, int y, int z);              // the stand's record (created if missing)
// one step of a stand (the host's tickFurnaces): returns true if it changed
bool tickBrew(Level& lv, World& w, json& e, double dt, int x, int y, int z);
std::string brewStatus(const json& e);                        // the panel's status line
float brewProgress(const json& e);                            // 0..1 of the bubbling tube
float brewFuel(const json& e);                                // 0..1 of the fuel bar

// drinking and throwing (itemUse on a potion), filling bottles, Bottle o' Enchanting
bool drinkPotion(Level& lv, Player& p, ItemStack& held);
void throwPotion(Level& lv, Player& p, ItemStack& held);
void splashPotionAt(Level& lv, int realm, vec3 at, const std::string& potionItem, Player* owner);
bool fillBottle(Level& lv, Player& p, ItemStack& held, ivec3 hit, int face);   // at water or a cauldron
bool breakXpBottle(Level& lv, Player& p, ItemStack& held);

std::string potionTooltip(const std::string& itemId);         // the lines the tooltip adds ("Speed II (1:30)")

} // namespace dq
