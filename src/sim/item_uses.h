// sim/item_uses.h — the drops that had no use (tools/port/item_uses.py, checked by tools/test_content.cpp "every item has a
// use"): the War Horn (the Minotaur's horn: your tamed creatures come to you, quicker and stronger for a while), the dragons'
// armour sets and their full-set bonuses (an item's armor.set; all four pieces of one set worn), and the capes' small
// effects (the ember cape against fire, the frost cape against frost, the royal cape at the shops).
#pragma once
#include "sim/level.h"
#include "sim/player.h"
#include "sim/combat.h"

namespace dq {

constexpr float HORN_SECS = 20, HORN_COOLDOWN = 60, HORN_RANGE = 64;
constexpr float RALLY_DAMAGE = 1.3f, RALLY_SPEED = 1.3f;

std::string gearSet(const Player& p);                       // the set worn whole ("" none): "dragonscale", "frostscale"...
std::string capeOf(const Player& p);                        // the cape worn ("" none)
// the share of fire harm the cape and the set take away (added to sim/inventory.cpp fireResist)
float gearFireResist(const Player& p);
// harm after armour, for the cape and the set (frost, falls, the Wyrmblood's tenth): a factor on it
float gearHarmFactor(const Player& p, const DamageSource& src);
// after a blow landed on you: the set strikes back at whoever struck (slowed, poisoned, set burning, jolted)
void gearOnHurt(Level& lv, Player& p, const DamageSource& src, float dealt);
float gearMoveFactor(Level& lv, const Player& p);           // quicker on sand (Dunewalker), in water (Deepcurrent)
float capeShopFactor(const Player& p);                      // the royal cape: the shops' prices times this

// the War Horn: false (and why) if it can't be blown yet; else how many creatures answered
bool warHornBlow(Level& lv, Player& p, int& answered, std::string& why);
bool rallied(Level& lv, const Entity& e);                    // under a horn's call (stronger and quicker)

void registerItemUses();   // called from initCore(): the horn's use, the sets' and capes' effects over time

} // namespace dq
